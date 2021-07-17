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

// config.c

typedef enum {
	SYSTEM35,
	SYSTEM36,
	SYSTEM38,
	SYSTEM39,
} SysVer;

typedef struct {
	const char *ald_basename;
	const char *output_ain;
	uint32_t ain_version;

	SysVer sys_ver;
	ScoVer sco_ver;
	const char *hed;
	const char *var_list;

	bool debug;
	bool unicode;
	bool utf8;
	bool disable_else;
	bool disable_ain_message;
	bool disable_ain_variable;
	bool old_SR;
} Config;
extern Config config;

void set_sys_ver(const char *ver);
void load_config(FILE *fp, const char *cfg_dir);
static inline bool use_ain_message(void) {
	return config.sys_ver == SYSTEM39 && !config.disable_ain_message;
}

static inline const char *to_output_encoding(const char *str_utf8) {
	return config.unicode ? str_utf8 : utf2sjis_sub(str_utf8, '?');
}

// sco.c

typedef struct {
	uint8_t *buf;
	int len;
	int cap;
} Buffer;

Buffer *new_buf(void);
void emit(Buffer *b, uint8_t c);
void emit_word(Buffer *b, uint16_t v);
void emit_word_be(Buffer *b, uint16_t v);
void emit_dword(Buffer *b, uint32_t v);
void emit_string(Buffer *b, const char *s);
void set_byte(Buffer *b, uint32_t addr, uint8_t val);
uint8_t get_byte(Buffer *b, uint32_t addr);
uint16_t swap_word(Buffer *b, uint32_t addr, uint16_t val);
uint32_t swap_dword(Buffer *b, uint32_t addr, uint32_t val);
void emit_var(Buffer *b, int var_id);
void emit_number(Buffer *b, int n);
void emit_command(Buffer *b, int cmd);
int current_address(Buffer *b);
void sco_init(Buffer *b, const char *src_name_utf8, int pageno);
void sco_finalize(Buffer *b);

// lexer.c

extern const char *input_name;
extern int input_page;
extern const char *input_buf;
extern const char *input;
extern int input_line;

noreturn void error_at(const char *pos, char *fmt, ...);
void lexer_init(const char *source, const char *name, int pageno);
void skip_whitespaces(void);
char next_char(void);
bool consume(char c);
void expect(char c);
bool consume_keyword(const char *keyword);
uint8_t echo(Buffer *b);
char *get_identifier(void);
char *get_label(void);
char *get_filename(void);
int get_number(void);
void compile_string(Buffer *b, char terminator, bool compact, bool forbid_ascii);
void compile_message(Buffer *b);
void compile_bare_string(Buffer *b);
int get_command(Buffer *b);

// compile.c

typedef struct {
	const char *name;
	bool resolved;
	uint16_t page;
	uint32_t addr;
	Vector *params;
} Function;

typedef struct {
	Buffer *buf;
	int ald_volume;
} Sco;

struct DebugInfo;

typedef struct {
	Vector *src_names;
	Vector *variables;
	HashMap *symbols;   // variables and constants
	HashMap *functions;
	Map *dlls;
	Buffer *msg_buf;
	int msg_count;
	Sco *scos;
	struct DebugInfo *dbg_info;
} Compiler;

Compiler *new_compiler(Vector *src_names, Vector *variables, Map *dlls);
void preprocess(Compiler *comp, const char *source, int pageno);
void preprocess_done(Compiler *comp);
Sco *compile(Compiler *comp, const char *source, int pageno);

// ain.c

void ain_write(Compiler *compiler, FILE *fp);

// hel.c

Vector *parse_hel(const char* hel, const char *name);

// debuginfo.c

struct DebugInfo *new_debug_info(Map *srcs);
void debug_init_page(struct DebugInfo *di, int page);
void debug_line_add(struct DebugInfo *di, int line, int addr);
void debug_finish_page(struct DebugInfo *di);
void debug_info_write(struct DebugInfo *di, Compiler *compiler, FILE *fp);
