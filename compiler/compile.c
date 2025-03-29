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
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static Compiler *compiler;
static const char *menu_item_start;
static bool compiling;
static Vector *branch_end_stack;

typedef enum {
	VARIABLE,
	CONST,
} SymbolType;

typedef struct {
	SymbolType type;
	int value;  // variable index or constant value
} Symbol;

static Symbol *new_symbol(SymbolType type, int value) {
	Symbol *s = calloc(1, sizeof(Symbol));
	s->type = type;
	s->value = value;
	return s;
}

static Map *labels;

static Buffer *out;

static int lookup_var(char *var, bool create) {
	Symbol *sym = hash_get(compiler->symbols, var);
	if (sym) {
		switch (sym->type) {
		case VARIABLE:
			return sym->value;
		case CONST:
			if (create)
				error_at(input - strlen(var), "'%s' is already defined as a constant", var);
			return -1;
		}
	}

	if (!create)
		return -1;
	sym = new_symbol(VARIABLE, compiler->variables->len);
	vec_push(compiler->variables, var);
	hash_put(compiler->symbols, var, sym);
	return sym->value;
}

static void expr(void);
static void expr_equal(void);
static void commands(void);

static void variable(char *id, bool create) {
	int var = lookup_var(id, create);
	if (compiling && var < 0)
		error_at(input - strlen(id), "Undefined variable '%s'", id);
	if (consume('[')) {
		emit(out, 0xc0);
		emit(out, OP_C0_INDEX);
		emit_word_be(out, var);
		expr();
		expect(']');
	} else {
		emit_var(out, var);
	}
}

static void number(void) {
	emit_number(out, get_number());
}

// prim ::= '(' equal ')' | number | '#' filename | const | var
static void expr_prim(void) {
	if (consume('(')) {
		expr_equal();
		expect(')');
	} else if (isdigit(next_char())) {
		number();
	} else if (consume('#')) {
		const char *top = input;
		char *fname = get_filename();
		for (int i = 0; i < compiler->src_paths->len; i++) {
			if (!strcasecmp(fname, basename_utf8(compiler->src_paths->data[i]))) {
				emit_number(out, i);
				return;
			}
		}
		error_at(top, "reference to unknown source file: '%s'", fname);
	} else {
		char *id = get_identifier();
		if (!strcmp(id, "__LINE__")) {
			emit_number(out, input_line);
		} else {
			Symbol *sym = hash_get(compiler->symbols, id);
			if (sym && sym->type == CONST)
				emit_number(out, sym->value);
			else
				variable(id, false);
		}
	}
}

// mul ::= prim ('*' prim | '/' prim | '%' prim)*
static void expr_mul(void) {
	expr_prim();
	for (;;) {
		if (consume('*')) {
			expr_prim();
			emit(out, OP_MUL);
		} else if (consume('/')) {
			expr_prim();
			emit(out, OP_DIV);
		} else if (consume('%')) {
			expr_prim();
			emit(out, 0xc0);
			emit(out, OP_C0_MOD);
		} else {
			break;
		}
	}
}

// add ::= mul ('+' mul | '-' mul)*
static void expr_add(void) {
	expr_mul();
	for (;;) {
		if (consume('+')) {
			expr_mul();
			emit(out, OP_ADD);
		} else if (consume('-')) {
			expr_mul();
			emit(out, OP_SUB);
		} else {
			break;
		}
	}
}

// bit ::= add ('&' add | '|' add | '^' add)*
static void expr_bit(void) {
	expr_add();
	for (;;) {
		if (consume('&')) {
			expr_add();
			emit(out, OP_AND);
		} else if (consume('|')) {
			expr_add();
			emit(out, OP_OR);
		} else if (consume('^')) {
			expr_add();
			emit(out, OP_XOR);
		} else {
			break;
		}
	}
}

// compare ::= bit ('<' bit | '>' bit | '<=' bit | '>=' bit)*
static void expr_compare(void) {
	expr_bit();
	for (;;) {
		int op = 0;
		if (consume('<')) {
			op = OP_LT;
			if (consume('='))
				op = OP_C0_LE;
		} else if (consume('>')) {
			op = OP_GT;
			if (consume('='))
				op = OP_C0_GE;
		}
		if (!op)
			break;
		expr_bit();
		if (op == OP_C0_LE || op == OP_C0_GE)
			emit(out, 0xc0);
		emit(out, op);
	}
}

