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
#include <stdlib.h>
#include <string.h>

enum {
	  CODE        = 1 << 0,
	  DATA        = 1 << 1,
	  LABEL       = 1 << 2,
	  FUNC_TOP    = 1 << 3,
	  WHILE_START = 1 << 4,
	  FOR_START   = 1 << 5,
};

typedef struct {
	Vector *scos;
	Vector *variables;
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
	if (addr > sco->filesize)
		error("address out of range (%x:%x)", page, addr);
	return &sco->mark[addr];
}

static void stack_push(Vector *stack, uint32_t n) {
	vec_push(stack, (void *)n);
}

static void stack_pop(Vector *stack) {
	if (stack->len == 0)
		error("stack underflow");
	stack->len--;
}

static uint32_t stack_top(Vector *stack) {
	if (stack->len == 0)
		error("stack_top: empty stack");
	return (uint32_t)stack->data[stack->len - 1];
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

static void cali(bool is_lhs) {
	struct Cali *node = parse_cali(&dc.p, is_lhs);
	if (dc.out)
		print_cali(node, dc.variables, dc.out);
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

static bool is_string_data(const uint8_t *begin, const uint8_t *end) {
	if (*begin == '\0' && begin + 1 == end)
		return true;
	for (const uint8_t *p = begin; p < end;) {
		if (*p == '\0')
			return p - begin >= 2;
		if (is_sjis_byte1(p[0]) && is_sjis_byte2(p[1]))
			p += 2;
		else if (is_sjis_half_kana(*p))
			p++;
		else
			break;
	}
	return false;
}

static void data_block(const uint8_t *p, const uint8_t *end) {
	if (!dc.out)
		return;

	while (p < end) {
		indent();
		if (is_string_data(p, end) || (*p == '\0' && is_string_data(p + 1, end))) {
			dc_putc('"');
			while (*p) {
				uint8_t c = *p++;
				if (c == ' ') {
					dc_puts("\x81\x40"); // full-width space
				} else if (is_sjis_half_kana(c)) {
					uint16_t full = from_sjis_half_kana(c);
					dc_putc(full >> 8);
					dc_putc(full & 0xff);
				} else {
					assert(is_sjis_byte1(c));
					dc_putc(c);
					dc_putc(*p++);
				}
			}
			dc_puts("\"\n");
			p++;
			continue;
		}

		dc_putc('[');
		const char *sep = "";
		for (; p < end && !is_string_data(p, end); p += 2) {
			if (p + 1 == end) {
				warning_at(p, "data block with odd number of bytes");
				dc_printf("%s%d", sep, p[0]);
			} else {
				dc_printf("%s%d", sep, p[0] | p[1] << 8);
			}
			sep = ", ";
		}
		dc_puts("]\n");
	}
}

static void data_table(void) {
	uint32_t addr = le32(dc.p);
	dc.p += 4;
	dc_printf("L_%05x", addr);
	dc_putc(',');
	cali(false);
	dc_putc(':');

	*mark_at(dc.page, addr) |= DATA | LABEL;
}

static void conditional(Vector *branch_end_stack) {
	dc.indent++;
	cali(false);
	dc_putc(':');
	uint32_t endaddr = le32(dc.p);
	dc.p += 4;

	*mark_at(dc.page, endaddr) |= CODE;
	stack_push(branch_end_stack, endaddr);
}

static void funcall(void) {
	uint16_t page = dc.p[0] | dc.p[1] << 8;
	dc.p += 2;
	switch (page) {
	case 0:  // return
		dc_puts("0,");
		cali(false);
		break;
	case 0xffff:
		dc_putc('~');
		cali(false);
		break;
	default:
		{
			page -= 1;
			uint32_t addr = le32(dc.p);
			dc.p += 4;
			dc_printf("F_%d_%05x", page, addr);

			uint8_t *mark = mark_at(page, addr);
			*mark |= FUNC_TOP;
			if (!(*mark & CODE))
				((Sco *)dc.scos->data[page])->preprocessed = false;
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
	parse_cali(&dc.p, false);  // var
	cali(false);  // e2
	dc_putc(',');
	cali(false);  // e3
	dc_putc(',');
	cali(false);  // e4
	dc_putc(':');
	dc.indent++;
}

static void loop_end(Vector *branch_end_stack) {
	uint32_t addr = le32(dc.p);
	dc.p += 4;

	uint8_t *mark = mark_at(dc.page, addr);
	Sco *sco = dc.scos->data[dc.page];
	switch (sco->data[addr]) {
	case '{':
		*mark |= WHILE_START;
		if (stack_top(branch_end_stack) != dc_addr())
			error("while-loop: unexpected address (%d != %d)", stack_top(branch_end_stack), dc_addr());
		stack_pop(branch_end_stack);
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
			cali(false);
			break;
		case 'n':
			dc_printf("%d", *dc.p++);
			break;
		case 's':
		case 'f':
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

static int get_command(void) {
	switch (*dc.p) {
	case '/':
		dc.p++;
		switch(*dc.p++) {
		case 0x06: dc_puts("inc"); return COMMAND_inc;
		case 0x07: dc_puts("dec"); return COMMAND_dec;
		case 0x08: dc_puts("TAA"); return COMMAND_TAA;
		case 0x09: dc_puts("TAB"); return COMMAND_TAB;
		default:
			error_at(dc.p - 2, "Unsupported command 2f %02x", dc.p[-1]);
		}
		break;
	case 'G':
		if (dc.p[1] == 'S' || dc.p[1] == 'X')
			goto cmd2;
		else
			goto cmd1;
	case 'N':
		if (dc.p[1] == 'D')
			goto cmd3;
		goto cmd2;
	case 'V':
		if (dc.p[1] == 'I')
			goto cmd3;
		goto cmd2;
	case 'L':
		if (dc.p[1] == 'H')
			goto cmd3;
		if (dc.p[1] == 'X')
			goto cmd3; // FIXME
		goto cmd2;
	case 'C':
	case 'D':
	case 'E':
	case 'I':
	case 'K':
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
	case 0x10: case 0x11: case 0x12: case 0x13:
	case 0x14: case 0x15: case 0x16: case 0x17:
		dc_putc('!');
		return *dc.p++;
	}
 cmd3:
	dc_putc(*dc.p++);
	dc_putc(*dc.p++);
	dc_putc(*dc.p++);
	return CMD3(dc.p[-3], dc.p[-2], dc.p[-1]);
}

static void decompile_page(int page) {
	Sco *sco = dc.scos->data[page];
	dc.page = page;
	dc.p = sco->data + sco->hdrsize;
	dc.indent = 1;
	bool in_menu_item = false;
	Vector *branch_end_stack = new_vec();

	while (dc.p < sco->data + sco->filesize) {
		int topaddr = dc.p - sco->data;
		uint8_t mark = sco->mark[dc.p - sco->data];
		while (branch_end_stack->len > 0 && stack_top(branch_end_stack) == topaddr) {
			stack_pop(branch_end_stack);
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
			const uint8_t *data_end = dc.p + 1;
			for (; data_end < sco->data + sco->filesize; data_end++) {
				if (sco->mark[data_end - sco->data] & ~DATA)
					break;
			}
			data_block(dc.p, data_end);
			dc.p = data_end;
			continue;
		}
		if (*dc.p == '>')
			dc.indent--;
		indent();
		sco->mark[dc.p - sco->data] |= CODE;
		if (*dc.p == 0x20 || *dc.p > 0x80) {
			dc_putc('\'');
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
				if (*mark_at(dc.page, dc_addr()) != 0)
					break;
			}
			dc_puts("'\n");
			continue;
		}
		if (mark & FOR_START) {
			assert(*dc.p == '!');
			dc.p++;
			dc_putc('<');
			cali(true);
			dc_putc(',');
			cali(false);
			dc_putc(',');
			assert(*dc.p == '<');
			dc.p++;
			for_loop();
			dc_putc('\n');
			continue;
		}
		if (mark & WHILE_START) {
			assert(*dc.p == '{');
			dc.p++;
			dc_puts("<@");
			conditional(branch_end_stack);
			dc_putc('\n');
			continue;
		}
		int cmd = get_command();
		switch (cmd) {
		case '!':  // Assign
		case 0x10: case 0x11: case 0x12: case 0x13:
		case 0x14: case 0x15: case 0x16: case 0x17:
			cali(true);
			if (cmd != '!')
				dc_putc("+-*/%&|^"[cmd - 0x10]);
			dc_putc(':');
			cali(false);
			dc_putc('!');
			break;

		case '{':  // Branch
			conditional(branch_end_stack);
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
			cali(false);
			dc_putc(':');
			break;

		case '%':  // Page call / return
			cali(false);
			dc_putc(':');
			break;

		case '<':  // For-loop
			for_loop();
			break;

		case '>':  // Loop end
			loop_end(branch_end_stack);
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
		case CMD2('D', 'C'): arguments("eee"); break;
		case CMD2('D', 'F'): arguments("vee"); break;
		case CMD2('D', 'I'): arguments("evv"); break;
		case CMD2('D', 'R'): arguments("v"); break;
		case CMD2('D', 'S'): arguments("vvee"); break;
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
		case CMD2('G', 'S'): arguments("ev"); break;
		case CMD2('G', 'X'): arguments("ee"); break;
		case 'H': arguments("ne"); break;
		case CMD2('I', 'C'): arguments("ev"); break;
		case CMD2('I', 'E'): arguments("ee"); break;
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
		case CMD2('K', 'I'): arguments("vee"); break;
		case CMD2('K', 'K'): arguments("e"); break;
		case CMD2('K', 'N'): arguments("v"); break;
		case CMD2('K', 'P'): arguments("v"); break;
		case CMD2('K', 'Q'): arguments("ve"); break;
		case CMD2('K', 'R'): arguments("v"); break;
		case CMD2('K', 'W'): arguments("ve"); break;
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
		case CMD2('N', '+'): arguments("vee"); break;
		case CMD2('N', '-'): arguments("vee"); break;
		case CMD2('N', '*'): arguments("vee"); break;
		case CMD2('N', '/'): arguments("vee"); break;
		case CMD2('N', '>'): arguments("veev"); break;
		case CMD2('N', '<'): arguments("veev"); break;
		case CMD2('N', '='): arguments("veev"); break;
		case CMD2('N', '\\'): arguments("ve"); break;
		case CMD2('N', '&'): arguments("vev"); break;
		case CMD2('N', '|'): arguments("vev"); break;
		case CMD2('N', '^'): arguments("vev"); break;
		case CMD2('N', '~'): arguments("ve"); break;
		case CMD2('N', 'B'): arguments("vve"); break;
		case CMD2('N', 'C'): arguments("ve"); break;
		case CMD2('N', 'I'): arguments("veee"); break;
		case CMD2('N', 'O'): arguments("nvve"); break;
		case CMD2('N', 'P'): arguments("vvev"); break;
		case CMD2('N', 'R'): arguments("ev"); break;
		case CMD2('N', 'T'): arguments("s"); break;
		case CMD3('N', 'D', '+'): arguments("eee"); break;
		case CMD3('N', 'D', '-'): arguments("eee"); break;
		case CMD3('N', 'D', '*'): arguments("eee"); break;
		case CMD3('N', 'D', '/'): arguments("eee"); break;
		case CMD3('N', 'D', 'A'): arguments("ee"); break;
		case CMD3('N', 'D', 'C'): arguments("ee"); break;
		case CMD3('N', 'D', 'D'): arguments("ve"); break;
		case CMD3('N', 'D', 'H'): arguments("ee"); break;
		case CMD3('N', 'D', 'M'): arguments("ee"); break;
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
			dc_putc(',');
			switch (subcommand_num()) {
			case 1:
				dc_putc(','); arguments("eee"); break;
			case 2:
			case 4:
				dc_putc(','); arguments("v"); break;
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
		case CMD2('V', 'A'): arguments("neee"); break;
		case CMD2('V', 'B'): arguments("eeeeeee"); break;
		case CMD2('V', 'C'): arguments("eeeeeee"); break;
		case CMD2('V', 'E'): arguments("eeeeee"); break;
		case CMD2('V', 'F'): arguments(""); break;
		case CMD2('V', 'G'): arguments("eeee"); break;
		case CMD2('V', 'H'): arguments("eeeeee"); break;
		case CMD3('V', 'I', 'C'): arguments("eeee"); break;
		case CMD3('V', 'I', 'P'): arguments("eeee"); break;
		case CMD2('V', 'J'): arguments("eeee"); break;
		case CMD2('V', 'P'): arguments("eeeeee"); break;
		case CMD2('V', 'R'): arguments("eev"); break;
		case CMD2('V', 'S'): arguments("eeeee"); break;
		case CMD2('V', 'T'): arguments("eeeeeeeeee"); break;
		case CMD2('V', 'V'): arguments("ee"); break;
		case CMD2('V', 'W'): arguments("eev"); break;
		case CMD2('V', 'X'): arguments("eeee"); break;
		case CMD2('V', 'Z'): arguments("nee"); break;
		case CMD2('W', 'V'): arguments("eeee"); break;
		case CMD2('W', 'W'): arguments("eee"); break;
		case CMD2('W', 'X'): arguments("eeee"); break;
		case CMD2('W', 'Z'): arguments("ne"); break;
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
		case COMMAND_TOC: arguments(""); break;
		case COMMAND_TOS: arguments(""); break;
		case COMMAND_TPC: arguments("e"); break;
		case COMMAND_TPS: arguments("e"); break;
		case COMMAND_TOP: arguments(""); break;
		case COMMAND_TPP: arguments(""); break;
		case COMMAND_inc: arguments("v"); break;
		case COMMAND_dec: arguments("v"); break;
		case COMMAND_TAA: arguments("e"); break;
		case COMMAND_TAB: arguments("v"); break;
		case COMMAND_wavLoad: arguments("ee"); break;
		case COMMAND_wavPlay: arguments("ee"); break;
		case COMMAND_wavStop: arguments("e"); break;
		case COMMAND_wavUnload: arguments("e"); break;
		case COMMAND_wavIsPlay: arguments("ev"); break;
		case COMMAND_wavFade: arguments("eeee"); break;
		case COMMAND_wavIsFade: arguments("ev"); break;
		case COMMAND_wavStopFade: arguments("e"); break;
		case COMMAND_trace: arguments("z"); break;
		case COMMAND_wav3DSetPos: arguments("eeee"); break;
		case COMMAND_wav3DCommit: arguments(""); break;
		case COMMAND_wav3DGetPos: arguments("evvv"); break;
		case COMMAND_wav3DSetPosL: arguments("eee"); break;
		case COMMAND_wav3DGetPosL: arguments("vvv"); break;
		case COMMAND_wav3DFadePos: arguments("eeeee"); break;
		case COMMAND_wav3DIsFadePos: arguments("ev"); break;
		case COMMAND_wav3DStopFadePos: arguments("e"); break;
		case COMMAND_wav3DFadePosL: arguments("eeee"); break;
		case COMMAND_wav3DIsFadePosL: arguments("v"); break;
		case COMMAND_wav3DStopFadePosL: arguments(""); break;
		case COMMAND_sndPlay: arguments("ee"); break;
		case COMMAND_sndStop: arguments(""); break;
		case COMMAND_sndIsPlay: arguments("v"); break;
		case COMMAND_msg: arguments("z"); break;
		case COMMAND_newHH: arguments("ne"); break;
		case COMMAND_newLC: arguments("eez"); break;
		case COMMAND_newLE: arguments("nzee"); break;
		case COMMAND_newLXG: arguments("ezz"); break;
		case COMMAND_newMI: arguments("eez"); break;
		case COMMAND_newMS: arguments("ez"); break;
		case COMMAND_newMT: arguments("z"); break;
		case COMMAND_newNT: arguments("z"); break;
		case COMMAND_newQE: arguments("nzee"); break;
		case COMMAND_newUP:
			switch (subcommand_num()) {
			case 0:
				arguments("ee"); break;
			case 1:
				arguments("ze"); break;
			case 2:
			case 3:
				arguments("zz"); break;
			default:
				goto unknown_command;
			}
			break;
		case COMMAND_newF: arguments("nee"); break;
		case COMMAND_wavWaitTime: arguments("ee"); break;
		case COMMAND_wavGetPlayPos: arguments("ev"); break;
		case COMMAND_wavWaitEnd: arguments("e"); break;
		case COMMAND_wavGetWaveTime: arguments("ev"); break;
		case COMMAND_menuSetCbkSelect: arguments("F"); break;
		case COMMAND_menuSetCbkCancel: arguments("F"); break;
		case COMMAND_menuClearCbkSelect: arguments(""); break;
		case COMMAND_menuClearCbkCancel: arguments(""); break;
		case COMMAND_wav3DSetMode: arguments("ee"); break;
		case COMMAND_grCopyStretch: arguments("eeeeeeeee"); break;
		case COMMAND_grFilterRect: arguments("eeeee"); break;
		case COMMAND_iptClearWheelCount: arguments(""); break;
		case COMMAND_iptGetWheelCount: arguments("vv"); break;
		case COMMAND_menuGetFontSize: arguments("v"); break;
		case COMMAND_msgGetFontSize: arguments("v"); break;
		case COMMAND_strGetCharType: arguments("eev"); break;
		case COMMAND_strGetLengthASCII: arguments("ev"); break;
		case COMMAND_sysWinMsgLock: arguments(""); break;
		case COMMAND_sysWinMsgUnlock: arguments(""); break;
		case COMMAND_aryCmpCount: arguments("veev"); break;
		case COMMAND_aryCmpTrans: arguments("veeeev"); break;
		case COMMAND_grBlendColorRect: arguments("eeeeeeeee"); break;
		case COMMAND_grDrawFillCircle: arguments("eeee"); break;
		case COMMAND_MHH: arguments("eee"); break;
		case COMMAND_menuSetCbkInit: arguments("F"); break;
		case COMMAND_menuClearCbkInit: arguments(""); break;
		case COMMAND_sysOpenShell: arguments("z"); break;
		case COMMAND_sysAddWebMenu: arguments("zz"); break;
		case COMMAND_iptSetMoveCursorTime: arguments("e"); break;
		case COMMAND_iptGetMoveCursorTime: arguments("v"); break;
		case COMMAND_grBlt: arguments("eeeeee"); break;
		case COMMAND_LXWT: arguments("ez"); break;
		case COMMAND_LXWS: arguments("ee"); break;
		case COMMAND_LXWE: arguments("ee"); break;
		case COMMAND_LXWH: arguments("ene"); break;
		case COMMAND_LXWHH: arguments("ene"); break;
		case COMMAND_sysGetOSName: arguments("e"); break;
		case COMMAND_patchEC: arguments("e"); break;
		case COMMAND_mathSetClipWindow: arguments("eeee"); break;
		case COMMAND_mathClip: arguments("vvvvvv"); break;
		case COMMAND_LXF: arguments("ezz"); break;
		case COMMAND_strInputDlg: arguments("zeev"); break;
		case COMMAND_strCheckASCII: arguments("ev"); break;
		case COMMAND_strCheckSJIS: arguments("ev"); break;
		case COMMAND_strMessageBox: arguments("z"); break;
		case COMMAND_strMessageBoxStr: arguments("e"); break;
		case COMMAND_grCopyUseAMapUseA: arguments("eeeeeee"); break;
		case COMMAND_grSetCEParam: arguments("ee"); break;
		case COMMAND_grEffectMoveView: arguments("eeee"); break;
		case COMMAND_cgSetCacheSize: arguments("e"); break;
		case COMMAND_gaijiSet: arguments("ee"); break;
		case COMMAND_gaijiClearAll: arguments(""); break;
		case COMMAND_menuGetLatestSelect: arguments("v"); break;
		case COMMAND_lnkIsLink: arguments("eev"); break;
		case COMMAND_lnkIsData: arguments("eev"); break;
		case COMMAND_fncSetTable: arguments("eF"); break;
		case COMMAND_fncSetTableFromStr: arguments("eev"); break;
		case COMMAND_fncClearTable: arguments("e"); break;
		case COMMAND_fncCall: arguments("e"); break;
		case COMMAND_fncSetReturnCode: arguments("e"); break;
		case COMMAND_fncGetReturnCode: arguments("v"); break;
		case COMMAND_msgSetOutputFlag: arguments("e"); break;
		case COMMAND_saveDeleteFile: arguments("ev"); break;
		case COMMAND_wav3DSetUseFlag: arguments("e"); break;
		case COMMAND_wavFadeVolume: arguments("eeee"); break;
		case COMMAND_patchEMEN: arguments("e"); break;
		case COMMAND_wmenuEnableMsgSkip: arguments("e"); break;
		case COMMAND_winGetFlipFlag: arguments("v"); break;
		case COMMAND_cdGetMaxTrack: arguments("v"); break;
		case COMMAND_dlgErrorOkCancel: arguments("zv"); break;
		case COMMAND_menuReduce: arguments("e"); break;
		case COMMAND_menuGetNumof: arguments("v"); break;
		case COMMAND_menuGetText: arguments("ee"); break;
		case COMMAND_menuGoto: arguments("ee"); break;
		case COMMAND_menuReturnGoto: arguments("ee"); break;
		case COMMAND_menuFreeShelterDIB: arguments(""); break;
		case COMMAND_msgFreeShelterDIB: arguments(""); break;
		default:
		unknown_command:
			error("%s:%x: unknown command '%.*s'", sjis2utf(sco->sco_name), topaddr, dc_addr() - topaddr, sco->data + topaddr);
		}
		dc_putc('\n');
	}
	while (branch_end_stack->len > 0 && stack_top(branch_end_stack) == sco->filesize) {
		stack_pop(branch_end_stack);
		dc.indent--;
		assert(dc.indent > 0);
		indent();
		dc_puts("}\n");
	}
	if (sco->mark[sco->filesize] & LABEL)
		dc_printf("*L_%05x:\n", sco->filesize);
}

static void write_sysver(const char *path) {
	if (dc.scos->len == 0)
		return;
	Sco *sco = dc.scos->data[0];
	FILE *fp = fopen(path, "w");
	switch (sco->version) {
	case SCO_S350: fputs("S350\n", fp); break;
	case SCO_S351: fputs("3.5\n", fp); break;
	case SCO_153S: fputs("153S\n", fp); break;
	case SCO_S360: fputs("3.6\n", fp); break;
	case SCO_S380: fputs("3.8\n", fp); break;
	}
	fclose(fp);
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

static void write_variables(const char *path) {
	FILE *fp = fopen(path, "w");
	for (int i = 0; i < dc.variables->len; i++) {
		const char *s = dc.variables->data[i];
		fprintf(fp, "%s\n", s ? s : "");
	}
	fclose(fp);
}

noreturn void error_at(const uint8_t *pos, char *fmt, ...) {
	Sco *sco = dc.scos->data[dc.page];
	assert(sco->data <= pos);
	assert(pos < sco->data + sco->filesize);;
	fprintf(stderr, "%s:%lx: ", sjis2utf(sco->sco_name), pos - sco->data);
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fputc('\n', stderr);
	exit(1);
}

void warning_at(const uint8_t *pos, char *fmt, ...) {
	Sco *sco = dc.scos->data[dc.page];
	assert(sco->data <= pos);
	assert(pos < sco->data + sco->filesize);;
	fprintf(stderr, "Waring: %s:%lx: ", sjis2utf(sco->sco_name), pos - sco->data);
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fputc('\n', stderr);
}

void decompile(Vector *scos, const char *outdir) {
	memset(&dc, 0, sizeof(dc));
	dc.scos = scos;
	dc.variables = new_vec();

	// Preprocess
	bool done = false;
	while (!done) {
		done = true;
		for (int i = 0; i < scos->len; i++) {
			Sco *sco = scos->data[i];
			if (sco->preprocessed)
				continue;
			done = false;
			decompile_page(i);
			sco->preprocessed = true;
		}
	}

	// Decompile
	for (int i = 0; i < scos->len; i++) {
		Sco *sco = scos->data[i];
		dc.out = fopen(path_join(outdir, sjis2utf(sco->src_name)), "w");
		decompile_page(i);
		fclose(dc.out);
	}

	write_sysver(path_join(outdir, "sysver.txt"));
	write_hed(path_join(outdir, "xsys35dc.hed"));
	write_variables(path_join(outdir, "variables.txt"));
}
