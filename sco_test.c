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
#include "xsys35c.h"
#include <assert.h>
#include <string.h>

#define TEST(name, source, expected) \
	test(name, source, expected, sizeof(expected) - 1)

static void hexdump(const uint8_t* data, int len, int pos) {
	for (int i = 0; i < len; i++) {
		if (i % 16 == 0)
			printf("\n%04x ", i);
		if (i % 16 == 8)
			putchar(' ');
		printf("%c%02x", i == pos ? '>' : ' ', data[i]);
	}
}

int main() {
	const char expected[] =
		"\x53\x33\x35\x31\x20\x00\x00\x00\x23\x00\x00\x00\x2a\x00\x00\x00"
		"\x08\x00\x54\x45\x53\x54\x2e\x41\x44\x56\x00\x00\x00\x00\x00\x00"
		"\x41\x42\x43";
	const int expected_len = sizeof(expected) - 1;

	sco_init("TEST.ADV", 42);
	assert(current_address() == 32);
	emit_string("ABC");
	assert(current_address() == 35);
	Sco *sco = sco_finalize();

	if (sco->len != expected_len ||
		memcmp(sco->buf, expected, expected_len) != 0) {
		int pos;
		for (pos = 0; pos < expected_len && pos < sco->len; pos++) {
			if (sco->buf[pos] != (uint8_t)expected[pos])
				break;
		}
		printf("header_test failed. expected:");
		hexdump((const uint8_t *)expected, expected_len, pos);
		printf("\ngot:");
		hexdump(sco->buf, sco->len, pos);
		printf("\n");
	}
}
