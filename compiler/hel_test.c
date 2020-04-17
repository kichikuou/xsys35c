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
#undef NDEBUG
#include <assert.h>

int main() {
	Vector *hel;

	hel = parse_hel("", "empty.hel");
	assert(hel->len == 0);

	hel = parse_hel("void foo(void)", "simple.hel");
	assert(hel->len == 1);
	DLLFunc *f = hel->data[0];
	assert(!strcmp(f->name, "foo"));
	assert(f->argc == 0);

	hel = parse_hel("void func1(pword arg)\nvoid func2(int arg1, IConstString arg2)", "twofuncs.hel");
	assert(hel->len == 2);
	DLLFunc *f1 = hel->data[0];
	DLLFunc *f2 = hel->data[1];
	assert(!strcmp(f1->name, "func1"));
	assert(!strcmp(f2->name, "func2"));
	assert(f1->argc == 1);
	assert(f1->argtypes[0] == Arg_pword);
	assert(f2->argc == 2);
	assert(f2->argtypes[0] == Arg_int);
	assert(f2->argtypes[1] == Arg_IConstString);
}
