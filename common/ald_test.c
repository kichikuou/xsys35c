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
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#undef NDEBUG

#define TIMESTAMP 850953600  // 1996-12-19 00:00:00 UTC

static void test_read(void) {
	Vector *es = ald_read(NULL, "testdata/expected.ald");
	assert(es->len == 2);
	AldEntry *e1 = es->data[0];
	AldEntry *e2 = es->data[1];

	assert(!strcmp(e1->name, "a.txt"));
	assert(e1->timestamp == TIMESTAMP);
	assert(e1->size == 7);
	assert(!memcmp(e1->data, "content", 7));

	assert(!strcmp(e2->name, "very_long_file_name.txt"));
	assert(e2->timestamp == TIMESTAMP);
	assert(e2->size == 2);
	assert(!memcmp(e2->data, "ok", 2));
}

static void test_write(void) {
	AldEntry e1 = {
		.disk = 1,
		.name = "a.txt",
		.timestamp = TIMESTAMP,
		.data = (const uint8_t *)"content",
		.size = 7,
	};
	AldEntry e2 = {
		.disk = 1,
		.name = "very_long_file_name.txt",
		.timestamp = TIMESTAMP,
		.data = (const uint8_t *)"ok",
		.size = 2,
	};
	Vector *es = new_vec();
	vec_push(es, &e1);
	vec_push(es, &e2);
	const char outfile[] = "testdata/actual.ald";
	FILE *fp = fopen(outfile, "wb");
	if (!fp)
		error("%s: %s", outfile, strerror(errno));
	ald_write(es, 1, fp);
	fclose(fp);
}

static void test_multidisk_read(void) {
	Vector *es = new_vec();
	ald_read(es, "testdata/expected_a.ald");
	ald_read(es, "testdata/expected_b.ald");
	assert(es->len == 5);
	for (int i = 0; i < 5; i++) {
		char expected[20];
		sprintf(expected, "%d.txt", i);

		AldEntry *e = es->data[i];
		assert(!strcmp(e->name, expected));
		assert(e->timestamp == TIMESTAMP);
		assert(e->size == strlen(expected));
		assert(!strcmp((const char *)e->data, expected));
	}
}

static void test_multidisk_write(void) {
	Vector *es = new_vec();
	for (int i = 0; i < 5; i++) {
		char buf[20];
		sprintf(buf, "%d.txt", i);
		AldEntry *e = calloc(1, sizeof(AldEntry));
		e->disk = i % 2 + 1;
		e->name = strdup(buf);
		e->timestamp = TIMESTAMP;
		e->data = (const uint8_t *)e->name;
		e->size = strlen(e->name);
		vec_push(es, e);
	}
	const char *aldname[] = {
		"testdata/actual_a.ald",
		"testdata/actual_b.ald",
	};
	for (int disk = 0; disk < 2; disk++) {
		FILE *fp = fopen(aldname[disk], "wb");
		if (!fp)
			error("%s: %s", aldname[disk], strerror(errno));
		ald_write(es, disk + 1, fp);
		fclose(fp);
	}
}

int main() {
	test_read();
	test_write();
	test_multidisk_read();
	test_multidisk_write();
	return 0;
}
