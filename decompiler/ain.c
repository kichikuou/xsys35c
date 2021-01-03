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
#include "xsys35dc.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *input;

static uint32_t FunctionHash(const Function *f) {
	return ((f->page * 16777619) ^ f->addr) * 16777619;
}

static int FunctionCompare(const Function *f1, const Function *f2) {
	return f1->page == f2->page && f1->addr == f2->addr ? 0 : 1;
}

static inline uint32_t read_le16(void) {
	uint16_t n = input[0] | input[1] << 8;
	input += 2;
	return n;
}

static inline uint32_t read_le32(void) {
	uint32_t n = le32(input);
	input += 4;
	return n;
}

static inline const char *read_string(void) {
	const char *s = (const char *)input;
	input += strlen(s) + 1;
	return s;
}

static Map *read_HEL0(void) {
	input += 8; // skip section header
	Map *dlls = new_map();
	uint32_t dll_count = read_le32();
	for (uint32_t i = 0; i < dll_count; i++) {
		const char *dllname = read_string();
		Vector *funcs = new_vec();
		uint32_t func_count = read_le32();
		for (uint32_t j = 0; j < func_count; j++) {
			const char *funcname = read_string();
			uint32_t arg_count = read_le32();
			DLLFunc *f = calloc(1, sizeof(DLLFunc) + arg_count * sizeof(uint32_t));
			f->name = funcname;
			f->argc = arg_count;
			for (uint32_t k = 0; k < f->argc; k++)
				f->argtypes[k] = read_le32();
			vec_push(funcs, f);
		}
		map_put(dlls, dllname, funcs);
	}
	return dlls;
}

static HashMap *read_FUNC(void) {
	input += 8; // skip section header
	HashMap *functions = new_function_hash();
	uint32_t count = read_le32();
	for (uint32_t i = 0; i < count; i++) {
		const char *name = read_string();
		Function *func = calloc(1, sizeof(Function));
		func->name = name;
		func->page = read_le16();
		func->addr = read_le32();
		func->argc = -1;
		Function *existing_entry = hash_get(functions, func);
		if (existing_entry) {
			if (!existing_entry->aliases)
				existing_entry->aliases = new_vec();
			vec_push(existing_entry->aliases, (void *)name);
		} else {
			hash_put(functions, func, func);
		}
	}
	return functions;
}

static Vector *read_strings_section(void) {
	input += 8; // skip section header
	Vector *v = new_vec();
	uint32_t count = read_le32();
	for (uint32_t i = 0; i < count; i++)
		vec_push(v, (void *)read_string());
	return v;
}

HashMap *new_function_hash(void) {
	return new_hash((HashFunc)FunctionHash, (HashKeyCompare)FunctionCompare);
}

Ain *ain_read(const char *path) {
	FILE *fp = checked_fopen(path, "rb");
	if (fseek(fp, 0, SEEK_END) != 0)
		error("%s: %s", path, strerror(errno));
	long size = ftell(fp);
	if (size < 0)
		error("%s: %s", path, strerror(errno));
	if (fseek(fp, 0, SEEK_SET) != 0)
		error("%s: %s", path, strerror(errno));
	input = malloc(size);
	const uint8_t *input_end = input + size;
	if (fread(input, size, 1, fp) != 1)
		error("%s: read error", path);
	fclose(fp);

	uint32_t version = le32(input + 4);
	if ((version == 1 && memcmp(input, "AINI", 4)) ||
		(version == 2 && memcmp(input, "AIN2", 4)))
		error("%s: not an AIN file", path);

	// Decrypt
	for (uint8_t *p = input + 4; p < input_end; p++)
		*p = *p << 2 | *p >> 6;

	Ain *ain = calloc(1, sizeof(Ain));
	ain->filename = basename(path);
	ain->version = version;

	input += 8;
	while (input < input_end) {
		if (!memcmp(input, "HEL0", 4))
			ain->dlls = read_HEL0();
		else if (!memcmp(input, "FUNC", 4))
			ain->functions = read_FUNC();
		else if (!memcmp(input, "VARI", 4))
			ain->variables = read_strings_section();
		else if (!memcmp(input, "MSGI", 4))
			ain->messages = read_strings_section();
		else
			error("%s: unknown ain section '%.4s'", path, input);
	}
	return ain;
}

