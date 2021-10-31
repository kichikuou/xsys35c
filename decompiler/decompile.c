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
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Config config = {
	.utf8_output = true,
};

static inline void annotate(uint8_t* mark, int type) {
	*mark = (*mark & ~TYPE_MASK) | type;
}

typedef struct {
	Vector *scos;
	Ain *ain;
	Vector *variables;
	HashMap *functions; // Function -> Function (itself)
	FILE *out;

	int page;
	const uint8_t *p;  // Points inside scos->data[page]->data
	int indent;

	bool disable_else;
	bool disable_ain_message;
	bool disable_ain_variable;
	bool old_SR;
} Decompiler;

static Decompiler dc;

static inline Sco *current_sco(void) {
	return dc.scos->data[dc.page];
}

static inline int dc_addr(void) {
	return dc.p - current_sco()->data;
}

static const uint8_t *code_at(int page, int addr) {
	if (page >= dc.scos->len)
		error("page out of range (%x:%x)", page, addr);
	Sco *sco = dc.scos->data[page];
	if (addr >= sco->filesize)
		error("address out of range (%x:%x)", page, addr);
	return &sco->data[addr];
}

static uint8_t *mark_at(int page, int addr) {
	if (page >= dc.scos->len)
		error("page out of range (%x:%x)", page, addr);
	Sco *sco = dc.scos->data[page];
	if (!sco)
		error("page does not exist (%x:%x)", page, addr);
	if (addr > sco->filesize)
		error("address out of range (%x:%x)", page, addr);
	return &sco->mark[addr];
}