// equal ::= compare ('=' compare | '\' compare | '$' compare)*
static void expr_equal(void) {
	expr_compare();
	for (;;) {
		if (consume('=')) {
			expr_compare();
			emit(out, OP_EQ);
		} else if (consume('\\')) {
			expr_compare();
			emit(out, OP_NE);
		} else if (consume('$')) {
			expr_compare();
		} else {
			break;
		}
	}
}

// expr ::= equal
static void expr(void) {
	expr_equal();
	emit(out, OP_END);
}

// 'const' 'word' identifier '=' constexpr (',' identifier '=' constexpr)* ':'
static void define_const(void) {
	if (!consume_keyword("word"))
		error_at(input, "unknown const type");
	do {
		const char *top = input;
		char *id = get_identifier();
		consume('=');
		int val = get_number();  // TODO: Allow expressions
		if (!compiling) {
			Symbol *sym = hash_get(compiler->symbols, id);
			if (sym) {
				switch (sym->type) {
				case VARIABLE:
					error_at(top, "'%s' is already defined as a variable", id);
				case CONST:
					error_at(top, "constant '%s' redefined", id);
				}
			}
			hash_put(compiler->symbols, id, new_symbol(CONST, val));
		}
	} while (consume(','));
	expect(':');
}

static Label *lookup_label(char *id) {
	Label *l = map_get(labels, id);
	if (!l) {
		l = calloc(1, sizeof(Label));
		l->source_loc = input - strlen(id);
		map_put(labels, id, l);
	}
	return l;
}

static void add_label(void) {
	char *id = get_label();
	if (!compiling)
		return;
	Label *l = lookup_label(id);
	if (l->addr)
		error_at(input - strlen(id), "label '%s' redefined", id);
	l->addr = current_address(out);

	while (l->hole_addr)
		l->hole_addr = swap_dword(out, l->hole_addr, l->addr);
}

static Label *label(void) {
	char *id = get_label();
	if (!compiling)
		return NULL;
	Label *l = lookup_label(id);
	if (!l->addr) {
		emit_dword(out, l->hole_addr);
		l->hole_addr = current_address(out) - 4;
	} else {
		assert(!l->hole_addr);
		emit_dword(out, l->addr);
	}
	return l;
}

// defun ::= '**' name (var (',' var)*)? ':'
static void defun(void) {
	const char *top = input;
	char *name = get_label();

	if (!compiling) {
		// First pass - create a function record and store parameter info
		if (hash_get(compiler->functions, name))
			error_at(top, "function '%s' redefined", name);
		Function *func = calloc(1, sizeof(Function));
		func->name = name;
		func->params = new_vec();

		bool needs_comma = false;
		while (!consume(':')) {
			if (needs_comma)
				expect(',');
			needs_comma = true;
			vec_push(func->params, get_identifier());
		}
		hash_put(compiler->functions, name, func);
		return;
	}

	// Second pass - resolve function address
	Function *func = hash_get(compiler->functions, name);
	assert(func);
	assert(!func->resolved);

	const int page = input_page + 1;
	const uint32_t addr = current_address(out);
	while (func->addr) {
		if (func->page == page) {
			func->page = swap_word(out, func->addr, page);
			func->addr = swap_dword(out, func->addr + 2, addr);
		} else {
			Buffer *sco = compiler->scos[func->page - 1].buf;
			assert(sco);
			uint8_t *p = sco->buf + func->addr;
			func->page = p[0] | p[1] << 8;
			func->addr = p[2] | p[3] << 8 | p[4] << 16 | p[5] << 24;
			p[0] = page & 0xff;
			p[1] = page >> 8;
			p[2] = addr & 0xff;
			p[3] = addr >> 8 & 0xff;
			p[4] = addr >> 16 & 0xff;
			p[5] = addr >> 24 & 0xff;
		}
	}
	func->page = page;
	func->addr = addr;
	func->resolved = true;

	// Check if all parameters are defined as variables.
	for (int i = 0; i < func->params->len; i++) {
		if (i != 0)
			expect(',');
		char *id = get_identifier();
		if (lookup_var(id, false) < 0)
			error_at(input - strlen(id), "Undefined variable '%s'", id);
	}
	expect(':');
}

