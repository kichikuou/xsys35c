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
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum {
	  LABEL = 0x80,
};

#define CMD2(a, b) (a | b << 8)

typedef struct {
	Vector *scos;
	FILE *out;

	int page;
	const uint8_t *p;  // Points inside scos->data[page]->data
} Decompiler;

static Decompiler dc;

static inline int dc_addr(void) {
	return dc.p - ((Sco *)dc.scos->data[dc.page])->data;
}

static void dc_putc(int c) {
	if (dc.out)
		fputc(c, dc.out);
}

static void dc_puts(const char *s) {
	if (dc.out)
		fputs(s, dc.out);
}

static void dc_printf(const char *fmt, ...) {
	if (!dc.out)
		return;
	va_list args;
	va_start(args, fmt);
	vfprintf(dc.out, fmt, args);
}

static int subcommand_num(void) {
	int c = *dc.p++;
	dc_printf("%d", c);
	return c;
}

static void label(void) {
	uint32_t addr = le32(dc.p);
	dc.p += 4;
	if (!dc.out) {  // first pass
		Sco *sco = dc.scos->data[dc.page];
		if (addr >= sco->filesize)
			error("address out of range (%x)", addr);
		sco->mark[addr] |= LABEL;
	} else {
		dc_printf("L_%05x", addr);
	}
}

static void arguments(const char *sig) {
	const char *sep = " ";
	for (; *sig; sig++) {
		dc_puts(sep);
		sep = ",";

		switch (*sig) {
		case 'e':
			dc.p += cali(dc.p, false, NULL, dc.out);
			break;
		case 'n':
			dc_printf("%d", *dc.p++);
			break;
		case 's':
			while (*dc.p != ':')
				dc_putc(*dc.p++);
			dc.p++;  // skip ':'
			break;
		default:
			error("BUG: invalid arguments() template : %c", *sig);
		}
	}
	dc_putc(':');
}

static void message(void) {
	while (*dc.p == 0x20 || *dc.p > 0x80) {
		uint8_t c = *dc.p++;
		if (c == ' ') {
			dc_puts("\x81\x40"); // full-width space
		} else if (is_sjis_half_kana(c)) {
			uint16_t full = from_sjis_half_kana(c);
			dc_putc(full >> 8);
			dc_putc(full & 0xff);
		} else {
			dc_putc(c);
			if (is_sjis_byte1(c))
				dc_putc(*dc.p++);
		}
	}
}

static int get_command(void) {
	switch (*dc.p) {
	case 'L':
	case 'W':
	case 'Z':
		dc_putc(*dc.p++);
		dc_putc(*dc.p++);
		return CMD2(dc.p[-2], dc.p[-1]);
	default:
		dc_putc(*dc.p++);
		return dc.p[-1];
	}
}

static void decompile_page(int page) {
	Sco *sco = dc.scos->data[page];
	dc.page = page;
	dc.p = sco->data + sco->hdrsize;

	while (dc.p < sco->data + sco->filesize) {
		if (sco->mark[dc.p - sco->data] & LABEL)
			dc_printf("*L_%05x:\n", dc.p - sco->data);
		dc_putc('\t');
		if (*dc.p == 0x20 || *dc.p > 0x80) {
			dc_putc('\'');
			message();
			dc_puts("'\n");
			continue;
		}
		int cmd = get_command();
		switch (cmd) {
		case '!':
			dc.p += cali(dc.p, true, NULL, dc.out);
			dc_putc(':');
			dc.p += cali(dc.p, false, NULL, dc.out);
			dc_putc('!');
			break;

		case '@':  // Label jump
			label();
			dc_putc(':');
			break;

		case '&':  // Page jump
			dc.p += cali(dc.p, false, NULL, dc.out);
			dc_putc(':');
			break;

		case ']':  // Menu
			break;

		case '$':  // Menu item
			label();
			dc_putc('$');
			if (*dc.p == 0x20 || *dc.p > 0x80) {
				message();
				dc_putc('$');
				if (*dc.p++ == '$')
					break;
			}
			error("%s:%x: Complex $ not implemented", sjis2utf(sco->sco_name), dc_addr());

		case 'A': break;
		case 'B':
			switch (subcommand_num()) {
			case 0:
				arguments("e"); break;
			case 1:
			case 2:
			case 3:
			case 4:
				arguments("eeeeee"); break;
			case 10:
			case 11:
				arguments("vv"); break;
			case 12:
			case 13:
			case 14:
				arguments("v"); break;
			case 21:
			case 22:
			case 23:
			case 24:
			case 31:
			case 32:
			case 33:
			case 34:
				arguments("evv"); break;
			default:
				goto unknown_command;
			}
			break;
		case CMD2('L', 'C'): arguments("ees"); break;
		case 'R': break;
		case CMD2('W', 'W'): arguments("eee"); break;
		case CMD2('W', 'V'): arguments("eeee"); break;
		case CMD2('Z', 'A'): arguments("ne"); break;
		case CMD2('Z', 'B'): arguments("e"); break;
		case CMD2('Z', 'C'): arguments("ee"); break;
		case CMD2('Z', 'D'): arguments("ne"); break;
		case CMD2('Z', 'E'): arguments("e"); break;
		case CMD2('Z', 'F'): arguments("e"); break;
		case CMD2('Z', 'G'): arguments("v"); break;
		case CMD2('Z', 'H'): arguments("e"); break;
		case CMD2('Z', 'I'): arguments("ee"); break;
		case CMD2('Z', 'K'): arguments("ees"); break;
		case CMD2('Z', 'L'): arguments("e"); break;
		case CMD2('Z', 'M'): arguments("e"); break;
		case CMD2('Z', 'R'): arguments("ev"); break;
		case CMD2('Z', 'S'): arguments("e"); break;
		case CMD2('Z', 'T'):
			switch (subcommand_num()) {
			case 2:
			case 3:
			case 4:
			case 5:
				arguments("v"); break;
			case 0:
			case 1:
			case 20:
			case 21:
				arguments("e"); break;
			case 10:
				arguments("eee"); break;
			case 11:
				arguments("ev"); break;
			default:
				goto unknown_command;
			}
			break;
		case CMD2('Z', 'W'): arguments("e"); break;
		case CMD2('Z', 'Z'): arguments("ne"); break;
		default:
		unknown_command:
			error("%s:%x: unknown command '%x'", sjis2utf(sco->sco_name), dc_addr(), cmd);
		}
		dc_putc('\n');
	}
}

static void write_hed(const char *path) {
	FILE *fp = fopen(path, "w");
	fprintf(fp, "#SYSTEM35\n");
	for (int i = 0; i < dc.scos->len; i++) {
		Sco *sco = dc.scos->data[i];
		fprintf(fp, "%s\n", sco->src_name);
	}
	fclose(fp);
}

void decompile(Vector *scos) {
	memset(&dc, 0, sizeof(dc));
	dc.scos = scos;

	for (int i = 0; i < scos->len; i++)
		decompile_page(i);

	dc.out = stdout;
	for (int i = 0; i < scos->len; i++)
		decompile_page(i);

	write_hed("xsys35dc.hed");
}
