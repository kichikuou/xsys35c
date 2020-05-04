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
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DLL_FUNC_PARAMS 20

static char *identifier(void) {
	skip_whitespaces();
	const char *top = input;
	if (!isalpha(*top) && *top != '_')
		error_at(top, "identifier expected");
	while (isalnum(*input) || *input == '_')
		input++;
	return strndup_(top, input - top);
}

typedef struct {
	const char *name;
	HELType type;
} HELTypeName;

static const HELTypeName hel_type_names[] = {
	{"pword", HEL_pword},
	{"int", HEL_int},
	{"ISurface", HEL_ISurface},
	{"IString", HEL_IString},
	{"IWinMsg", HEL_IWinMsg},
	{"ITimer", HEL_ITimer},
	{"IUI", HEL_IUI},
	{"ISys3xDIB", HEL_ISys3xDIB},
	{"ISys3xCG", HEL_ISys3xCG},
	{"ISys3xStringTable", HEL_ISys3xStringTable},
	{"ISys3xSystem", HEL_ISys3xSystem},
	{"ISys3xMusic", HEL_ISys3xMusic},
	{"ISys3xMsgString", HEL_ISys3xMsgString},
	{"ISys3xInputDevice", HEL_ISys3xInputDevice},
	{"ISys3x", HEL_ISys3x},
	{"IConstString", HEL_IConstString},
	{NULL, 0},
};

static HELType param_type_from_name(const char *name) {
	for (const HELTypeName *e = hel_type_names; e->name; e++) {
		if (!strcmp(e->name, name))
			return e->type;
	}
	error_at(input - strlen(name), "invalid type");
}

// params ::= 'void' | type identifier [',' type identifier]*
static void params(DLLFunc *func) {
	if (consume_keyword("void"))
		return;
	do {
		if (func->argc >= MAX_DLL_FUNC_PARAMS)
			error_at(input, "%s: too many parameters", func->name);
		const char *type = identifier();
		identifier();  // name
		func->argtypes[func->argc++] = param_type_from_name(type);
	} while (consume(','));
}

// fundecl ::= rtype identifier '(' params ')'
static DLLFunc *fundecl(void) {
	// TODO: Support 'bool' return type
	if (!consume_keyword("void"))
		error_at(input, "keyword 'void' expected");

	DLLFunc *func = calloc(1, sizeof(DLLFunc) + sizeof(HELType) * MAX_DLL_FUNC_PARAMS);
	func->name = identifier();
	expect('(');
	params(func);
	expect(')');
	return func;
}

// hel ::= fundecl*
Vector *parse_hel(const char* hel, const char* name) {
	lexer_init(hel, name, -1);
	Vector *funcs = new_vec();
	while (skip_whitespaces(), *input)
		vec_push(funcs, fundecl());
	return funcs;
}