//    funcall ::= '~' name (var (',' var)*)? ':'
//     return ::= '~0' ',' expr ':'
// get-retval ::= '~~' ',' var ':'
static void funcall(void) {
	if (consume('~')) {
		emit(out, '~');
		emit_word(out, 0xffff);
		variable(get_identifier(), false);
		emit(out, OP_END);
		expect(':');
		return;
	}
	const char *top = input;
	char *name = get_label();
	if (!strcmp(name, "0")) {
		emit(out, '~');
		emit_word(out, 0);
		expect(',');
		expr();
		expect(':');
		return;
	}
	if (!compiling) {
		// First pass - skip parameters
		bool needs_comma = false;
		while (!consume(':')) {
			if (needs_comma)
				expect(',');
			needs_comma = true;
			expr();
		}
		return;
	}

	// Second pass
	Function *func = hash_get(compiler->functions, name);
	if (!func)
		error_at(top, "undefined function '%s'", name);
	for (int i = 0; i < func->params->len; i++) {
		emit(out, '!');
		int var = lookup_var(func->params->data[i], false);
		emit_var(out, var);
		if (i == 0)
			consume(',');
		else
			expect(',');
		expr();
	}
	expect(':');

	emit(out, '~');
	emit_word(out, func->page);
	emit_dword(out, func->addr);
	if (!func->resolved) {
		func->page = input_page + 1;
		func->addr = current_address(out) - 6;
	}
}

// numarray ::= '[' ']' | '[' number (',' number)* ']'
static void number_array(void) {
	bool first = true;
	while (!consume(']')) {
		if (!first)
			expect(',');
		first = false;

		const char *top = input;
		int n = get_number();
		if (n > 0xffff)
			error_at(top, "number constant out of range: %d", n);

		if (consume('b'))  // byte constant (xsys35c extension)
			emit(out, n);
		else
			emit_word(out, n);
	}
}

static void dll_arguments(DLLFunc *f) {
	bool need_comma = false;
	for (int i = 0; i < f->argc; i++) {
		switch (f->argtypes[i]) {
		case HEL_pword:
		case HEL_int:
		case HEL_IString:
			if (need_comma)
				expect(',');
			expr();
			need_comma = true;
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
			emit_number(out, 0);
			emit(out, OP_END);
			break;
		case HEL_IConstString:
			if (need_comma)
				expect(',');
			expect('"');
			compile_string(out, '"', false, false);
			emit(out, 0);
			need_comma = true;
			break;
		default:
			error("argtype %d not implemented", f->argtypes[i]);
		}
	}
	expect(':');
}

static int hel_index(const char *dllname) {
	for (int i = 0; i < compiler->dlls->keys->len; i++) {
		Vector *funcs = compiler->dlls->vals->data[i];
		if (funcs->len > 0 && !strcmp(compiler->dlls->keys->data[i], dllname))
			return i;
	}
	return -1;
}

static void dll_call(void) {
	const char *dot = strchr(input, '.');
	assert(dot);
	const char *dllname = strndup_(input, dot - input);
	int dll_index = hel_index(dllname);
	if (dll_index < 0)
		error_at(input, "unknown DLL name '%s'", dllname);
	emit_dword(out, dll_index);
	Vector *funcs = compiler->dlls->vals->data[dll_index];
	input = dot + 1;
	const char *funcname = get_identifier();
	for (int i = 0; i < funcs->len; i++) {
		DLLFunc *f = funcs->data[i];
		if (!strcmp(f->name, funcname)) {
			emit_dword(out, i);
			dll_arguments(f);
			return;
		}
	}
	error_at(dot + 1, "unknown DLL function '%s'", funcname);
}

