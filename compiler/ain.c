/* Copyright (C) 2020 <KichikuouChrome@gmail.com>
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
#include <stdlib.h>
#include <string.h>

static void ain_emit_HEL0(Buffer *out, Map *dlls) {
	emit(out, 'H');
	emit(out, 'E');
	emit(out, 'L');
	emit(out, '0');
	emit_dword(out, 0);  // reserved
	emit_dword(out, dlls->keys->len);
	for (int i = 0; i < dlls->keys->len; i++) {
		emit_string(out, dlls->keys->data[i]);
		emit(out, 0);
		Vector *funcs = dlls->vals->data[i];
		emit_dword(out, funcs->len);
		for (int j = 0; j < funcs->len; j++) {
			DLLFunc *f = funcs->data[j];
			emit_string(out, f->name);
			emit(out, 0);
			emit_dword(out, f->argc);
			for (int k = 0; k < f->argc; k++)
				emit_dword(out, f->argtypes[k]);
		}
	}
}

static int func_compare_by_name(const void *a, const void *b) {
	const Function *fa = *(const Function **)a;
	const Function *fb = *(const Function **)b;
	return strcmp(fa->name, fb->name);
}

static int func_compare_by_addr(const void *a, const void *b) {
	const Function *fa = *(const Function **)a;
	const Function *fb = *(const Function **)b;
	if (fa->page != fb->page)
		return fa->page - fb->page;
	else
		return fa->addr - fb->addr;
}

static void ain_emit_FUNC(Buffer *out, HashMap *functions) {
	emit(out, 'F');
	emit(out, 'U');
	emit(out, 'N');
	emit(out, 'C');
	emit_dword(out, 0);  // reserved

	int nfunc = functions->occupied;
	const Function **items = malloc(nfunc * sizeof(const Function *));
	const Function **p = items;
	for (HashItem *i = hash_iterate(functions, NULL); i; i = hash_iterate(functions, i))
		*p++ = (Function *)i->val;
	if (config.disable_ain_variable)  // FIXME: Use a dedicated config for this.
		qsort(items, nfunc, sizeof(const Function *), func_compare_by_addr);
	else
		qsort(items, nfunc, sizeof(const Function *), func_compare_by_name);

	emit_dword(out, nfunc);
	for (int i = 0; i < nfunc; i++) {
		emit_string(out, items[i]->name);
		emit(out, 0);
		emit_word(out, items[i]->page);
		emit_dword(out, items[i]->addr);
	}
}

static void ain_emit_VARI(Buffer *out, Vector *variables) {
	emit(out, 'V');
	emit(out, 'A');
	emit(out, 'R');
	emit(out, 'I');
	emit_dword(out, 0);  // reserved
	emit_dword(out, variables->len);
	for (int i = 0; i < variables->len; i++) {
		emit_string(out, variables->data[i]);
		emit(out, 0);
	}
}

static void ain_emit_MSGI_head(Buffer *out, int msg_count) {
	emit(out, 'M');
	emit(out, 'S');
	emit(out, 'G');
	emit(out, 'I');
	emit_dword(out, 0);  // reserved
	emit_dword(out, msg_count);
}

static void ain_write_buf(Buffer *buf, FILE *fp) {
	uint8_t *end = buf->buf + buf->len;
	for (uint8_t *p = buf->buf; p < end; p++)
		fputc(*p >> 2 | *p << 6, fp);
}

void ain_write(Compiler *compiler, FILE *fp) {
	fputs("AINI", fp);
	Buffer *out = new_buf();
	emit_dword(out, 4);
	ain_emit_HEL0(out, compiler->dlls);
	ain_emit_FUNC(out, compiler->functions);
	if (!config.disable_ain_variable)
		ain_emit_VARI(out, compiler->variables);
	if (compiler->msg_count > 0)
		ain_emit_MSGI_head(out, compiler->msg_count);
	ain_write_buf(out, fp);
	if (compiler->msg_count > 0)
		ain_write_buf(compiler->msg_buf, fp);
}