static const uint8_t *advance_char(const uint8_t *s) {
	if (config.utf8_input) {
		while (UTF8_TRAIL_BYTE(*++s))
			;
	} else {
		s += is_sjis_byte1(*s) ? 2 : 1;
	}
	return s;
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

enum dc_put_string_flags {
	STRING_ESCAPE = 1 << 0,
	STRING_EXPAND = 1 << 1,
};

static void dc_put_string_n(const char *s, int len, unsigned flags) {
	if (!dc.out)
		return;

	const char *end = s + len;
	while (s < end) {
		uint8_t c = *s++;
		if (isgraph(c)) {
			if (flags & STRING_ESCAPE && (c == '\\' || c == '\'' || c == '"' || c == '<'))
				dc_putc('\\');
			dc_putc(c);
		} else if (config.utf8_input) {
			dc_putc(c);
			while (UTF8_TRAIL_BYTE(*s))
				dc_putc(*s++);
		} else if (is_compacted_sjis(c)) {
			if (flags & STRING_EXPAND) {
				uint16_t full = expand_sjis(c);
				dc_putc(full >> 8);
				dc_putc(full & 0xff);
			} else {
				dc_putc(c);
			}
		} else if (c == 0xde || c == 0xdf) {  // Halfwidth (semi-)voiced sound mark
			dc_putc(c);
		} else {
			assert(is_sjis_byte1(c));
			uint8_t c2 = *s++;
			if (config.utf8_output && (flags & STRING_ESCAPE) && !is_unicode_safe(c, c2)) {
				dc_printf("<0x%04X>", c << 8 | c2);
			} else {
				dc_putc(c);
				dc_putc(c2);
			}
		}
	}
}

static const void *dc_put_string(const char *s, int terminator, unsigned flags) {
	const char *end = strchr(s, terminator);
	assert(end);
	dc_put_string_n(s, end - s, flags);
	return end + 1;
}

static void print_address(void) {
	if (config.address)
		dc_printf("/* %05x */\t", dc_addr());
}

static void indent(void) {
	if (!dc.out)
		return;
	print_address();
	for (int i = 0; i < dc.indent; i++)
		fputc('\t', dc.out);
}

static Cali *cali(bool is_lhs) {
	Cali *node = parse_cali(&dc.p, is_lhs);
	if (dc.out)
		print_cali(node, dc.variables, dc.out);
	return node;
}

static void page_name(int cmd) {
	Cali *node = parse_cali(&dc.p, false);
	if (!dc.out)
		return;
	if (node->type == NODE_NUMBER) {
		int page = node->val;
		if ((cmd != '%' || page != 0) && page < dc.scos->len) {
			Sco *sco = dc.scos->data[page];
			if (sco) {
				fprintf(dc.out, "#%s", sco->src_name);
				return;
			}
		}
	}
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

	uint8_t *mark = mark_at(dc.page, addr);
	if (!(*mark & LABEL) && addr < dc_addr())
		current_sco()->analyzed = false;
	*mark |= LABEL;
}

static bool is_string_data(const uint8_t *begin, const uint8_t *end, bool should_expand) {
	if (*begin == '\0' && begin + 1 == end)
		return true;
	for (const uint8_t *p = begin; p < end;) {
		if (*p == '\0')
			return p - begin >= 2;
		if (config.utf8_input) {
			const uint8_t *next;
			if (*p <= 0x7f) {
				next = p + 1;
			} else if (p[0] <= 0xdf) {
				next = p + 2;
			} else if (p[0] <= 0xef) {
				next = p + 3;
			} else if (p[0] <= 0xf7) {
				next = p + 4;
			} else {
				return false;
			}
			if (next > end)
				return false;
			while (++p < next) {
				if (!UTF8_TRAIL_BYTE(*p))
					return false;
			}
		} else {
			if (is_valid_sjis(p[0], p[1]))
				p += 2;
			else if (isprint(*p) || (should_expand && is_compacted_sjis(*p)))
				p++;
			else
				break;
		}
	}
	return false;
}

static void data_block(const uint8_t *end) {
	if (!dc.out) {
		dc.p = end;
		return;
	}

	bool should_expand = current_sco()->version <= SCO_S351;
	bool prefer_string = false;

	while (dc.p < end) {
		indent();
		if (is_string_data(dc.p, end, should_expand) ||
			(*dc.p == '\0' && (prefer_string || is_string_data(dc.p + 1, end, should_expand)))) {
			dc_putc('"');
			unsigned flags = STRING_ESCAPE | (should_expand ? STRING_EXPAND : 0);
			dc.p = dc_put_string((const char *)dc.p, '\0', flags);
			dc_puts("\"\n");
			prefer_string = true;
			continue;
		}
		prefer_string = false;

		dc_putc('[');
		const char *sep = "";
		for (; dc.p < end && !is_string_data(dc.p, end, should_expand); dc.p += 2) {
			if (dc.p + 1 == end) {
				warning_at(dc.p, "data block with odd number of bytes");
				dc_printf("%s%db", sep, dc.p[0]);
				dc.p++;
				break;
			} else {
				dc_printf("%s%d", sep, dc.p[0] | dc.p[1] << 8);
			}
			sep = ", ";
		}
		dc_puts("]\n");
	}
}

static void data_table_addr(void) {
	uint32_t addr = le32(dc.p);
	dc.p += 4;
	dc_printf("L_%05x", addr);
	dc_puts(", ");
	cali(false);
	dc_putc(':');

	uint8_t *mark = mark_at(dc.page, addr);
	uint8_t old_mark = *mark;
	annotate(mark, DATA_TABLE | LABEL);
	if (*mark != old_mark)
		current_sco()->analyzed = false;
}

static bool data_table(void) {
	Sco *sco = current_sco();
	uint32_t pos = dc.p - sco->data;
	uint32_t first_data = sco->filesize;
	while (pos < first_data) {
		uint32_t addr = le32(&sco->data[pos]);
		pos += 4;
		if (addr < sco->hdrsize || addr >= sco->filesize)
			return false;
		if (pos <= addr && addr < first_data)
			first_data = addr;
		if (sco->mark[pos])
			break;
	}

	for (; dc.p < sco->data + pos; dc.p += 4) {
		uint32_t addr = le32(dc.p);
		indent();
		dc_printf("_L_%05x:\n", addr);
		if ((sco->mark[addr] & (DATA | LABEL)) != (DATA | LABEL)) {
			sco->mark[addr] |= DATA | LABEL;
			sco->analyzed = false;
		}
	}
	return true;
}

static uint8_t *get_surrounding_else(Vector *branch_end_stack) {
	if (branch_end_stack->len == 0)
		return NULL;
	uint8_t *mark = mark_at(dc.page, dc_addr() - 6);
	if ((*mark & TYPE_MASK) != ELSE)
		return NULL;
	uint32_t addr = le32(dc.p - 5);
	if (addr != stack_top(branch_end_stack))
		return NULL;
	return mark;
}

static void conditional(Vector *branch_end_stack) {
	uint8_t *surrounding_else = get_surrounding_else(branch_end_stack);

	dc.indent++;
	cali(false);
	dc_putc(':');
	uint32_t endaddr = le32(dc.p);
	dc.p += 4;

	*mark_at(dc.page, endaddr) |= CODE;
	const uint8_t *epilogue = code_at(dc.page, endaddr - 5);
	switch (*epilogue) {
	case '@':
		if (!dc.disable_else) {
			uint32_t addr = le32(epilogue + 1);
			if (endaddr <= addr && addr <= current_sco()->filesize) {
				if (surrounding_else && stack_top(branch_end_stack) == addr) {
					stack_pop(branch_end_stack);
					annotate(surrounding_else, ELSE_IF);
				}
				uint8_t *m = mark_at(dc.page, endaddr - 5);
				if ((*m & TYPE_MASK) != ELSE_IF)
					annotate(m, ELSE);
				endaddr = addr;
			} else {
				dc.disable_else = true;
			}
		}
		break;
	case '>':
		break;
	default:
		dc.disable_else = true;
		break;
	}
	stack_push(branch_end_stack, endaddr);
}

static void defun(Function *f, const char *name) {
	dc_puts("**");
	dc_puts(name);
	for (int i = 0; i < f->argc; i++) {
		dc_puts(i == 0 ? " " : ", ");
		Cali node = {.type = NODE_VARIABLE, .val = f->argv[i]};
		print_cali(&node, dc.variables, dc.out);
	}
	dc_putc(':');
}

static void func_labels(uint16_t page, uint32_t addr) {
	if (!dc.out)
		return;
	const Function key = { .page = page + 1, .addr = addr };
	Function *f = hash_get(dc.functions, &key);
	if (!f)
		error("BUG: function record for (%d:%x) not found", page, addr);

	print_address();
	defun(f, f->name);
	dc_putc('\n');
	if (f->aliases) {
		for (int i = 0; i < f->aliases->len; i++) {
			print_address();
			defun(f, f->aliases->data[i]);
			dc_putc('\n');
		}
	}
}

static Function *get_function(uint16_t page, uint32_t addr) {
	const Function key = { .page = page + 1, .addr = addr };
	Function *f = hash_get(dc.functions, &key);
	if (f)
		return f;

	if (dc.ain && dc.ain->functions)
		warning_at(dc.p, "function %d:%d is not found in System39.ain", page, addr);

	f = calloc(1, sizeof(Function));
	if (page < dc.scos->len && dc.scos->data[page]) {
		Sco *sco = dc.scos->data[page];
		char *name = malloc(strlen(sco->sco_name) + 10);
		strcpy(name, sco->sco_name);
		char *p = strrchr(name, '.');
		if (!p)
			p = name + strlen(name);
		sprintf(p, "_%x", addr);
		f->name = name;
	} else {
		char name[16];
		sprintf(name, "F_%d_%05x", page, addr);
		f->name = strdup(name);
	}
	f->page = page + 1;
	f->addr = addr;
	f->argc = -1;
	hash_put(dc.functions, f, f);
	return f;
}

static Function *func_label(uint16_t page, uint32_t addr) {
	Function *f = get_function(page, addr);
	dc_puts(f->name);

	if (page < dc.scos->len && dc.scos->data[page]) {
		uint8_t *mark = mark_at(page, addr);
		*mark |= FUNC_TOP;
		if (!(*mark & (CODE | DATA))) {
			if (page != dc.page || addr < dc_addr())
				((Sco *)dc.scos->data[page])->analyzed = false;
		}
	}
	return f;
}

static uint16_t get_next_assignment_var(Sco *sco, uint32_t *addr) {
	assert(sco->data[*addr] == '!');
	const uint8_t *p = sco->data + *addr + 1;
	Cali *node = parse_cali(&p, true);
	assert(node->type == NODE_VARIABLE);
	// skip to next arg
	do
		(*addr)++;
	while (!sco->mark[*addr]);
	return node->val;
}

// When a function Func is defined as "**Func var1,...,varn:", a call to this
// function "~func arg1,...,argn:" is compiled to this command sequence:
//  !var1:arg1!
//      ...
//  !varn:argn!
//  ~func:
//
// Since parameter information is lost in SCO, we infer the parameters by
// examining preceding variable assignments that are common to all calls to the
// function.
static void analyze_args(Function *func, uint32_t topaddr_candidate, uint32_t funcall_addr) {
	if (!topaddr_candidate) {
		func->argc = 0;
		return;
	}
	Sco *sco = dc.scos->data[dc.page];

	// Count the number of preceding variable assignments
	int argc = 0;
	for (int addr = topaddr_candidate; addr < funcall_addr; addr++) {
		if (sco->mark[addr]) {
			assert(sco->data[addr] == '!');
			argc++;
		}
	}

	if (func->argc == -1) {
		// This is the first callsite we've found.
		uint16_t *argv = malloc(argc * sizeof(uint16_t));
		int argi = 0;
		for (uint32_t addr = topaddr_candidate; addr < funcall_addr;)
			argv[argi++] = get_next_assignment_var(sco, &addr);
		assert(argi == argc);
		func->argc = argc;
		func->argv = argv;
	} else {
		// Find the longest common suffix of the variable assignments here and
		// the parameter candidates in `func`, and update `func`.
		if (argc < func->argc) {
			func->argv += func->argc - argc;
			func->argc = argc;
		}
		for (; argc > func->argc; argc--) {
			do
				topaddr_candidate++;
			while (!sco->mark[topaddr_candidate]);
		}
		assert(argc == func->argc);
		int argi = 0;
		int last_mismatch = 0;
		for (uint32_t addr = topaddr_candidate; addr < funcall_addr;) {
			if (func->argv[argi++] != get_next_assignment_var(sco, &addr)) {
				topaddr_candidate = addr;
				last_mismatch = argi;
			}
		}
		func->argc -= last_mismatch;
		func->argv += last_mismatch;
	}
	if (topaddr_candidate < funcall_addr) {
		// From next time, this funcall will be handled by funcall_with_args().
		annotate(sco->mark + topaddr_candidate, FUNCALL_TOP);
	}
}

static bool funcall_with_args(void) {
	Sco *sco = dc.scos->data[dc.page];

	// Count the number of preceding variable assignments
	int argc = 0;
	int addr = dc.p - sco->data;
	bool was_not_funcall = false;
	while (sco->data[addr] == '!') {
		argc++;
		// skip to next arg
		do
			addr++;
		while (!sco->mark[addr]);
		if (sco->mark[addr] != CODE) {
			// This happens when a label was inserted in the middle of variable
			// assignments sequence, after the FUNCALL_TOP annotation was added.
			annotate(sco->mark + (dc.p - sco->data), 0);  // Remove FUNCALL_TOP annotation
			if (sco->data[addr] == '!')
				annotate(sco->mark + addr, FUNCALL_TOP);
			was_not_funcall = true;
			argc = 0;
		}
	}
	assert(sco->data[addr] == '~');

	uint16_t page = (sco->data[addr + 1] | sco->data[addr + 2] << 8) - 1;
	uint32_t funcaddr = le32(sco->data + addr + 3);
	Function *func = get_function(page, funcaddr);
	if (was_not_funcall) {
		if (argc < func->argc) {
			func->argv += func->argc - argc;
			func->argc = argc;
		}
		return false;
	}

	if (func->argc < argc) {
		// func->argc has been decremented since this FUNCALL_TOP was added.
		addr = dc.p - sco->data;
		annotate(sco->mark + addr, 0);  // Remove the FUNCALL_TOP annotation
		if (!func->argc)
			return false;
		while (func->argc < argc--) {
			// skip to next arg
			do
				addr++;
			while (!sco->mark[addr]);
		}
		annotate(sco->mark + addr, FUNCALL_TOP);
		return false;
	}
	assert(argc == func->argc);
	dc_putc('~');
	dc_puts(func->name);
	char *sep = " ";
	while (argc-- > 0) {
		dc.p++;  // skip '!'
		parse_cali(&dc.p, true);  // skip varname
		dc_puts(sep);
		sep = ", ";
		cali(false);
	}
	dc_putc(':');
	assert(dc.p == sco->data + addr);
	dc.p += 7;  // skip '~', page, funcaddr
	return true;
}

static void funcall(uint32_t topaddr_candidate) {
	uint32_t calladdr = dc_addr() - 1;
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
			Function *func = func_label(page, addr);
			analyze_args(func, topaddr_candidate, calladdr);
			break;
		}
	}
	dc_putc(':');
}

