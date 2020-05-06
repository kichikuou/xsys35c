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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *input;

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

static Map *read_FUNC(void) {
	input += 8; // skip section header
	Map *functions = new_map();
	uint32_t count = read_le32();
	for (uint32_t i = 0; i < count; i++) {
		const char *name = read_string();
		Function *func = calloc(1, sizeof(Function));
		func->name = name;
		func->page = read_le16();
		func->addr = read_le32();
		func->argc = -1;
		map_put(functions, name, func);
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

	if (memcmp(input, "AINI", 4))
		error("%s: not an AIN file", path);

	// Decrypt
	for (uint8_t *p = input + 4; p < input_end; p++)
		*p = *p << 2 | *p >> 6;

	Ain *ain = calloc(1, sizeof(Ain));

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