void write_hels(Map *dlls, const char *dir) {
	for (int i = 0; i < dlls->keys->len; i++) {
		Vector *funcs = dlls->vals->data[i];
		if (funcs->len == 0)
			continue;

		char hel_name[100];
		snprintf(hel_name, sizeof(hel_name), "%s.HEL", (char *)dlls->keys->data[i]);
		char *hel_path = path_join(dir, hel_name);
		FILE *fp = checked_fopen(hel_path, "w");

		for (int j = 0; j < funcs->len; j++) {
			DLLFunc *func = funcs->data[j];
			fprintf(fp, "void %s(", func->name);
			if (func->argc == 0)
				fputs("void", fp);
			const char *sep = "";
			for (int k = 0; k < func->argc; k++) {
				const char *type;
				switch (func->argtypes[k]) {
				case HEL_pword: type = "pword"; break;
				case HEL_int: type = "int"; break;
				case HEL_ISurface: type = "ISurface"; break;
				case HEL_IString: type = "IString"; break;
				case HEL_IWinMsg: type = "IWinMsg"; break;
				case HEL_ITimer: type = "ITimer"; break;
				case HEL_IUI: type = "IUI"; break;
				case HEL_ISys3xDIB: type = "ISys3xDIB"; break;
				case HEL_ISys3xCG: type = "ISys3xCG"; break;
				case HEL_ISys3xStringTable: type = "ISys3xStringTable"; break;
				case HEL_ISys3xSystem: type = "ISys3xSystem"; break;
				case HEL_ISys3xMusic: type = "ISys3xMusic"; break;
				case HEL_ISys3xMsgString: type = "ISys3xMsgString"; break;
				case HEL_ISys3xInputDevice: type = "ISys3xInputDevice"; break;
				case HEL_ISys3x: type = "ISys3x"; break;
				case HEL_IConstString: type = "IConstString"; break;
				default: error("%s.%s: unknown parameter type %d", dlls->keys->data[i], func->name, func->argtypes[k]);
				}
				fprintf(fp, "%s%s arg%d", sep, type, k + 1);
				sep = ", ";
			}
			fputs(")\n", fp);
		}
		fclose(fp);
	}
}

static void json_indent(int delta) {
	static int lv = 0;
	if (delta < 0)
		lv += delta;
	for (int i = 0; i < lv; i++)
		fputs("  ", stdout);
	if (delta > 0)
		lv += delta;
}

static void json_string(const char *s) {
	putchar('"');
	while (*s) {
		if (iscntrl(*s)) {
			printf("\\u%04x", *s++);
			continue;
		}
		if (*s == '"' || *s == '\\')
			putchar('\\');
		putchar(*s++);
	}
	putchar('"');
}

static void json_key(const char *s) {
	json_string(s);
	fputs(": ", stdout);
}

static void json_maybe_comma(bool comma) {
	if (comma)
		putchar(',');
	putchar('\n');
}

static void json_open(const char *key, char ch) {
	json_indent(1);
	if (key)
		json_key(key);
	putchar(ch);
	putchar('\n');
}

static void json_close(char ch, bool append_comma) {
	json_indent(-1);
	putchar(ch);
	json_maybe_comma(append_comma);
}

void ain_dump(Ain *ain) {
	json_open(NULL, '{');
	if (ain->dlls) {
		Map *dlls = ain->dlls;
		json_open("HEL0", '{');
		for (int i = 0; i < dlls->keys->len; i++) {
			json_open(dlls->keys->data[i], '{');
			Vector *funcs = dlls->vals->data[i];
			for (int j = 0; j < funcs->len; j++) {
				DLLFunc *func = funcs->data[j];
				json_indent(0);
				json_key(func->name);
				putchar('[');
				const char *sep = "";
				for (int k = 0; k < func->argc; k++) {
					printf("%s%d", sep, func->argtypes[k]);
					sep = ", ";
				}
				putchar(']');
				json_maybe_comma(j < funcs->len - 1);
			}
			json_close('}', i < dlls->keys->len - 1);
		}
		json_close('}', ain->functions || ain->variables || ain->messages);
	}
	if (ain->functions) {
		json_open("FUNC", '{');
		for (HashItem *i = hash_iterate(ain->functions, NULL); i;) {
			Function *func = (Function *)i->val;
			json_indent(0);
			json_key(sjis2utf(func->name));
			printf("{ \"page\": %d, \"addr\": %d }", func->page, func->addr);
			i = hash_iterate(ain->functions, i);
			json_maybe_comma(i);
		}
		json_close('}', ain->variables || ain->messages);
	}
	if (ain->variables) {
		json_open("VARI", '[');
		Vector *v = ain->variables;
		for (int i = 0; i < v->len; i++) {
			json_indent(0);
			json_string(sjis2utf(v->data[i]));
			json_maybe_comma(i < v->len - 1);
		}
		json_close(']', ain->messages);
	}
	if (ain->messages) {
		json_open("MSGI", '[');
		Vector *v = ain->messages;
		for (int i = 0; i < v->len; i++) {
			json_indent(0);
			json_string(sjis2utf(v->data[i]));
			json_maybe_comma(i < v->len - 1);
		}
		json_close(']', false);
	}
	json_close('}', false);
}