static void for_loop(void) {
	uint8_t *mark = mark_at(dc.page, dc_addr()) - 2;
	while (!(*mark & CODE))
		mark--;
	annotate(mark, FOR_START);
	if (*dc.p++ != 0)
		error("for_loop: 0 expected, got 0x%02x", *--dc.p);
	if (*dc.p++ != '<')
		error("for_loop: '<' expected, got 0x%02x", *--dc.p);
	if (*dc.p++ != 1)
		error("for_loop: 1 expected, got 0x%02x", *--dc.p);
	dc.p += 4; // skip label
	parse_cali(&dc.p, false);  // var
	cali(false);  // e2
	dc_puts(", ");
	cali(false);  // e3
	dc_puts(", ");
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
		annotate(mark, WHILE_START);
		if (stack_top(branch_end_stack) != dc_addr())
			error("while-loop: unexpected address (%x != %x)", stack_top(branch_end_stack), dc_addr());
		stack_pop(branch_end_stack);
		break;
	case '<':
		break;
	default:
		error("Unexpected loop structure");
	}
}

// Decompile command arguments. Directives:
//  e: expression
//  n: number (single-byte)
//  s: string (colon-terminated)
//  v: variable
//  z: string (zero-terminated)
//  F: function name
static void arguments(const char *sig) {
	const char *sep = " ";
	for (; *sig; sig++) {
		dc_puts(sep);
		sep = ", ";

		switch (*sig) {
		case 'e':
		case 'v':
			cali(false);
			break;
		case 'n':
			dc_printf("%d", *dc.p++);
			break;
		case 's':
		case 'z':
			{
				uint8_t terminator = *sig == 'z' ? 0 : ':';
				if (current_sco()->version <= SCO_S360) {
					dc.p = dc_put_string((const char *)dc.p, terminator, 0);
				} else {
					dc_putc('"');
					dc.p = dc_put_string((const char *)dc.p, terminator, STRING_ESCAPE);
					dc_putc('"');
				}
			}
			break;
		case 'o':  // obfuscated string
			{
				if (*dc.p != 0)
					error_at(dc.p, "0x00 expected");
				dc_putc('"');
				char *buf = strdup((const char *)++dc.p);
				for (uint8_t *p = (uint8_t *)buf; *p; p++)
					*p = *p >> 4 | *p << 4;
				dc_put_string(buf, '\0', 0);
				dc.p += strlen(buf) + 1;
				dc_putc('"');
			}
			break;
		case 'F':
			{
				uint16_t page = (dc.p[0] | dc.p[1] << 8) - 1;
				uint32_t addr = le32(dc.p + 2);
				func_label(page, addr);
				dc.p += 6;
			}
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
		case 0x00: dc_puts("TOC"); return COMMAND_TOC;
		case 0x01: dc_puts("TOS"); return COMMAND_TOS;
		case 0x02: dc_puts("TPC"); return COMMAND_TPC;
		case 0x03: dc_puts("TPS"); return COMMAND_TPS;
		case 0x04: dc_puts("TOP"); return COMMAND_TOP;
		case 0x05: dc_puts("TPP"); return COMMAND_TPP;
		case 0x06: dc_puts("inc"); return COMMAND_inc;
		case 0x07: dc_puts("dec"); return COMMAND_dec;
		case 0x08: dc_puts("TAA"); return COMMAND_TAA;
		case 0x09: dc_puts("TAB"); return COMMAND_TAB;
		case 0x0a: dc_puts("wavLoad"); return COMMAND_wavLoad;
		case 0x0b: dc_puts("wavPlay"); return COMMAND_wavPlay;
		case 0x0c: dc_puts("wavStop"); return COMMAND_wavStop;
		case 0x0d: dc_puts("wavUnload"); return COMMAND_wavUnload;
		case 0x0e: dc_puts("wavIsPlay"); return COMMAND_wavIsPlay;
		case 0x0f: dc_puts("wavFade"); return COMMAND_wavFade;
		case 0x10: dc_puts("wavIsFade"); return COMMAND_wavIsFade;
		case 0x11: dc_puts("wavStopFade"); return COMMAND_wavStopFade;
		case 0x12: dc_puts("trace"); return COMMAND_trace;
		case 0x13: dc_puts("wav3DSetPos"); return COMMAND_wav3DSetPos;
		case 0x14: dc_puts("wav3DCommit"); return COMMAND_wav3DCommit;
		case 0x15: dc_puts("wav3DGetPos"); return COMMAND_wav3DGetPos;
		case 0x16: dc_puts("wav3DSetPosL"); return COMMAND_wav3DSetPosL;
		case 0x17: dc_puts("wav3DGetPosL"); return COMMAND_wav3DGetPosL;
		case 0x18: dc_puts("wav3DFadePos"); return COMMAND_wav3DFadePos;
		case 0x19: dc_puts("wav3DIsFadePos"); return COMMAND_wav3DIsFadePos;
		case 0x1a: dc_puts("wav3DStopFadePos"); return COMMAND_wav3DStopFadePos;
		case 0x1b: dc_puts("wav3DFadePosL"); return COMMAND_wav3DFadePosL;
		case 0x1c: dc_puts("wav3DIsFadePosL"); return COMMAND_wav3DIsFadePosL;
		case 0x1d: dc_puts("wav3DStopFadePosL"); return COMMAND_wav3DStopFadePosL;
		case 0x1e: dc_puts("sndPlay"); return COMMAND_sndPlay;
		case 0x1f: dc_puts("sndStop"); return COMMAND_sndStop;
		case 0x20: dc_puts("sndIsPlay"); return COMMAND_sndIsPlay;
		case 0x21: return COMMAND_msg;
		case 0x22: dc_puts("HH"); return COMMAND_newHH;
		case 0x23: dc_puts("LC"); return COMMAND_newLC;
		case 0x24: dc_puts("LE"); return COMMAND_newLE;
		case 0x25: dc_puts("LXG"); return COMMAND_newLXG;
		case 0x26: dc_puts("MI"); return COMMAND_newMI;
		case 0x27: dc_puts("MS"); return COMMAND_newMS;
		case 0x28: dc_puts("MT"); return COMMAND_newMT;
		case 0x29: dc_puts("NT"); return COMMAND_newNT;
		case 0x2a: dc_puts("QE"); return COMMAND_newQE;
		case 0x2b: dc_puts("UP"); return COMMAND_newUP;
		case 0x2c: dc_puts("F"); return COMMAND_newF;
		case 0x2d: dc_puts("wavWaitTime"); return COMMAND_wavWaitTime;
		case 0x2e: dc_puts("wavGetPlayPos"); return COMMAND_wavGetPlayPos;
		case 0x2f: dc_puts("wavWaitEnd"); return COMMAND_wavWaitEnd;
		case 0x30: dc_puts("wavGetWaveTime"); return COMMAND_wavGetWaveTime;
		case 0x31: dc_puts("menuSetCbkSelect"); return COMMAND_menuSetCbkSelect;
		case 0x32: dc_puts("menuSetCbkCancel"); return COMMAND_menuSetCbkCancel;
		case 0x33: dc_puts("menuClearCbkSelect"); return COMMAND_menuClearCbkSelect;
		case 0x34: dc_puts("menuClearCbkCancel"); return COMMAND_menuClearCbkCancel;
		case 0x35: dc_puts("wav3DSetMode"); return COMMAND_wav3DSetMode;
		case 0x36: dc_puts("grCopyStretch"); return COMMAND_grCopyStretch;
		case 0x37: dc_puts("grFilterRect"); return COMMAND_grFilterRect;
		case 0x38: dc_puts("iptClearWheelCount"); return COMMAND_iptClearWheelCount;
		case 0x39: dc_puts("iptGetWheelCount"); return COMMAND_iptGetWheelCount;
		case 0x3a: dc_puts("menuGetFontSize"); return COMMAND_menuGetFontSize;
		case 0x3b: dc_puts("msgGetFontSize"); return COMMAND_msgGetFontSize;
		case 0x3c: dc_puts("strGetCharType"); return COMMAND_strGetCharType;
		case 0x3d: dc_puts("strGetLengthASCII"); return COMMAND_strGetLengthASCII;
		case 0x3e: dc_puts("sysWinMsgLock"); return COMMAND_sysWinMsgLock;
		case 0x3f: dc_puts("sysWinMsgUnlock"); return COMMAND_sysWinMsgUnlock;
		case 0x40: dc_puts("aryCmpCount"); return COMMAND_aryCmpCount;
		case 0x41: dc_puts("aryCmpTrans"); return COMMAND_aryCmpTrans;
		case 0x42: dc_puts("grBlendColorRect"); return COMMAND_grBlendColorRect;
		case 0x43: dc_puts("grDrawFillCircle"); return COMMAND_grDrawFillCircle;
		case 0x44: dc_puts("MHH"); return COMMAND_MHH;
		case 0x45: dc_puts("menuSetCbkInit"); return COMMAND_menuSetCbkInit;
		case 0x46: dc_puts("menuClearCbkInit"); return COMMAND_menuClearCbkInit;
		case 0x47:
			if (*dc.p != ']')
				error_at(dc.p - 2, "command 2F47 not followed by ']'");
			dc_putc(*dc.p++);
			return COMMAND_menu;
		case 0x48: dc_puts("sysOpenShell"); return COMMAND_sysOpenShell;
		case 0x49: dc_puts("sysAddWebMenu"); return COMMAND_sysAddWebMenu;
		case 0x4a: dc_puts("iptSetMoveCursorTime"); return COMMAND_iptSetMoveCursorTime;
		case 0x4b: dc_puts("iptGetMoveCursorTime"); return COMMAND_iptGetMoveCursorTime;
		case 0x4c: dc_puts("grBlt"); return COMMAND_grBlt;
		case 0x4d: dc_puts("LXWT"); return COMMAND_LXWT;
		case 0x4e: dc_puts("LXWS"); return COMMAND_LXWS;
		case 0x4f: dc_puts("LXWE"); return COMMAND_LXWE;
		case 0x50: dc_puts("LXWH"); return COMMAND_LXWH;
		case 0x51: dc_puts("LXWHH"); return COMMAND_LXWHH;
		case 0x52: dc_puts("sysGetOSName"); return COMMAND_sysGetOSName;
		case 0x53: dc_puts("patchEC"); return COMMAND_patchEC;
		case 0x54: dc_puts("mathSetClipWindow"); return COMMAND_mathSetClipWindow;
		case 0x55: dc_puts("mathClip"); return COMMAND_mathClip;
		case 0x56: dc_puts("LXF"); return COMMAND_LXF;
		case 0x57: dc_puts("strInputDlg"); return COMMAND_strInputDlg;
		case 0x58: dc_puts("strCheckASCII"); return COMMAND_strCheckASCII;
		case 0x59: dc_puts("strCheckSJIS"); return COMMAND_strCheckSJIS;
		case 0x5a: dc_puts("strMessageBox"); return COMMAND_strMessageBox;
		case 0x5b: dc_puts("strMessageBoxStr"); return COMMAND_strMessageBoxStr;
		case 0x5c: dc_puts("grCopyUseAMapUseA"); return COMMAND_grCopyUseAMapUseA;
		case 0x5d: dc_puts("grSetCEParam"); return COMMAND_grSetCEParam;
		case 0x5e: dc_puts("grEffectMoveView"); return COMMAND_grEffectMoveView;
		case 0x5f: dc_puts("cgSetCacheSize"); return COMMAND_cgSetCacheSize;
		case 0x60: return COMMAND_dllCall;
		case 0x61: dc_puts("gaijiSet"); return COMMAND_gaijiSet;
		case 0x62: dc_puts("gaijiClearAll"); return COMMAND_gaijiClearAll;
		case 0x63: dc_puts("menuGetLatestSelect"); return COMMAND_menuGetLatestSelect;
		case 0x64: dc_puts("lnkIsLink"); return COMMAND_lnkIsLink;
		case 0x65: dc_puts("lnkIsData"); return COMMAND_lnkIsData;
		case 0x66: dc_puts("fncSetTable"); return COMMAND_fncSetTable;
		case 0x67: dc_puts("fncSetTableFromStr"); return COMMAND_fncSetTableFromStr;
		case 0x68: dc_puts("fncClearTable"); return COMMAND_fncClearTable;
		case 0x69: dc_puts("fncCall"); return COMMAND_fncCall;
		case 0x6a: dc_puts("fncSetReturnCode"); return COMMAND_fncSetReturnCode;
		case 0x6b: dc_puts("fncGetReturnCode"); return COMMAND_fncGetReturnCode;
		case 0x6c: dc_puts("msgSetOutputFlag"); return COMMAND_msgSetOutputFlag;
		case 0x6d: dc_puts("saveDeleteFile"); return COMMAND_saveDeleteFile;
		case 0x6e: dc_puts("wav3DSetUseFlag"); return COMMAND_wav3DSetUseFlag;
		case 0x6f: dc_puts("wavFadeVolume"); return COMMAND_wavFadeVolume;
		case 0x70: dc_puts("patchEMEN"); return COMMAND_patchEMEN;
		case 0x71: dc_puts("wmenuEnableMsgSkip"); return COMMAND_wmenuEnableMsgSkip;
		case 0x72: dc_puts("winGetFlipFlag"); return COMMAND_winGetFlipFlag;
		case 0x73: dc_puts("cdGetMaxTrack"); return COMMAND_cdGetMaxTrack;
		case 0x74: dc_puts("dlgErrorOkCancel"); return COMMAND_dlgErrorOkCancel;
		case 0x75: dc_puts("menuReduce"); return COMMAND_menuReduce;
		case 0x76: dc_puts("menuGetNumof"); return COMMAND_menuGetNumof;
		case 0x77: dc_puts("menuGetText"); return COMMAND_menuGetText;
		case 0x78: dc_puts("menuGoto"); return COMMAND_menuGoto;
		case 0x79: dc_puts("menuReturnGoto"); return COMMAND_menuReturnGoto;
		case 0x7a: dc_puts("menuFreeShelterDIB"); return COMMAND_menuFreeShelterDIB;
		case 0x7b: dc_puts("msgFreeShelterDIB"); return COMMAND_msgFreeShelterDIB;
		case 0x7c: return COMMAND_ainMsg;
		case 0x7d: return COMMAND_ainH;
		case 0x7e: return COMMAND_ainHH;
		case 0x7f: return COMMAND_ainX;
		case 0x80: dc_puts("dataSetPointer"); return COMMAND_dataSetPointer;
		case 0x81: dc_puts("dataGetWORD"); return COMMAND_dataGetWORD;
		case 0x82: dc_puts("dataGetString"); return COMMAND_dataGetString;
		case 0x83: dc_puts("dataSkipWORD"); return COMMAND_dataSkipWORD;
		case 0x84: dc_puts("dataSkipString"); return COMMAND_dataSkipString;
		case 0x85: dc_puts("varGetNumof"); return COMMAND_varGetNumof;
		case 0x86: dc_puts("patchG0"); return COMMAND_patchG0;
		case 0x87: dc_puts("regReadString"); return COMMAND_regReadString;
		case 0x88: dc_puts("fileCheckExist"); return COMMAND_fileCheckExist;
		case 0x89: dc_puts("timeCheckCurDate"); return COMMAND_timeCheckCurDate;
		case 0x8a: dc_puts("dlgManualProtect"); return COMMAND_dlgManualProtect;
		case 0x8b: dc_puts("fileCheckDVD"); return COMMAND_fileCheckDVD;
		case 0x8c: dc_puts("sysReset"); return COMMAND_sysReset;
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

static bool inline_menu_string(void) {
	const uint8_t *end = dc.p;
	while (*end == 0x20 || *end > 0x80)
		end = advance_char(end);
	if (*end != '$')
		return false;

	dc_put_string_n((const char *)dc.p, end - dc.p, STRING_EXPAND);
	dc.p = end;
	dc_putc(*dc.p++);  // '$'
	return true;
}

static void ain_msg(const char *cmd, const char *args) {
	uint32_t id = le32(dc.p);
	dc.p += 4;
	if (!dc.ain)
		error("System39.ain is required to decompile this file.");
	if (!dc.ain->messages || id >= dc.ain->messages->len)
		error_at(dc.p - 6, "invalid message id %d", id);

	if (cmd) {
		dc_puts(cmd);
		arguments(args);
		if (*(char *)dc.ain->messages->data[id])
			error_at(dc.p - 6, "Unexpected non-empty message id %d", id);
	} else {
		dc_putc('\'');
		dc_put_string(dc.ain->messages->data[id], '\0', STRING_ESCAPE);
		dc_putc('\'');
	}
}

static void dll_call(void) {
	uint32_t dll_id = le32(dc.p);
	uint32_t func_id = le32(dc.p + 4);
	dc.p += 8;
	if (!dc.ain)
		error("System39.ain is required to decompile this file.");
	if (dll_id >= dc.ain->dlls->vals->len)
		error_at(dc.p - 8, "DLL id out of range (dll:%d, func:%d)", dll_id, func_id);

	Vector *funcs = dc.ain->dlls->vals->data[dll_id];
	if (func_id >= funcs->len)
		error_at(dc.p - 8, "Function id out of range (dll:%d, func:%d)", dll_id, func_id);
	DLLFunc *f = funcs->data[func_id];

	dc_puts(dc.ain->dlls->keys->data[dll_id]);
	dc_putc('.');
	dc_puts(f->name);

	const char *sep = " ";
	for (int i = 0; i < f->argc; i++) {
		switch (f->argtypes[i]) {
		case HEL_pword:
		case HEL_int:
		case HEL_IString:
			dc_puts(sep);
			sep = ", ";
			cali(false);
			break;
		case HEL_ISurface:
		case HEL_IWinMsg:
		case HEL_ITimer:
		case HEL_IUI:
		case HEL_ISys3xDIB:
		case HEL_ISys3xCG:
		case HEL_ISys3xStringTable:
		case HEL_ISys3xSystem:
		case HEL_ISys3xMusic:
		case HEL_ISys3xMsgString:
		case HEL_ISys3xInputDevice:
		case HEL_ISys3x:
			dc.p += 2;  // ??
			break;
		case HEL_IConstString:
			dc_puts(sep);
			sep = ", ";
			dc_putc('"');
			dc.p = dc_put_string((const char *)dc.p, '\0', STRING_ESCAPE);
			dc_putc('"');
			break;
		default:
			error_at(dc.p, "argtype %d not implemented", f->argtypes[i]);
		}
	}
	dc_putc(':');
}

static void decompile_page(int page) {
	Sco *sco = dc.scos->data[page];
	dc.page = page;
	dc.p = sco->data + sco->hdrsize;
	dc.indent = 1;
	bool in_menu_item = false;
	Vector *branch_end_stack = new_vec();
	uint32_t next_funcall_top_candidate = 0;

	// Skip the "ZU 1:" command of unicode SCO.
	if (config.utf8_input && page == 0 && !memcmp(dc.p, "ZU\x41\x7f", 4))
		dc.p += 4;

	while (dc.p < sco->data + sco->filesize) {
		int topaddr = dc.p - sco->data;
		uint8_t mark = sco->mark[dc.p - sco->data];
		while (branch_end_stack->len > 0 && stack_top(branch_end_stack) == topaddr) {
			stack_pop(branch_end_stack);
			dc.indent--;
			assert(dc.indent > 0);
			indent();
			dc_puts("}\n");
			next_funcall_top_candidate = 0;
		}
		uint32_t funcall_top_candidate = (mark & ~CODE) ? 0 : next_funcall_top_candidate;
		next_funcall_top_candidate = 0;
		if (mark & FUNC_TOP)
			func_labels(page, dc.p - sco->data);
		if (mark & LABEL) {
			print_address();
			dc_printf("*L_%05x:\n", dc.p - sco->data);
		}

		if ((mark & TYPE_MASK) == DATA_TABLE) {
			if (data_table())
				continue;
			mark |= DATA;
		}
		if (mark & DATA) {
			const uint8_t *data_end = dc.p + 1;
			for (; data_end < sco->data + sco->filesize; data_end++) {
				if (sco->mark[data_end - sco->data] & ~DATA)
					break;
			}
			data_block(data_end);
			continue;
		}
		if ((mark & TYPE_MASK) == ELSE_IF && !dc.disable_else) {
			assert(*dc.p == '@');
			assert(dc.p[5] == '{');
			dc.p += 6;
			dc.indent--;
			stack_pop(branch_end_stack);
			indent();
			dc_puts("} else if {");
			conditional(branch_end_stack);
			dc_putc('\n');
			continue;
		}
		if ((mark & TYPE_MASK) == ELSE && !dc.disable_else) {
			assert(*dc.p == '@');
			dc.p += 5;
			if (le32(dc.p - 4) != dc_addr()) {
				dc.indent--;
				indent();
				dc_puts("} else {\n");
				dc.indent++;
			}
			continue;
		}
		if (*dc.p == '>')
			dc.indent--;
		indent();
		if (*dc.p == 0 || *dc.p == 0x20 || *dc.p > 0x80) {
			uint8_t *mark_at_string_start = &sco->mark[dc.p - sco->data];
			dc_putc('\'');
			const uint8_t *begin = dc.p;
			while (*dc.p == 0x20 || *dc.p > 0x80) {
				dc.p = advance_char(dc.p);
				if (*mark_at(dc.page, dc_addr()) != 0)
					break;
			}
			dc_put_string_n((const char *)begin, dc.p - begin, STRING_ESCAPE | STRING_EXPAND);
			dc_puts("'\n");
			if (*dc.p == '\0') {
				// String data in code area. This happens when the author
				// accidentally use double quotes instead of single quotes.
				*mark_at_string_start |= DATA;
				dc.p++;
			} else {
				*mark_at_string_start |= CODE;
			}
			continue;
		}
		sco->mark[dc.p - sco->data] |= CODE;
		if ((mark & TYPE_MASK) == FOR_START) {
			assert(*dc.p == '!');
			dc.p++;
			dc_putc('<');
			cali(true);
			dc_puts(", ");
			cali(false);
			dc_puts(", ");
			assert(*dc.p == '<');
			dc.p++;
			for_loop();
			dc_putc('\n');
			continue;
		}
		if ((mark & TYPE_MASK) == WHILE_START) {
			assert(*dc.p == '{');
			dc.p++;
			dc_puts("<@");
			conditional(branch_end_stack);
			dc_putc('\n');
			continue;
		}
		if ((mark & TYPE_MASK) == FUNCALL_TOP) {
			if (funcall_with_args()) {
				dc_putc('\n');
				continue;
			}
		}
		int cmd = get_command();
		switch (cmd) {
		case '!':  // Assign
			next_funcall_top_candidate = funcall_top_candidate ? funcall_top_candidate : dc_addr() - 1;
			// fall through
		case 0x10: case 0x11: case 0x12: case 0x13:
		case 0x14: case 0x15: case 0x16: case 0x17:
			{
				Cali *node = cali(true);
				// Array reference cannot be a function argument.
				if (node->type == NODE_AREF)
					next_funcall_top_candidate = 0;
				dc_putc(' ');
				if (cmd != '!')
					dc_putc("+-*/%&|^"[cmd - 0x10]);
				dc_puts(": ");
				cali(false);
				dc_putc('!');
			}
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
			page_name(cmd);
			dc_putc(':');
			break;

		case '%':  // Page call / return
			page_name(cmd);
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
				if (inline_menu_string())
					in_menu_item = false;
			}
			break;

		case '#':  // Data table address
			data_table_addr();
			break;

		case '~':  // Function call
			funcall(funcall_top_candidate);
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
		case CMD2('L', 'E'): arguments("nsee"); break;
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
		case CMD2('Q', 'E'): arguments("nsee"); break;
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
		case CMD2('S', 'R'):
			if (*dc.p < 0x40) {
				arguments("nv");
			} else {
				dc.old_SR = true;
				arguments("ev");
			}
			break;
		case CMD2('S', 'S'): arguments("e"); break;
		case CMD2('S', 'T'): arguments("e"); break;
		case CMD2('S', 'U'): arguments("vv"); break;
		case CMD2('S', 'W'): arguments("veee"); break;
		case CMD2('S', 'X'):
			subcommand_num();  // device
			dc_puts(", ");
			switch (subcommand_num()) {
			case 1:
				dc_puts(", "); arguments("eee"); break;
			case 2:
			case 4:
				dc_puts(", "); arguments("v"); break;
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
		case COMMAND_msg:
			dc.disable_ain_message = true;
			dc_putc('\'');
			dc.p = dc_put_string((const char *)dc.p, '\0', STRING_ESCAPE);
			dc_putc('\'');
			break;
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
		case COMMAND_menu: break;
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
		case COMMAND_dllCall: dll_call(); break;
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
		case COMMAND_ainMsg: ain_msg(NULL, NULL); break;
		case COMMAND_ainH: ain_msg("H", "ne"); break;
		case COMMAND_ainHH: ain_msg("HH", "ne"); break;
		case COMMAND_ainX: ain_msg("X", "e"); break;
		case COMMAND_dataSetPointer: arguments("F"); break;
		case COMMAND_dataGetWORD: arguments("ve"); break;
		case COMMAND_dataGetString: arguments("ee"); break;
		case COMMAND_dataSkipWORD: arguments("e"); break;
		case COMMAND_dataSkipString: arguments("e"); break;
		case COMMAND_varGetNumof: arguments("v"); break;
		case COMMAND_patchG0: arguments("e"); break;
		case COMMAND_regReadString: arguments("eeev"); break;
		case COMMAND_fileCheckExist: arguments("ev"); break;
		case COMMAND_timeCheckCurDate: arguments("eeev"); break;
		case COMMAND_dlgManualProtect: arguments("oo"); break;
		case COMMAND_fileCheckDVD: arguments("oeeov"); break;
		case COMMAND_sysReset: arguments(""); break;
		default:
		unknown_command:
			if (dc.out)
				error("%s:%x: unknown command '%.*s'", to_utf8(sco->sco_name), topaddr, dc_addr() - topaddr, sco->data + topaddr);
			// If we're in the analyze phase, retry as a data block.
			dc.p = sco->data + topaddr;
			sco->mark[topaddr] &= ~CODE;
			sco->mark[topaddr] |= DATA;
			break;
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

char *missing_adv_name(int page) {
	char buf[32];
	sprintf(buf, "_missing%d.adv", page);
	return strdup(buf);
}

static void create_adv_for_missing_sco(const char *outdir, int page) {
	dc.out = checked_fopen(path_join(outdir, missing_adv_name(page)), "w+");

	// Set ald_volume to zero so that xsys35c will not generate ALD for this.
	fprintf(dc.out, "pragma ald_volume 0:\n");

	for (HashItem *i = hash_iterate(dc.functions, NULL); i; i = hash_iterate(dc.functions, i)) {
		Function *f = (Function *)i->val;
		if (f->page - 1 != page)
			continue;
		fprintf(dc.out, "pragma address 0x%x:\n", f->addr);
		defun(f, f->name);
		dc_putc('\n');
		if (f->aliases) {
			for (int i = 0; i < f->aliases->len; i++) {
				defun(f, f->aliases->data[i]);
				dc_putc('\n');
			}
		}
	}

	if (!config.utf8_input && config.utf8_output)
		convert_to_utf8(dc.out);
	fclose(dc.out);
}

static void write_config(const char *path, const char *ald_basename) {
	if (dc.scos->len == 0)
		return;
	FILE *fp = checked_fopen(path, "w+");
	if (ald_basename)
		fprintf(fp, "ald_basename = %s\n", ald_basename);
	if (dc.ain) {
		fprintf(fp, "output_ain = %s\n", dc.ain->filename);
		if (dc.ain->version != 1)
			fprintf(fp, "ain_version = %d\n", dc.ain->version);
	}

	fputs("hed = xsys35dc.hed\n", fp);
	fputs("variables = variables.txt\n", fp);
	if (dc.disable_else)
		fputs("disable_else = true\n", fp);
	if (dc.old_SR)
		fputs("old_SR = true\n", fp);

	if (dc.ain) {
		fputs("sys_ver = 3.9\n", fp);
		if (dc.disable_ain_message)
			fputs("disable_ain_message = true\n", fp);
		if (dc.disable_ain_variable)
			fputs("disable_ain_variable = true\n", fp);
	} else {
		Sco *sco = dc.scos->data[0];
		switch (sco->version) {
		case SCO_S350: fputs("sys_ver = S350\n", fp); break;
		case SCO_S351: fputs("sys_ver = 3.5\n", fp); break;
		case SCO_153S: fputs("sys_ver = 153S\n", fp); break;
		case SCO_S360: fputs("sys_ver = 3.6\n", fp); break;
		case SCO_S380: fputs("sys_ver = 3.8\n", fp); break;
		}
	}

	fprintf(fp, "encoding = %s\n", config.utf8_output ? "utf8" : "sjis");
	if (config.utf8_input)
		fprintf(fp, "unicode = true\n");

	fclose(fp);
}

static void write_hed(const char *path, Map *dlls) {
	FILE *fp = checked_fopen(path, "w+");
	fputs("#SYSTEM35\n", fp);
	for (int i = 0; i < dc.scos->len; i++) {
		Sco *sco = dc.scos->data[i];
		fprintf(fp, "%s\n", sco ? sco->src_name : missing_adv_name(i));
	}

	if (dlls && dlls->keys->len) {
		fputs("\n#DLLHeader\n", fp);
		for (int i = 0; i < dlls->keys->len; i++) {
			Vector *funcs = dlls->vals->data[i];
			fprintf(fp, "%s.%s\n", (char *)dlls->keys->data[i], funcs->len ? "HEL" : "DLL");
		}
	}
	if (!config.utf8_input && config.utf8_output)
		convert_to_utf8(fp);
	fclose(fp);
}

static void write_variables(const char *path) {
	FILE *fp = checked_fopen(path, "w+");
	for (int i = 0; i < dc.variables->len; i++) {
		const char *s = dc.variables->data[i];
		fprintf(fp, "%s\n", s ? s : "");
	}
	if (!config.utf8_input && config.utf8_output)
		convert_to_utf8(fp);
	fclose(fp);
}

noreturn void error_at(const uint8_t *pos, char *fmt, ...) {
	Sco *sco = dc.scos->data[dc.page];
	assert(sco->data <= pos);
	assert(pos < sco->data + sco->filesize);;
	fprintf(stderr, "%s:%x: ", to_utf8(sco->sco_name), (unsigned)(pos - sco->data));
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
	fprintf(stderr, "Warning: %s:%x: ", to_utf8(sco->sco_name), (unsigned)(pos - sco->data));
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fputc('\n', stderr);
}

void decompile(Vector *scos, Ain *ain, const char *outdir, const char *ald_basename) {
	memset(&dc, 0, sizeof(dc));
	dc.scos = scos;
	dc.ain = ain;
	dc.variables = (ain && ain->variables) ? ain->variables : new_vec();
	dc.functions = (ain && ain->functions) ? ain->functions : new_function_hash();
	dc.disable_ain_variable = ain && !ain->variables;

	// Preprocess
	if (config.verbose)
		puts("Preprocessing...");
	preprocess(scos, ain);

	// Analyze
	bool done = false;
	while (!done) {
		done = true;
		for (int i = 0; i < scos->len; i++) {
			Sco *sco = scos->data[i];
			if (!sco || sco->analyzed)
				continue;
			if (config.verbose)
				printf("Analyzing %s (page %d)...\n", to_utf8(sco->sco_name), i);
			done = false;
			sco->analyzed = true;
			decompile_page(i);
		}
	}

	// Decompile
	for (int i = 0; i < scos->len; i++) {
		Sco *sco = scos->data[i];
		if (!sco) {
			create_adv_for_missing_sco(outdir, i);
			continue;
		}
		if (config.verbose)
			printf("Decompiling %s (page %d)...\n", to_utf8(sco->sco_name), i);
		dc.out = checked_fopen(path_join(outdir, to_utf8(sco->src_name)), "w+");
		if (sco->ald_volume != 1)
			fprintf(dc.out, "pragma ald_volume %d:\n", sco->ald_volume);
		decompile_page(i);
		if (!config.utf8_input && config.utf8_output)
			convert_to_utf8(dc.out);
		fclose(dc.out);
	}

	if (config.verbose)
		puts("Generating config files...");

	write_config(path_join(outdir, "xsys35c.cfg"), ald_basename);
	write_hed(path_join(outdir, "xsys35dc.hed"), ain ? ain->dlls : NULL);
	write_variables(path_join(outdir, "variables.txt"));
	if (ain && ain->dlls)
		write_hels(ain->dlls, outdir);

	if (config.verbose)
		puts("Done!");
}
