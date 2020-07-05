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
#include "xsys35dc.h"
#include <string.h>

// These label names are hard-coded in NIGHTDLL.DLL and used to refer data blocks.
static const char *night_data_labels[] = {
	"MonsterData",
	"イベントダンジョンデータ",
	"イベント戦闘データ",
	"スキルデータ",
	"仲間データ",
	"武器コメント",
	"武器データ",
	"装備アイテムデータ",
	"訓練ダンジョンデータ",
	"訓練ダンジョンデータ",
	"ＢＧＭデータ",
	"特訓誘いいずみ",
	"特訓誘い鏡花",
	"特訓誘い真言美",
	"特訓誘いマコト",
	"特訓誘い新開",
	"特訓誘い星川",
	"特訓誘い百瀬",
	"ダンジョン初いずみ",
	"ダンジョン初鏡花",
	"ダンジョン初新開",
	"ダンジョン初星川",
	"特訓後いずみ04月",
	"特訓後いずみ05月",
	"特訓後いずみ06月",
	"特訓後いずみ07月",
	"特訓後いずみ08月",
	"特訓後いずみ09月",
	"特訓後いずみ10月",
	"特訓後いずみ11月",
	"特訓後いずみ惚れ",
	"特訓後鏡花04月",
	"特訓後鏡花05月",
	"特訓後鏡花06月",
	"特訓後鏡花07月",
	"特訓後鏡花08月",
	"特訓後鏡花09月",
	"特訓後鏡花10月",
	"特訓後鏡花11月",
	"特訓後鏡花惚れ",
	"特訓後真言美04月",
	"特訓後真言美05月",
	"特訓後真言美06月",
	"特訓後真言美07月",
	"特訓後真言美08月",
	"特訓後真言美09月",
	"特訓後真言美10月",
	"特訓後真言美11月",
	"特訓後真言美惚れ",
	"特訓後マコト04月",
	"特訓後マコト05月",
	"特訓後マコト06月",
	"特訓後マコト07月",
	"特訓後マコト08月",
	"特訓後マコト09月",
	"特訓後マコト10月",
	"特訓後マコト11月",
	"特訓後マコト惚れ",
	"特訓後新開04月",
	"特訓後新開05月",
	"特訓後新開06月",
	"特訓後新開07月",
	"特訓後新開08月",
	"特訓後新開09月",
	"特訓後新開10月",
	"特訓後新開11月",
	"特訓後星川04月",
	"特訓後星川05月",
	"特訓後星川06月",
	"特訓後星川07月",
	"特訓後星川08月",
	"特訓後星川09月",
	"特訓後星川10月",
	"特訓後星川11月",
	"特訓後百瀬04月",
	"特訓後百瀬05月",
	"特訓後百瀬06月",
	"特訓後百瀬07月",
	"特訓後百瀬08月",
	"特訓後百瀬09月",
	"特訓後百瀬10月",
	"特訓後百瀬11月",
	"ダンジョン内いずみ／鏡花",
	"ダンジョン内いずみ／真言美",
	"ダンジョン内いずみ／マコト",
	"ダンジョン内いずみ／新開",
	"ダンジョン内いずみ／星川",
	"ダンジョン内いずみ／百瀬",
	"ダンジョン内鏡花／真言美",
	"ダンジョン内鏡花／マコト",
	"ダンジョン内鏡花／新開",
	"ダンジョン内鏡花／星川",
	"ダンジョン内鏡花／百瀬",
	"ダンジョン内真言美／マコト",
	"ダンジョン内真言美／新開",
	"ダンジョン内真言美／星川",
	"ダンジョン内真言美／百瀬",
	"ダンジョン内マコト／新開",
	"ダンジョン内マコト／星川",
	"ダンジョン内マコト／百瀬",
	"ダンジョン内新開／星川",
	"ダンジョン内新開／百瀬",
	"ダンジョン内星川／百瀬",
	"ダンジョン内いずみ",
	"ダンジョン内鏡花",
	"ダンジョン内真言美",
	"ダンジョン内マコト",
	"ダンジョン内新開",
	"ダンジョン内星川",
	"ダンジョン内百瀬",
	NULL
};

static void mark_functions_from_ain(Vector *scos, Ain *ain) {
	HashMap *data_labels = new_string_hash();
	if (ain->dlls && map_get(ain->dlls, "NIGHTDLL")) {
		for (const char **s = night_data_labels; *s; s++)
			hash_put(data_labels, utf2sjis(*s), *s);
	}

	for (HashItem *i = hash_iterate(ain->functions, NULL); i; i = hash_iterate(ain->functions, i)) {
		Function *f = (Function *)i->val;
		unsigned page = f->page - 1;
		Sco *sco = page < scos->len ? scos->data[page] : NULL;
		if (sco && f->addr <= sco->filesize) {
			sco->mark[f->addr] |= FUNC_TOP;
			if (hash_get(data_labels, f->name))
				sco->mark[f->addr] |= DATA;
		}
	}
}

// Scan the SCO and annotate locations that look like data blocks.
static void scan_for_data_tables(Sco *sco, Vector *scos, Ain *ain) {
	const uint8_t *p = sco->data + sco->hdrsize;
	const uint8_t *end = sco->data + sco->filesize - 6;  // -6 for address and cali

	// Scan for the pattern '#' <32-bit address> <cali>
	while (p < end && (p = memchr(p, '#', end - p)) != NULL) {
		uint32_t ptr_addr = le32(++p);
		if (p[5] != 0x7f) // Only check if it is a simple 2-byte cali
			continue;
		if (ptr_addr < sco->hdrsize || ptr_addr > sco->filesize - 4)
			continue;
		// Mark only backward references heuristically. Forward references
		// will be marked in the analyze phase.
		if (ptr_addr < p - sco->data)
			sco->mark[ptr_addr] |= DATA_TABLE;

		uint32_t data_addr = le32(sco->data + ptr_addr);
		if (data_addr >= sco->hdrsize && data_addr < sco->filesize) {
			sco->mark[data_addr] |= DATA;
		}
	}

	// Scan for dataSetPointer (0x2f 0x80) command
	if (!ain)
		return;  // dataSetPointer is only in system 3.9
	p = sco->data + sco->hdrsize;
	end = sco->data + sco->filesize - 7;
	while (p < end && (p = memchr(p, 0x2f, end - p)) != NULL) {
		if (*++p != 0x80)
			continue;
		p++;
		uint16_t page = (p[0] | p[1] << 8) - 1;
		uint32_t addr = le32(p + 2);
		if (page >= scos->len)
			continue;
		Sco *sco = scos->data[page];
		if (addr >= sco->filesize)
			continue;
		// Must be adready marked using ain->functions
		if (!(sco->mark[addr] & FUNC_TOP))
			continue;
		sco->mark[addr] |= DATA;
	}
}

void preprocess(Vector *scos, Ain *ain) {
	if (ain && ain->functions)
		mark_functions_from_ain(scos, ain);

	for (int i = 0; i < scos->len; i++) {
		Sco *sco = scos->data[i];
		if (sco)
			scan_for_data_tables(sco, scos, ain);
	}
}