// Compile command arguments. Directives:
//  e: expression
//  n: number (ascii digits)
//  s: string (colon-terminated)
//  v: variable
//  z: string (zero-terminated)
//  F: function name
static void arguments(const char *sig) {
	if (*sig == 'n') {
		emit(out, get_number());
		if (*++sig)
			consume(',');  // comma between subcommand num and next argument is optional
	}

	while (*sig) {
		const char *top = input;
		switch (*sig) {
		case 'e':
			expr();
			break;
		case 'n':
			emit(out, get_number());
			break;
		case 's':
		case 'z':
			while (isspace(*input))
				input++;  // Do not consume full-width spaces here
			if (*input == '"') {
				expect('"');
				compile_string(out, '"', false, false);
			} else {
				compile_bare_string(out);
			}
			emit(out, *sig == 'z' ? 0 : ':');
			break;
		case 'o': // obfuscated string
			{
				emit(out, 0);
				expect('"');
				int start = current_address(out);
				compile_string(out, '"', false, false);
				for (int i = start; i < current_address(out); i++) {
					uint8_t b = get_byte(out, i);
					set_byte(out, i, b >> 4 | b << 4);
				}
				emit(out, 0);
			}
			break;
		case 'v':
			variable(get_identifier(), false);
			emit(out, OP_END);
			break;
		case 'F':
			{
				char *name = get_label();
				if (compiling) {
					Function *func = hash_get(compiler->functions, name);
					if (!func)
						error_at(top, "undefined function '%s'", name);
					emit_word(out, func->page);
					emit_dword(out, func->addr);
					if (!func->resolved) {
						func->page = input_page + 1;
						func->addr = current_address(out) - 6;
					}
				}
			}
			break;
		default:
			error("BUG: invalid arguments() template : %c", *sig);
		}
		if (*++sig) {
			if (consume(':'))
				error_at(input - 1, "too few arguments");
			expect(',');
		}
	}
	if (consume(','))
		error_at(input - 1, "too many arguments");
	expect(':');
}

// assign ::= '!' var [+-*/%&|^]? ':' expr '!'
static void assign(void) {
	int op = current_address(out);
	emit(out, '!');
	variable(get_identifier(), true);
	if (consume('+')) {
		set_byte(out, op, 0x10);
	} else if (consume('-')) {
		set_byte(out, op, 0x11);
	} else if (consume('*')) {
		set_byte(out, op, 0x12);
	} else if (consume('/')) {
		set_byte(out, op, 0x13);
	} else if (consume('%')) {
		set_byte(out, op, 0x14);
	} else if (consume('&')) {
		set_byte(out, op, 0x15);
	} else if (consume('|')) {
		set_byte(out, op, 0x16);
	} else if (consume('^')) {
		set_byte(out, op, 0x17);
	}
	expect(':');
	expr();
	expect('!');
}

// conditional ::= '{' expr ':' commands '}'
static void conditional(void) {
	emit(out, '{');
	expr();
	expect(':');
	int hole = current_address(out);
	emit_dword(out, 0);

	if (branch_end_stack) {
		stack_push(branch_end_stack, hole);
		return;
	}

	commands();
	expect('}');
	if (config.sys_ver >= SYSTEM38 && !config.disable_else) {
		emit(out, '@'); // Label jump
		emit_dword(out, 0);
		swap_dword(out, hole, current_address(out));
		hole = current_address(out) - 4;
		if (consume_keyword("else")) {
			if (consume_keyword("if")) {
				expect('{');
				conditional();
			} else {
				expect('{');
				commands();
				expect('}');
			}
		}
	}
	swap_dword(out, hole, current_address(out));
}

// while-loop ::= '<@' expr ':' commands '>'
static void while_loop(void) {
	int loop_addr = current_address(out);
	emit(out, '{');
	expr();
	expect(':');
	int end_hole = current_address(out);
	emit_dword(out, 0);

	commands();

	expect('>');
	emit(out, '>');
	emit_dword(out, loop_addr);

	swap_dword(out, end_hole, current_address(out));
}

// for-loop ::= '<' var ',' expr ',' expr ',' expr ',' expr ':' commands '>'
static void for_loop(void) {
	emit(out, '!');
	int var_begin = current_address(out);
	variable(get_identifier(), true);  // for-loop can define a variable.
	int var_end = current_address(out);
	expect(',');

	expr();  // start
	expect(',');

	emit(out, '<');
	emit(out, 0x00);
	int loop_addr = current_address(out);
	emit(out, '<');
	emit(out, 0x01);

	int end_hole = current_address(out);
	emit_dword(out, 0);

	// Copy the opcode for the variable.
	for (int i = var_begin; i < var_end; i++)
		emit(out, get_byte(out, i));
	emit(out, OP_END);

	expr();  // end
	expect(',');
	expr();  // sign
	expect(',');
	expr();  // step
	expect(':');

	commands();

	expect('>');
	emit(out, '>');
	emit_dword(out, loop_addr);

	swap_dword(out, end_hole, current_address(out));
}

