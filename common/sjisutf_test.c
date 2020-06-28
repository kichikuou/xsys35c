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
#include <stdio.h>
#include <stdlib.h>

static void test_compaction(void) {
	for (int c = 0; c < 256; c++) {
		if (is_compacted_sjis(c)) {
			uint16_t full = expand_sjis(c);
			if (!full) {
				printf("[FAIL] expand_sjis(0x%02x) unexpectedly returned zero\n", c);
				exit(1);
			}
			uint8_t half = compact_sjis(full >> 8, full & 0xff);
			if (c != half) {
				printf("[FAIL] compact_sjis(0x%04x): expected 0x%02x, got 0x%02x\n", full, c, half);
				exit(1);
			}
		} else {
			uint16_t full = expand_sjis(c);
			if (full) {
				printf("[FAIL] expand_sjis(0x%02x) unexpectedly returned non-zero value 0x%04x\n", c, full);
				exit(1);
			}
		}
	}
	for (int c1 = 0x81; c1 <= 0x82; c1++) {
		for (int c2 = 0x40; c2 <= 0xff; c2++) {
			uint8_t half = compact_sjis(c1, c2);
			if (!half)
				continue;
			uint16_t full = expand_sjis(half);
			if (full != (c1 << 8 | c2)) {
				printf("[FAIL] compact_sjis/expand_sjis: 0x%04x -> 0x%02x -> 0x%04x\n", c1 << 8 | c2, half, full);
			}
		}
	}
}

void sjisutf_test(void) {
	test_compaction();
}
