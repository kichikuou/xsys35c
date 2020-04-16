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

// ain.c

typedef enum {
	Arg_pword = 0,
	Arg_int = 1,
	Arg_ISurface = 2,
	Arg_IString = 3,
	Arg_IWinMsg = 4,
	Arg_ITimer = 5,
	Arg_IUI = 6,
	Arg_ISys3xDIB = 7,
	Arg_ISys3xCG = 9,
	Arg_ISys3xStringTable = 10,
	Arg_ISys3xSystem = 13,
	Arg_ISys3xMusic = 14,
	Arg_ISys3xMsgString = 15,
	Arg_ISys3xInputDevice = 16,
	Arg_ISys3x = 17,
	Arg_IConstString = 18,
} DllArgType;

typedef struct {
	const char *name;
	uint32_t argc;
	DllArgType argtypes[];
} DLLFunc;

typedef struct {
	uint16_t page;
	uint32_t addr;
} Function;

typedef struct {
	Map *dlls;         // dllname -> Vector<DLLFunc>
	Map *functions;    // funcname -> Function
	Vector *variables;
	Vector *messages;
} Ain;

Ain *ain_read(const char *path);
void write_hels(Map *dlls, const char *dir);

// cali.c

struct Cali;
struct Cali *parse_cali(const uint8_t **code, bool is_lhs);
void print_cali(struct Cali *node, Vector *variables, FILE *out);

// decompile.c

void decompile(Vector *scos, Ain *ain, const char *outdir);
noreturn void error_at(const uint8_t *pos, char *fmt, ...);
void warning_at(const uint8_t *pos, char *fmt, ...);
