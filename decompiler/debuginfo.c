/* Copyright (C) 2025 <KichikuouChrome@gmail.com>
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

#define DSYM_VERSION 0

static Vector *read_string_array(FILE *fp, uint32_t section_len, const char *path) {
	uint8_t *buf = malloc(section_len);
	if (fread(buf, section_len, 1, fp) != 1)
		error("%s: %s", path, strerror(errno));
	uint32_t count = le32(buf);
	Vector *vec = new_vec();
	uint8_t *p = buf + 4;
	for (uint32_t i = 0; i < count; i++) {
		vec_push(vec, p);
		p += strlen((char *)p) + 1;
	}
	if (p != buf + section_len)
		error("%s: invalid string array section", path);
	return vec;
}

DebugInfo *debug_info_read(const char *path) {
	FILE *fp = checked_fopen(path, "rb");
	uint8_t header[12];

	if (fread(header, sizeof(header), 1, fp) != 1 || memcmp(header, "DSYM", 4))
		error("%s: not a DSYM file", path);
	if (le32(header + 4) != DSYM_VERSION)
		error("%s: unsupported DSYM version", path);
	int nr_sections = le32(header + 8);

	DebugInfo *di = calloc(1, sizeof(DebugInfo));
	di->srcs = calloc(1, sizeof(Map));
	for (int i = 0; i < nr_sections; i++) {
		uint8_t section_header[8];
		if (fread(section_header, sizeof(section_header), 1, fp) != 1)
			break;
		uint32_t section_len = le32(section_header + 4) - 8;

		if (!memcmp(section_header, "SRCS", 4)) {
			di->srcs->keys = read_string_array(fp, section_len, path);
		} else if (!memcmp(section_header, "SCNT", 4)) {
			di->srcs->vals = read_string_array(fp, section_len, path);
		} else if (!memcmp(section_header, "VARI", 4)) {
			di->variables = read_string_array(fp, section_len, path);
		} else {
			fseek(fp, section_len, SEEK_CUR);
		}
	}
	if (!di->srcs->keys || !di->srcs->vals || di->srcs->keys->len != di->srcs->vals->len ||
		!di->variables) {
		error("%s: invalid DSYM file", path);
	}
	return di;
}
