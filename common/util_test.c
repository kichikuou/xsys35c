/* Copyright (C) 2023 <KichikuouChrome@gmail.com>
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

#undef NDEBUG
#include "common.h"
#include <assert.h>
#include <string.h>

void test_dirname_utf8(void) {
	assert(!strcmp(dirname_utf8(""), "."));
	assert(!strcmp(dirname_utf8("/usr/lib"), "/usr"));
	assert(!strcmp(dirname_utf8("/usr/"), "/"));
	assert(!strcmp(dirname_utf8("usr"), "."));
	assert(!strcmp(dirname_utf8("/"), "/"));
	assert(!strcmp(dirname_utf8("."), "."));
	assert(!strcmp(dirname_utf8(".."), "."));

#ifdef _WIN32
	// Match Mingw-w64's dirname() behavior
	assert(!strcmp(dirname_utf8("c:"), "."));
	assert(!strcmp(dirname_utf8("c:\\usr\\lib"), "c:\\usr"));
	assert(!strcmp(dirname_utf8("c:\\usr\\"), "c:\\"));
	assert(!strcmp(dirname_utf8("c:usr"), "c:."));
	assert(!strcmp(dirname_utf8("c:\\"), "c:\\"));
	assert(!strcmp(dirname_utf8("c:."), "c:."));
	assert(!strcmp(dirname_utf8("c:.."), "c:."));
#endif
}

void test_basename_utf8(void) {
	assert(!strcmp(basename_utf8(""), "."));
	assert(!strcmp(basename_utf8("/usr/lib"), "lib"));
	assert(!strcmp(basename_utf8("/usr/"), "usr"));
	assert(!strcmp(basename_utf8("usr"), "usr"));
	assert(!strcmp(basename_utf8("/"), "/"));
	assert(!strcmp(basename_utf8("."), "."));
	assert(!strcmp(basename_utf8(".."), ".."));

#ifdef _WIN32
	// Match Mingw-w64's basename() behavior
	assert(!strcmp(basename_utf8("c:"), "."));
	assert(!strcmp(basename_utf8("c:\\usr\\lib"), "lib"));
	assert(!strcmp(basename_utf8("c:\\usr\\"), "usr"));
	assert(!strcmp(basename_utf8("c:usr"), "usr"));
	assert(!strcmp(basename_utf8("c:\\"), "\\"));
	assert(!strcmp(basename_utf8("c:."), "."));
	assert(!strcmp(basename_utf8("c:.."), ".."));
#endif
}

void util_test(void) {
	test_dirname_utf8();
	test_basename_utf8();
}
