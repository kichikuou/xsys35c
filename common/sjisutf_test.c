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
		if (is_compacted_kana(c)) {
			uint16_t full = from_sjis_half_kana(c);
			if (!full) {
				printf("[FAIL] from_sjis_half_kana(0x%02x) unexpectedly returned zero\n", c);
				exit(1);
			}
			uint8_t half = to_sjis_half_kana(full >> 8, full & 0xff);
			if (c != half) {
				printf("[FAIL] to_sjis_half_kana(0x%04x): expected 0x%02x, got 0x%02x\n", full, c, half);
				exit(1);
			}
		} else {
			uint16_t full = from_sjis_half_kana(c);
			if (full) {
				printf("[FAIL] from_sjis_half_kana(0x%02x) unexpectedly returned non-zero value 0x%04x\n", c, full);
				exit(1);
			}
		}
	}
}

int main() {
	test_compaction();
	return 0;
}