static void pragma(void) {
	if (consume_keyword("ald_volume")) {
		compiler->scos[input_page].ald_volume = get_number();
		expect(':');
	} else if (consume_keyword("address")) {
		int address = get_number();
		if (out) {
			if (out->len > address)
				out->len = address;
			while (out->len < address)
				emit(out, 0);
			// Addresses in LINE debug table must be monotonically increasing,
			// but address pragma breaks this condition. So clear the LINE info
			// for this page.
			if (compiler->dbg_info)
				debug_line_reset(compiler->dbg_info);
		}
		expect(':');
	} else {
		error_at(input, "unknown pragma");
	}
}

static int subcommand_num(void) {
	int n = get_number();
	emit(out, n);
	consume(',');
	return n;
}

static bool command(void) {
	skip_whitespaces();
	if (out && compiler->dbg_info)
		debug_line_add(compiler->dbg_info, input_line, current_address(out));

	const char *command_top = input;
	int cmd = get_command(out);

	switch (cmd) {
	case '\0':
		return false;

	case '\x1a': // DOS EOF
		break;

	case '\'': // Message
		switch (config.sys_ver) {
		case SYSTEM39:
			if (use_ain_message()) {
				emit_command(out, COMMAND_ainMsg);
				compile_message(compiler->msg_buf);
				emit_dword(out, compiler->msg_count++);
				break;
			}
			// fall through
		case SYSTEM38:
			emit_command(out, COMMAND_msg);
			compile_message(out);
			break;
		default:
			compile_string(out, '\'', config.sys_ver == SYSTEM35, true);
			break;
		}
		break;

	case '!':  // Assign
		assign();
		break;

	case '{':  // Branch
		conditional();
		break;

	case '}':
		if (branch_end_stack && branch_end_stack->len > 0) {
			expect('}');
			swap_dword(out, stack_top(branch_end_stack), current_address(out));
			stack_pop(branch_end_stack);
		} else {
			return false;
		}
		break;

	case '*':  // Label or function definition
		if (consume('*')) {
			defun();
		} else {
			add_label();
			expect(':');
		}
		break;

	case '@':  // Label jump
		emit(out, cmd);
		label();
		expect(':');
		break;

	case '\\': // Label call
		emit(out, cmd);
		if (consume('0'))
			emit_dword(out, 0);  // Return
		else {
			Label *l = label();
			if (l)
				l->is_function = true;
		}
		expect(':');
		break;

	case '&':  // Page jump
		emit(out, cmd);
		expr();
		expect(':');
		break;

	case '%':  // Page call / return
		emit(out, cmd);
		expr();
		expect(':');
		break;

	case '<':  // Loop
		if (consume('@'))
			while_loop();
		else
			for_loop();
		break;

	case '>':
		return false;

	case ']':  // Menu
		if (config.sys_ver >= SYSTEM38)
			emit_command(out, COMMAND_menu);
		emit(out, cmd);
		break;

	case '$':  // Menu item
		emit(out, cmd);
		if (menu_item_start) {
			menu_item_start = NULL;
			break;
		}
		label();
		expect('$');
		if (!isascii(*input)) {
			compile_string(out, '$', config.sys_ver == SYSTEM35, true);
			emit(out, '$');
		} else {
			menu_item_start = command_top;
		}
		break;

	case '#':  // Data table address
		emit(out, cmd);
		label();
		expect(',');
		expr();
		expect(':');
		break;

	case '_':  // Label address as data
		label();
		expect(':');
		break;

	case '"':  // String data
		compile_string(out, '"', config.sys_ver == SYSTEM35, false);
		emit(out, 0);
		break;

	case '[':  // Data
		number_array();
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
		{
			int op = current_address(out);
			emit(out, 0);
			expr();
			if (consume(',')) {
				set_byte(out, op, 1);
				expr();
			}
			consume(':');
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
	case CMD3('L', 'X', 'X'): arguments("eev"); break;
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
	case CMD2('S', 'R'): arguments(config.sys_ver == SYSTEM35 || config.old_SR ? "ev" : "nv"); break;
	case CMD2('S', 'S'): arguments("e"); break;
	case CMD2('S', 'T'): arguments("e"); break;
	case CMD2('S', 'U'): arguments("vv"); break;
	case CMD2('S', 'V'): arguments("ee"); break;
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
	case CMD2('Z', 'U'):
		get_number();
		expect(':');
		if (!compiling)
			warn_at(command_top, "Warning: The ZU command is deprecated. Now it is not needed.");
		break;
	case CMD2('Z', 'W'): arguments("e"); break;
	case CMD2('Z', 'Z'): arguments("ne"); break;

	case COMMAND_IF:
		expect('{');
		conditional();
		break;

	case COMMAND_CONST:
		define_const();
		break;

	case COMMAND_PRAGMA:
		pragma();
		break;

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
	case COMMAND_ainH: // fall through
	case COMMAND_ainHH:
		emit(compiler->msg_buf, 0);
		emit_dword(out, compiler->msg_count++);
		arguments("ne");
		break;
	case COMMAND_ainX:
		emit(compiler->msg_buf, 0);
		emit_dword(out, compiler->msg_count++);
		arguments("e");
		break;
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
		goto unknown_command;
	}
	return true;
 unknown_command:
		error_at(command_top, "Unknown command %.*s", input - command_top, command_top);
}

// commands ::= command*
static void commands(void) {
	while (command())
		;
}

// toplevel ::= commands
static void toplevel(void) {
	if (config.unicode && input_page == 0) {
		// Inject "ZU 1:" command.
		skip_whitespaces();
		if (out && compiler->dbg_info)
			debug_line_add(compiler->dbg_info, input_line, current_address(out));
		emit(out, 'Z');
		emit(out, 'U');
		emit(out, 0x41);
		emit(out, 0x7f);
	}

	commands();
	if (*input)
		error_at(input, "unexpected '%c'", *input);
}

Compiler *new_compiler(Vector *src_paths, Vector *variables, Map *dlls) {
	Compiler *comp = calloc(1, sizeof(Compiler));
	comp->src_paths = src_paths;
	comp->variables = variables ? variables : new_vec();
	comp->symbols = new_string_hash();
	comp->functions = new_string_hash();
	comp->dlls = dlls ? dlls : new_map();
	comp->scos = calloc(src_paths->len, sizeof(Sco));

	for (int i = 0; i < comp->variables->len; i++)
		hash_put(comp->symbols, comp->variables->data[i], new_symbol(VARIABLE, i));

	return comp;
}

static void prepare(Compiler *comp, const char *source, int pageno) {
	compiler = comp;
	lexer_init(source, comp->src_paths->data[pageno], pageno);
	menu_item_start = NULL;
	branch_end_stack = (config.sys_ver == SYSTEM35) ? new_vec() : NULL;
}

static void check_undefined_labels(void) {
	for (int i = 0; i < labels->vals->len; i++) {
		Label *l = labels->vals->data[i];
		if (l->hole_addr)
			error_at(l->source_loc, "undefined label '%s'", labels->keys->data[i]);
	}
}

void preprocess(Compiler *comp, const char *source, int pageno) {
	prepare(comp, source, pageno);
	compiling = false;
	labels = NULL;

	toplevel();

	if (menu_item_start)
		error_at(menu_item_start, "unfinished menu item");
	if (branch_end_stack && branch_end_stack->len > 0)
		error_at(input, "'}' expected");
}

void preprocess_done(Compiler *comp) {
	if (config.sys_ver == SYSTEM39)
		comp->msg_buf = new_buf();
	comp->msg_count = 0;
}

Sco *compile(Compiler *comp, const char *source, int pageno) {
	prepare(comp, source, pageno);
	compiling = true;
	labels = new_map();

	comp->scos[pageno].ald_volume = 1;
	out = new_buf();
	sco_init(out, comp->src_paths->data[pageno], pageno);
	if (comp->dbg_info)
		debug_init_page(comp->dbg_info, pageno);

	toplevel();

	if (menu_item_start)
		error_at(menu_item_start, "unfinished menu item");
	check_undefined_labels();

	sco_finalize(out);
	if (comp->dbg_info)
		debug_finish_page(comp->dbg_info, labels);
	comp->scos[pageno].buf = out;
	out = NULL;
	return &comp->scos[pageno];
}
