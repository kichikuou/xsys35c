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
	const char *sco_name;  // in SJIS
	int ald_volume;
	bool analyzed;  // if false, decompile() will (re-)analyze this page
} Sco;

// Sco.mark[i] stores annotation for Sco.data[i], collected during the
// analysis phase (see the comment at the top of decompile.c).
// An annotation consists of a 3-bit type field and flags.
enum {
	  // Type field: identifies the start of a reconstructed control
	  // structure, on the first byte of the instruction noted below.
	  WHILE_START = 1,  // on '{': conditional jump that is a while-loop head
	  FOR_START,        // on '!': variable assignment that starts a for-loop
	  ELSE,             // on '@': jump over an else-block
	  ELSE_IF,          // on '@': jump over an else-if block
	  FUNCALL_TOP,      // on '!': first argument-passing assignment of a
	                    //     function call (see analyze_args())
	  DATA_TABLE,       // on an array of addresses referenced by '#'
	  TYPE_MASK   = 0x7,

	  // Flags
	  CODE        = 1 << 4,  // executed as an instruction
	  DATA        = 1 << 5,  // referenced as data; decompiled in [...] form
	  LABEL       = 1 << 6,  // jump target; gets an "*L_xxxxx:" label
	  FUNC_TOP    = 1 << 7,  // function entry point; gets a "**name:" label
};

// ain.c

typedef struct {
	const char *name;
	Vector *aliases; // Used if a single address has multiple global labels
	uint16_t page;  // one-based numbering
	uint32_t addr;
	int argc;  // -1 for unknown
	uint16_t *argv;
} Function;

typedef enum {
	MAGIC_AINI,
	MAGIC_AIN2,
} AinMagic;

typedef struct {
	const char *filename;
	AinMagic magic;
	uint32_t version;
	Map *dlls;           // dllname -> Vector<DLLFunc>
	HashMap *functions;  // Function -> Function (itself)
	Vector *variables;
	Vector *messages;
} Ain;

Ain *ain_read(const char *path);
void ain_dump(Ain *ain);
void write_hels(Map *dlls, const char *dir);
HashMap *new_function_hash(void);

// debuginfo.c

typedef struct {
	Map *srcs;
	Vector *variables;
} DebugInfo;

DebugInfo *debug_info_read(const char *path);

// cali.c

typedef struct Cali {
	enum {
		NODE_NUMBER,
		NODE_VARIABLE,
		NODE_OP,
		NODE_AREF,
	} type;
	int val;
	struct Cali *lhs, *rhs;
} Cali;

// The returned node is valid until next parse_cali() call.
Cali *parse_cali(const uint8_t **code, bool is_lhs);
void print_cali(Cali *node, Vector *variables, FILE *out);

// preprocess.c

void preprocess(Vector *scos, Ain *ain);

// decompile.c

typedef struct {
	bool address;
	bool utf8_input;
	bool utf8_output;
	bool verbose;
} Config;

extern Config config;

void decompile(Vector *scos, Ain *ain, DebugInfo *debug_info, const char *outdir, const char *ald_basename);
noreturn void error_at(const uint8_t *pos, char *fmt, ...);
void warning_at(const uint8_t *pos, char *fmt, ...);

// xsys35dc.c
void convert_to_utf8(FILE *fp);
const char *to_utf8(const char *s);
