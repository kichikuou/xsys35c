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
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum {
	  CODE        = 1 << 0,
	  DATA        = 1 << 1,
	  LABEL       = 1 << 2,
	  FUNC_TOP    = 1 << 3,
	  IF_END      = 1 << 4,
	  WHILE_START = 1 << 5,
	  FOR_START   = 1 << 6,
};

#define CMD2(a, b) (a | b << 8)
#define CMD3(a, b, c) (a | b << 8 | c << 16)

typedef struct {
	Vector *scos;
	FILE *out;

	int page;
	const uint8_t *p;  // Points inside scos->data[page]->data
	int indent;
} Decompiler;

static Decompiler dc;

static inline int dc_addr(void) {
	return dc.p - ((Sco *)dc.scos->data[dc.page])->data;
}

static uint8_t *mark_at(int page, int addr) {
	if (page >= dc.scos->len)
		error("page out of range (%x:%x)", page, addr);
	Sco *sco = dc.scos->data[page];
	if (addr >= sco->filesize)
		error("address out of range (%x:%x)", addr, addr);
	return &sco->mark[addr];
}

static void indent(void) {
	if (!dc.out)
		return;
	for (int i = 0; i < dc.indent; i++)
		fputc('\t', dc.out);
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
	if (addr == 0)
		dc_putc('0');
	else
		dc_printf("L_%05x", addr);

	*mark_at(dc.page, addr) |= LABEL;
}

static void data_table(void) {
	uint32_t addr = le32(dc.p);
	dc.p += 4;
	dc_printf("L_%05x", addr);
	dc_putc(',');
	dc.p += cali(dc.p, false, NULL, dc.out);
	dc_putc(':');

	*mark_at(dc.page, addr) |= DATA | LABEL;
}

static void conditional(void) {
	dc.indent++;
	dc.p += cali(dc.p, false, NULL, dc.out);
	dc_putc(':');
	uint32_t endaddr = le32(dc.p);
	dc.p += 4;

	*mark_at(dc.page, endaddr) |= IF_END;
}

static void funcall(void) {
	uint16_t page = dc.p[0] | dc.p[1] << 8;
	dc.p += 2;
	switch (page) {
	case 0:  // return
		dc_puts("0,");
		dc.p += cali(dc.p, false, NULL, dc.out);
		break;
	case 0xffff:
		dc_putc('~');
		dc.p += cali(dc.p, false, NULL, dc.out);
		break;
	default:
		{
			page -= 1;
			uint32_t addr = le32(dc.p);
			dc.p += 4;
			dc_printf("F_%d_%05x", page, addr);

			*mark_at(page, addr) |= FUNC_TOP;
			break;
		}
	}
	dc_putc(':');
}

static void for_loop(void) {
	uint8_t *mark = mark_at(dc.page, dc_addr()) - 2;
	while (!(*mark & CODE))
		mark--;
	*mark |= FOR_START;
	if (*dc.p++ != 0)
		error("for_loop: 0 expected, got 0x%02x", *--dc.p);
	if (*dc.p++ != '<')
		error("for_loop: '<' expected, got 0x%02x", *--dc.p);
	if (*dc.p++ != 1)
		error("for_loop: 1 expected, got 0x%02x", *--dc.p);
	dc.p += 4; // skip label
	dc.p += cali(dc.p, false, NULL, NULL);  // var
	dc.p += cali(dc.p, false, NULL, dc.out);  // e2
	dc_putc(',');
	dc.p += cali(dc.p, false, NULL, dc.out);  // e3
	dc_putc(',');
	dc.p += cali(dc.p, false, NULL, dc.out);  // e4
	dc_putc(':');
	dc.indent++;
}

static void loop_end(void) {
	uint32_t addr = le32(dc.p);
	dc.p += 4;

	uint8_t *mark = mark_at(dc.page, addr);
	Sco *sco = dc.scos->data[dc.page];
	switch (sco->data[addr]) {
	case '{':
		*mark |= WHILE_START;
		*mark_at(dc.page, dc_addr()) &= ~IF_END;  // ??
		break;
	case '<':
		break;
	default:
		error("Unexpected loop structure");
	}
}

