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

static inline uint32_t le32(const uint8_t *p) {
	return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

typedef enum {
	SCO_S350,
	SCO_153S,
	SCO_S351,
	SCO_S360,
	SCO_S380
} ScoVer;

// util.c

noreturn void error(char *fmt, ...);
char *sjis2utf(const char *str);
char *utf2sjis(const char *str);
uint8_t to_sjis_half_kana(uint8_t c1, uint8_t c2);
uint16_t from_sjis_half_kana(uint8_t c);

static inline bool is_sjis_half_kana(uint8_t c) {
	return 0xa1 <= c && c <= 0xdf;
}

static inline bool is_sjis_byte1(uint8_t c) {
	return (0x81 <= c && c <= 0x9f) || (0xe0 <= c && c <= 0xef);
}

static inline bool is_sjis_byte2(uint8_t c) {
	return 0x40 <= c && c <= 0xfc && c != 0x7f;
}

#define PATH_SEPARATOR '/'
const char *basename(const char *path);
char *dirname(const char *path);
char *path_join(const char *dir, char *path);

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

// ald.c

typedef struct {
	const char *name;
	time_t timestamp;
	const void *data;
	int size;
} AldEntry;

void ald_write(Vector *entries, FILE *fp);
Vector *ald_read(const char *path);

// opcodes

enum {
	OP_AND = 0x74,
	OP_OR,
	OP_XOR,
	OP_MUL,
	OP_DIV,
	OP_ADD,
	OP_SUB,
	OP_EQ,
	OP_LT,
	OP_GT,
	OP_NE,
	OP_END, // End of expression
};
enum {
	OP_C0_INDEX = 1,
	OP_C0_MOD,
	OP_C0_LE,
	OP_C0_GE,
};
