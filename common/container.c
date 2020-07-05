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
#include <stdlib.h>
#include <string.h>

#define HASH_INIT_SIZE 16

Vector *new_vec(void) {
	Vector *v = malloc(sizeof(Vector));
	v->data = malloc(sizeof(void *) * 16);
	v->cap = 16;
	v->len = 0;
	return v;
}

void vec_push(Vector *v, void *e) {
	if (v->len == v->cap) {
		v->cap *= 2;
		v->data = realloc(v->data, sizeof(void *) * v->cap);
	}
	v->data[v->len++] = e;
}

void vec_set(Vector *v, int index, void *e) {
	while (v->len <= index)
		vec_push(v, NULL);
	v->data[index] = e;
}

void stack_push(Vector *stack, uintptr_t n) {
	vec_push(stack, (void *)n);
}

void stack_pop(Vector *stack) {
	if (stack->len == 0)
		error("stack underflow");
	stack->len--;
}

uintptr_t stack_top(Vector *stack) {
	if (stack->len == 0)
		error("stack underflow");
	return (uintptr_t)stack->data[stack->len - 1];
}

Map *new_map(void) {
	Map *m = malloc(sizeof(Map));
	m->keys = new_vec();
	m->vals = new_vec();
	return m;
}

void map_put(Map *m, const char *key, void *val) {
	vec_push(m->keys, (void *)key);
	vec_push(m->vals, val);
}

void *map_get(Map *m, const char *key) {
	for (int i = m->keys->len - 1; i >= 0; i--)
		if (!strcmp(m->keys->data[i], key))
			return m->vals->data[i];
	return NULL;
}

HashMap *new_hash(HashFunc hash, HashKeyCompare compare) {
	HashMap *m = calloc(1, sizeof(HashMap));
	m->size = HASH_INIT_SIZE;
	m->table = calloc(m->size, sizeof(HashItem));
	m->hash = hash;
	m->compare = compare;
	return m;
}

static uint32_t string_hash(const char *p) {
	// FNV hash
	uint32_t r = 2166136261;
	for (; *p; p++) {
		r ^= *p;
		r *= 16777619;
	}
	return r;
}

HashMap *new_string_hash(void) {
	return new_hash((HashFunc)string_hash, (HashKeyCompare)strcmp);
}

static void maybe_rehash(HashMap *m) {
	if (m->occupied * 4 < m->size * 3)
		return;
	HashMap old = *m;
	m->size *= 2;
	m->table = calloc(m->size, sizeof(HashItem));
	m->occupied = 0;
	for (uint32_t i = 0; i < old.size; i++) {
		if (old.table[i].key)
			hash_put(m, old.table[i].key, old.table[i].val);
	}
	free(old.table);
}

void hash_put(HashMap *m, const void *key, const void *val) {
	maybe_rehash(m);
	uint32_t h = m->hash(key) & (m->size - 1);
	while (m->table[h].key) {
		if (!m->compare(key, m->table[h].key)) {
			m->table[h].val = (void *)val;
			return;
		}
		if (++h == m->size)
			h = 0;
	}
	m->table[h].key = key;
	m->table[h].val = (void *)val;
	m->occupied++;
}

void *hash_get(HashMap *m, const void *key) {
	uint32_t h = m->hash(key) & (m->size - 1);
	while (m->table[h].key) {
		if (!m->compare(key, m->table[h].key))
			return m->table[h].val;
		if (++h == m->size)
			h = 0;
	}
	return NULL;
}

HashItem *hash_iterate(HashMap *m, HashItem *item) {
	for (item = item ? item + 1 : m->table; item < m->table + m->size; item++) {
		if (item->key)
			return item;
	}
	return NULL;
}
