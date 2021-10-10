/* Copyright (C) 2021 <KichikuouChrome@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/
#include "xsys35c.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DSYM_VERSION 0

typedef struct {
	int line;
	int addr;
} LineInfo;

typedef struct {
	const char *name;
	int page;
	int addr;
	bool is_local;
} FuncInfo;

typedef struct DebugInfo {
	Map *srcs;
	Buffer *line_section;
	Vector *linemap;
	int nr_files;
	Vector *functions;
} DebugInfo;

struct DebugInfo *new_debug_info(Map *srcs) {
	DebugInfo *di = calloc(1, sizeof(DebugInfo));
	di->srcs = new_map();
	for (int i = 0; i < srcs->keys->len; i++)
		map_put(di->srcs, basename(srcs->keys->data[i]), srcs->vals->data[i]);
	di->functions = new_vec();
	return di;
}

static void add_local_functions(struct DebugInfo *di, Map *labels, int page) {
	for (int i = 0; i < labels->keys->len; i++) {
		Label *label = labels->vals->data[i];
		if (!label->is_function)
			continue;
		FuncInfo *fi = calloc(1, sizeof(FuncInfo));
		fi->name = labels->keys->data[i];
		fi->page = page;
		fi->addr = label->addr;
		fi->is_local = true;
		vec_push(di->functions, fi);
	}
}

static void add_global_functions(struct DebugInfo *di, HashMap *functions) {
	for (HashItem *i = hash_iterate(functions, NULL); i; i = hash_iterate(functions, i)) {
		Function *f = i->val;
		FuncInfo *fi = calloc(1, sizeof(FuncInfo));
		fi->name = f->name;
		fi->page = f->page - 1;  // 1-based to 0-based index
		fi->addr = f->addr;
		fi->is_local = false;
		vec_push(di->functions, fi);
	}
}

void debug_init_page(DebugInfo *di, int page) {
	if (!di->line_section) {
		di->line_section = new_buf();
		emit(di->line_section, 'L');
		emit(di->line_section, 'I');
		emit(di->line_section, 'N');
		emit(di->line_section, 'E');
		emit_dword(di->line_section, 0);  // section length (to be filled later)
		emit_dword(di->line_section, 0);  // nr_files (to be filled later)
	}

	assert(page == di->nr_files);
	assert(!di->linemap);
	di->linemap = new_vec();
}

void debug_line_add(DebugInfo *di, int line, int addr) {
	Vector *linemap = di->linemap;

	if (linemap->len > 0) {
		LineInfo *last = linemap->data[linemap->len - 1];
		assert(addr >= last->addr);
		assert(line >= last->line);
		if (addr == last->addr) {
			last->line = line;
			return;
		}
		if (line == last->line)
			return;
	}
	LineInfo *li = calloc(1, sizeof(LineInfo));
	li->line = line;
	li->addr = addr;
	vec_push(linemap, li);
	return;
}

void debug_line_reset(DebugInfo *di) {
	di->linemap->len = 0;
}

void debug_finish_page(DebugInfo *di, Map *labels) {
	add_local_functions(di, labels, di->nr_files);

	Vector *linemap = di->linemap;
	assert(linemap);

	// Drop the last entry because it points to the end address of the SCO.
	if (linemap->len > 0)
		linemap->len--;

	emit_dword(di->line_section, linemap->len);
	for (int i = 0; i < linemap->len; i++) {
		LineInfo *li = linemap->data[i];
		emit_dword(di->line_section, li->line);
		emit_dword(di->line_section, li->addr);
	}
	di->linemap = NULL;
	di->nr_files++;

	swap_dword(di->line_section, 4, di->line_section->len);
	swap_dword(di->line_section, 8, di->nr_files);
}

static void write_string_array_section(const char *tag, Vector *vec, FILE *fp) {
	int section_len = 12;
	for (int i = 0; i < vec->len; i++)
		section_len += strlen(vec->data[i]) + 1;

	fputs(tag, fp);
	fputdw(section_len, fp);
	fputdw(vec->len, fp);
	for (int i = 0; i < vec->len; i++) {
		fputs(vec->data[i], fp);
		fputc('\0', fp);
	}
}

int funcinfo_compare(const void *a, const void *b) {
	const FuncInfo *fa = *(const FuncInfo **)a;
	const FuncInfo *fb = *(const FuncInfo **)b;
	if (fa->page != fb->page)
		return fa->page - fb->page;
	else
		return fa->addr - fb->addr;
}

static void write_func_section(Vector *functions, FILE *fp) {
	fputs("FUNC", fp);
	long section_length_offset = ftell(fp);
	fputdw(0, fp);

	// Sort by address.
	qsort(functions->data, functions->len, sizeof(void *), funcinfo_compare);

	fputdw(functions->len, fp);
	for (int i = 0; i < functions->len; i++) {
		FuncInfo *fi = functions->data[i];
		fputs(fi->name, fp);
		fputc(0, fp);
		fputw(fi->page, fp);
		fputdw(fi->addr, fp);
		fputc(fi->is_local, fp);
	}

	long section_length = ftell(fp) - section_length_offset + 4;
	fseek(fp, section_length_offset, SEEK_SET);
	fputdw(section_length, fp);
	fseek(fp, 0, SEEK_END);
}

void debug_info_write(struct DebugInfo *di, Compiler *compiler, FILE *fp) {
	add_global_functions(di, compiler->functions);

	fputs("DSYM", fp);
	fputdw(DSYM_VERSION, fp);
	fputdw(5, fp);  // nr_sections

	write_string_array_section("SRCS", di->srcs->keys, fp);
	write_string_array_section("SCNT", di->srcs->vals, fp);
	fwrite(di->line_section->buf, di->line_section->len, 1, fp);
	write_func_section(di->functions, fp);
	write_string_array_section("VARI", compiler->variables, fp);
}
