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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdnoreturn.h>
#include <time.h>

#define VERSION "0.1.0"

// util.c

noreturn void error(char *fmt, ...);
char *sjis2utf(const char *str);
char *utf2sjis(const char *str);
uint8_t to_sjis_half_kana(uint8_t c1, uint8_t c2);

static inline bool is_sjis_half_kana(uint8_t c) {
	return 0xa1 <= c && c <= 0xdf;
}

static inline bool is_sjis_byte1(uint8_t c) {
	return (0x81 <= c && c <= 0x9f) || (0xe0 <= c && c <= 0xef);
}

static inline bool is_sjis_byte2(uint8_t c) {
	return 0x40 <= c && c <= 0xfc && c != 0x7f;
}

typedef struct {
	void **data;
	int len;
	int cap;
} Vector;

Vector *new_vec(void);
void vec_push(Vector *v, void *e);

typedef struct {
	Vector *keys;
	Vector *vals;
} Map;

Map *new_map(void);
void map_put(Map *m, char *key, void *val);
void *map_get(Map *m, char *key);

// sco.c

typedef enum {
	SYSTEM35,
	SYSTEM36,
	SYSTEM38,
} SysVer;
extern SysVer sys_ver;

typedef struct {
	uint8_t *buf;
	int len;
	int cap;
} Sco;

void sco_init(const char *src_name, int pageno);
Sco *sco_finalize(void);
void emit(uint8_t b);
void emit_word(uint16_t v);
void emit_word_be(uint16_t v);
void emit_dword(uint32_t v);
void emit_string(const char *s);
void set_byte(uint32_t addr, uint8_t val);
uint16_t swap_word(uint32_t addr, uint16_t val);
uint32_t swap_dword(uint32_t addr, uint32_t val);
int current_address(void);

// compile.c

typedef struct {
	bool resolved;
	uint16_t page;
	uint32_t addr;
	Vector *params;
} Function;

typedef struct {
	Vector *src_names;
	Vector *variables;
	Map *functions;
	Sco **scos;
} Compiler;

void compiler_init(Compiler *compiler, Vector *src_names);
void preprocess(Compiler *comp, const char *source, int pageno);
Sco *compile(Compiler *comp, const char *source, int pageno);

// ald.c

typedef struct {
	const char *name;
	time_t timestamp;
	const void *data;
	int size;
} AldEntry;

void ald_write(Vector *entries, FILE *fp);
