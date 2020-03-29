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
#include <string.h>
#undef NDEBUG

#define TIMESTAMP 850953600  // 1996-12-19 00:00:00 UTC

static void test_read(void) {
	Vector *es = ald_read("testdata/expected.ald");
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
		.name = "a.txt",
		.timestamp = TIMESTAMP,
		.data = "content",
		.size = 7,
	};
	AldEntry e2 = {
		.name = "very_long_file_name.txt",
		.timestamp = TIMESTAMP,
		.data = "ok",
		.size = 2,
	};
	Vector *es = new_vec();
	vec_push(es, &e1);
	vec_push(es, &e2);
	const char outfile[] = "testdata/actual.ald";
	FILE *fp = fopen(outfile, "wb");
	if (!fp)
		error("%s: %s", outfile, strerror(errno));
	ald_write(es, fp);
	fclose(fp);
}

int main() {
	test_read();
	test_write();
	return 0;
}
