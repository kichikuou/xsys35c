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
#include <stdlib.h>
#include <string.h>

SysVer sys_ver = SYSTEM35;

static Sco out;

void emit(uint8_t b) {
	if (!out.buf)
		return;
	if (out.len == out.cap) {
		out.cap *= 2;
		out.buf = realloc(out.buf, out.cap);
	}
	out.buf[out.len++] = b;
}

void emit_word(uint16_t v) {
	if (!out.buf)
		return;
	emit(v & 0xff);
	emit(v >> 8 & 0xff);
}

void emit_word_be(uint16_t v) {
	if (!out.buf)
		return;
	emit(v >> 8 & 0xff);
	emit(v & 0xff);
}

void emit_dword(uint32_t v) {
	if (!out.buf)
		return;
	emit(v & 0xff);
	emit(v >> 8 & 0xff);
	emit(v >> 16 & 0xff);
	emit(v >> 24 & 0xff);
}

void emit_string(const char *s) {
	if (!out.buf)
		return;
	while (*s)
		emit(*s++);
}

int current_address(void) {
	if (!out.buf)
		return 0;
	return out.len;
}

void set_byte(uint32_t addr, uint8_t val) {
	if (!out.buf)
		return;
	out.buf[addr] = val;
}

uint16_t swap_word(uint32_t addr, uint16_t val) {
	if (!out.buf)
		return 0;
	uint8_t *b = &out.buf[addr];
	uint16_t oldval = b[0] | (b[1] << 8);
	b[0] = val & 0xff;
	b[1] = val >> 8 & 0xff;
	return oldval;
}

uint32_t swap_dword(uint32_t addr, uint32_t val) {
	if (!out.buf)
		return 0;
	uint8_t *b = &out.buf[addr];
	uint32_t oldval = b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
	b[0] = val & 0xff;
	b[1] = val >> 8 & 0xff;
	b[2] = val >> 16 & 0xff;
	b[3] = val >> 24 & 0xff;
	return oldval;
}

void sco_init(const char *src_name, int pageno) {
	out.buf = calloc(1, 4096);
	out.cap = 4096;
	out.len = 0;

	int namelen = strlen(src_name);
	if (namelen >= 1024)
		error("file name too long: %s", src_name);
	int hdrsize = (18 + namelen + 15) & ~0xf;

	// SCO header
	switch (sys_ver) {
	case SYSTEM35:
		emit_string("S351");
		break;
	case SYSTEM36:
		emit_string("S360");
		break;
	case SYSTEM38:
		emit_string("S380");
		break;
	}
	emit_dword(hdrsize);
	emit_dword(0);  // File size (to be filled by sco_finalize)
	emit_dword(pageno);
	emit_word(namelen);
	emit_string(src_name);
	while (out.len < hdrsize)
		emit(0);
}

Sco *sco_finalize(void) {
	swap_dword(8, out.len);
	Sco *sco = calloc(1, sizeof(Sco));
	*sco = out;
	memset(&out, 0, sizeof(Sco));
	return sco;
}