static void arguments(const char *sig) {
	const char *sep = " ";
	for (; *sig; sig++) {
		dc_puts(sep);
		sep = ",";

		switch (*sig) {
		case 'e':
		case 'v':
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
	case 'G':
		if (dc.p[1] == 'S' || dc.p[1] == 'X')
			goto cmd2;
		else
			goto cmd1;
	case 'C':
	case 'E':
	case 'I':
	case 'L':
	case 'M':
	case 'P':
	case 'Q':
	case 'S':
	case 'U':
	case 'W':
	case 'Z':
	cmd2:
		dc_putc(*dc.p++);
		dc_putc(*dc.p++);
		return CMD2(dc.p[-2], dc.p[-1]);
	default:
	cmd1:
		dc_putc(*dc.p++);
		return dc.p[-1];
	}
}

static void decompile_page(int page) {
	Sco *sco = dc.scos->data[page];
	dc.page = page;
	dc.p = sco->data + sco->hdrsize;
	dc.indent = 1;
	bool in_menu_item = false;

	while (dc.p < sco->data + sco->filesize) {
		int topaddr = dc.p - sco->data;
		uint8_t mark = sco->mark[dc.p - sco->data];
		if (mark & IF_END) {
			dc.indent--;
			assert(dc.indent > 0);
			indent();
			dc_puts("}\n");
		}
		if (mark & FUNC_TOP)
			dc_printf("**F_%d_%05x:\n", page, dc.p - sco->data);
		if (mark & LABEL)
			dc_printf("*L_%05x:\n", dc.p - sco->data);
		if (mark & DATA) {
			// TODO: find next code block
			break;
		}
		if (*dc.p == '>')
			dc.indent--;
		indent();
		if (*dc.p == 0x20 || *dc.p > 0x80) {
			dc_putc('\'');
			message();
			dc_puts("'\n");
			continue;
		}
		sco->mark[dc.p - sco->data] |= CODE;
		int cmd = get_command();
		switch (cmd) {
		case '!':  // Assign
			dc.p += cali(dc.p, true, NULL, dc.out);
			dc_putc(':');
			dc.p += cali(dc.p, false, NULL, dc.out);
			dc_putc('!');
			break;

		case '{':  // Branch
			conditional();
			break;

		case '@':  // Label jump
			label();
			dc_putc(':');
			break;

		case '\\': // Label call
			label();
			dc_putc(':');
			break;

		case '&':  // Page jump
			dc.p += cali(dc.p, false, NULL, dc.out);
			dc_putc(':');
			break;

		case '%':  // Page call / return
			dc.p += cali(dc.p, false, NULL, dc.out);
			dc_putc(':');
			break;

		case '<':  // For-loop
			for_loop();
			break;

		case '>':  // Loop end
			loop_end();
			break;

		case ']':  // Menu
			break;

		case '$':  // Menu item
			in_menu_item = !in_menu_item;
			if (in_menu_item) {
				label();
				dc_putc('$');
			}
			break;

		case '#':  // Data table address
			data_table();
			break;

		case '~':  // Function call
			funcall();
			break;

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
		case CMD2('C', 'B'): arguments("eeeee"); break;
		case CMD2('C', 'C'): arguments("eeeeee"); break;
		case CMD2('C', 'D'): arguments("eeeeeeeeee"); break;
		case CMD2('C', 'E'): arguments("eeeeeeeee"); break;
		case CMD2('C', 'F'): arguments("eeeee"); break;
		case CMD2('C', 'K'): arguments("neeeeeeee"); break;
		case CMD2('C', 'L'): arguments("eeeee"); break;
		case CMD2('C', 'M'): arguments("eeeeeeeee"); break;
		case CMD2('C', 'P'): arguments("eee"); break;
		case CMD2('C', 'S'): arguments("eeeeeee"); break;
		case CMD2('C', 'T'): arguments("vee"); break;
		case CMD2('C', 'U'): arguments("eeeeee"); break;
		case CMD2('C', 'V'): arguments("eeeeee"); break;
		case CMD2('C', 'X'): arguments("eeeeeeee"); break;
		case CMD2('C', 'Y'): arguments("eeeee"); break;
		case CMD2('C', 'Z'): arguments("eeeeeee"); break;
		case CMD2('E', 'C'): arguments("e"); break;
		case CMD2('E', 'G'): arguments("evvvv"); break;
		case CMD2('E', 'M'): arguments("evee"); break;
		case CMD2('E', 'N'): arguments("veeee"); break;
		case CMD2('E', 'S'): arguments("eeeeee"); break;
		case 'F': arguments("nee"); break;
		case 'G':
			switch (*dc.p++) {
			case 0:
				arguments("e"); break;
			case 1:
				arguments("ee"); break;
			default:
				goto unknown_command;
			}
			break;
		case 'H': arguments("ne"); break;
		case CMD2('I', 'C'): arguments("ev"); break;
		case CMD2('I', 'G'): arguments("veee"); break;
		case CMD2('I', 'K'): arguments("n"); break;
		case CMD2('I', 'M'): arguments("vv"); break;
		case CMD2('I', 'X'): arguments("v"); break;
		case CMD2('I', 'Y'): arguments("e"); break;
		case CMD2('I', 'Z'): arguments("ee"); break;
		case 'J':
			switch (subcommand_num()) {
			case 0:
			case 1:
			case 2:
			case 3:
				arguments("ee"); break;
			case 4:
				arguments(""); break;
			default:
				goto unknown_command;
			}
			break;
		case CMD2('L', 'C'): arguments("ees"); break;
		case CMD2('L', 'D'): arguments("e"); break;
		case CMD2('L', 'E'): arguments("nfee"); break;
		case CMD3('L', 'H', 'D'): arguments("ne"); break;
		case CMD3('L', 'H', 'G'): arguments("ne"); break;
		case CMD3('L', 'H', 'M'): arguments("ne"); break;
		case CMD3('L', 'H', 'S'): arguments("ne"); break;
		case CMD3('L', 'H', 'W'): arguments("ne"); break;
		case CMD3('L', 'X', 'C'): arguments("e"); break;
		case CMD3('L', 'X', 'G'): arguments("ess"); break;
		case CMD3('L', 'X', 'L'): arguments("eee"); break;
		case CMD3('L', 'X', 'O'): arguments("eee"); break;
		case CMD3('L', 'X', 'P'): arguments("eee"); break;
		case CMD3('L', 'X', 'R'): arguments("eve"); break;
		case CMD3('L', 'X', 'S'): arguments("evv"); break;
		case CMD3('L', 'X', 'W'): arguments("eve"); break;
		case CMD2('L', 'L'): arguments("neee"); break;
		case CMD2('L', 'P'): arguments("eve"); break;
		case CMD2('L', 'T'): arguments("ev"); break;
		case CMD2('M', 'A'): arguments("ee"); break;
		case CMD2('M', 'C'): arguments("ee"); break;
		case CMD2('M', 'D'): arguments("eee"); break;
		case CMD2('M', 'E'): arguments("eeeee"); break;
		case CMD2('M', 'F'): arguments("veee"); break;
		case CMD2('M', 'G'): arguments("ne"); break;
		case CMD2('M', 'H'): arguments("eee"); break;
		case CMD2('M', 'I'): arguments("ees"); break;
		case CMD2('M', 'J'): arguments("eeeee"); break;
		case CMD2('M', 'L'): arguments("ve"); break;
		case CMD2('M', 'M'): arguments("ee"); break;
		case CMD2('M', 'N'): arguments("nev"); break;
		case CMD2('M', 'P'): arguments("ee"); break;
		case CMD2('M', 'S'): arguments("es"); break;
		case CMD2('M', 'T'): arguments("s"); break;
		case CMD2('M', 'V'): arguments("e"); break;
		case CMD2('M', 'Z'): arguments("neee"); break;
		case CMD2('P', 'C'): arguments("e"); break;
		case CMD2('P', 'D'): arguments("e"); break;
		case CMD2('P', 'F'): // fall through
		case CMD2('P', 'W'):
			switch (subcommand_num()) {
			case 0:
			case 1:
				arguments("e"); break;
			case 2:
			case 3:
				arguments("ee"); break;
			default:
				goto unknown_command;
			}
			break;
		case CMD2('P', 'G'): arguments("vee"); break;
		case CMD2('P', 'N'): arguments("e"); break;
		case CMD2('P', 'P'): arguments("vee"); break;
		case CMD2('P', 'S'): arguments("eeee"); break;
		case CMD2('P', 'T'):
			switch (subcommand_num()) {
			case 0:
				arguments("vee"); break;
			case 1:
				arguments("vvvee"); break;
			case 2:
				arguments("vvee"); break;
			default:
				goto unknown_command;
			}
			break;
		case CMD2('Q', 'C'): arguments("ee"); break;
		case CMD2('Q', 'D'): arguments("e"); break;
		case CMD2('Q', 'E'): arguments("nfee"); break;
		case CMD2('Q', 'P'): arguments("eve"); break;
		case 'R': break;
		case CMD2('S', 'C'): arguments("v"); break;
		case CMD2('S', 'G'):
			switch (subcommand_num()) {
			case 0:
			case 1:
			case 2:
			case 3:
			case 4:
				arguments("e"); break;
			case 5:
			case 6:
			case 7:
			case 8:
				arguments("ee"); break;
			default:
				goto unknown_command;
			}
			break;
		case CMD2('S', 'I'): arguments("nv"); break;
		case CMD2('S', 'L'): arguments("e"); break;
		case CMD2('S', 'M'): arguments("e"); break;
		case CMD2('S', 'O'): arguments("v"); break;
		case CMD2('S', 'P'): arguments("ee"); break;
		case CMD2('S', 'Q'): arguments("eee"); break;
		case CMD2('S', 'R'): arguments(*dc.p < 0x40 ? "nv" : "ev"); break;
		case CMD2('S', 'S'): arguments("e"); break;
		case CMD2('S', 'T'): arguments("e"); break;
		case CMD2('S', 'U'): arguments("vv"); break;
		case CMD2('S', 'W'): arguments("veee"); break;
		case CMD2('S', 'X'):
			subcommand_num();  // device
			switch (subcommand_num()) {
			case 1:
				arguments("eee"); break;
			case 2:
			case 4:
				arguments("v"); break;
			case 3:
				break;
			default:
				goto unknown_command;
			}
			break;
		case 'T': arguments("ee"); break;
		case CMD2('U', 'C'): arguments("ne"); break;
		case CMD2('U', 'D'): arguments("e"); break;
		case CMD2('U', 'G'): arguments("ee"); break;
		case CMD2('U', 'P'):
			switch (subcommand_num()) {
			case 0:
				arguments("ee"); break;
			case 1:
				arguments("se"); break;
			case 2:
			case 3:
				arguments("ss"); break;
			default:
				goto unknown_command;
			}
			break;
		case CMD2('U', 'R'): arguments("v"); break;
		case CMD2('U', 'S'): arguments("ee"); break;
		case CMD2('W', 'W'): arguments("eee"); break;
		case CMD2('W', 'V'): arguments("eeee"); break;
		case 'X': arguments("e"); break;
		case 'Y': arguments("ee"); break;
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
			error("%s:%x: unknown command %.*s", sjis2utf(sco->sco_name), topaddr, dc_addr() - topaddr, sco->data + topaddr);
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

void decompile(Vector *scos, const char *outdir) {
	memset(&dc, 0, sizeof(dc));
	dc.scos = scos;

	for (int i = 0; i < scos->len; i++)
		decompile_page(i);

	for (int i = 0; i < scos->len; i++) {
		Sco *sco = scos->data[i];
		dc.out = fopen(path_join(outdir, sjis2utf(sco->src_name)), "w");
		decompile_page(i);
		fclose(dc.out);
	}

	write_hed(path_join(outdir, "xsys35dc.hed"));
}
