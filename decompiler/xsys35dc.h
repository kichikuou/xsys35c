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
#include "common.h"

typedef struct {
	const uint8_t *data;
	uint8_t *mark;
	ScoVer version;
	uint32_t hdrsize;
	uint32_t filesize;
	uint32_t page;
	const char *src_name;
	const char *sco_name;
	bool preprocessed;
} Sco;

// cali.c

struct Cali;
struct Cali *parse_cali(const uint8_t **code, bool is_lhs);
void print_cali(struct Cali *node, Vector *variables, FILE *out);

// decompile.c

void decompile(Vector *scos, const char *outdir);
noreturn void error_at(const uint8_t *pos, char *fmt, ...);
void warning_at(const uint8_t *pos, char *fmt, ...);
