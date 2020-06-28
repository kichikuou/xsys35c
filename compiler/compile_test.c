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
#include <string.h>

#define TEST(name, source, expected) \
	test(name, source, expected, sizeof(expected) - 1)

static void hexdump(const uint8_t* data, int len, int pos) {
	for (int i = 0; i < len; i++) {
		if (i % 16 == 0)
			printf("\n%04x ", i);
		if (i % 16 == 8)
			putchar(' ');
		printf("%c%02x", i == pos ? '>' : ' ', data[i]);
	}
}

static void test(const char *name, const char *source,
		  const char *expected, int expected_len) {
	if (strlen(name) > 14)
		error("%s: test name must be <= 14 characters.", name);
	Vector *src_names = new_vec();
	vec_push(src_names, (char *)name);
	Vector *variables = new_vec();
	vec_push(variables, "V");
	Compiler *compiler = new_compiler(src_names, variables, NULL);
	preprocess(compiler, source, 0);

	compiler->msg_count = 0;

	Buffer *sco = compile(compiler, source, 0)->buf;
	// Ignore SCO header
	sco->buf += 32;
	sco->len -= 32;

	if (sco->len != expected_len ||
		memcmp(sco->buf, expected, expected_len) != 0) {
		int pos;
		for (pos = 0; pos < expected_len && pos < sco->len; pos++) {
			if (sco->buf[pos] != (uint8_t)expected[pos])
				break;
		}
		printf("%s failed. expected:", name);
		hexdump((const uint8_t *)expected, expected_len, pos);
		printf("\ngot:");
		hexdump(sco->buf, sco->len, pos);
		printf("\n");
	}
}

void compile_test(void) {
	config.sys_ver = SYSTEM35;

	TEST("A", "A", "A");
	TEST("B1",
		 "B1,1,450,20,172,240,1:",
		 "\x42\x01\x41\x7f\x01\xc2\x7f\x54\x7f\x00\xac\x7f\x00\xf0\x7f\x41\x7f");
	TEST("B1-nocomma",
		 "B1 1,450,20,172,240,1:",
		 "\x42\x01\x41\x7f\x01\xc2\x7f\x54\x7f\x00\xac\x7f\x00\xf0\x7f\x41\x7f");
	TEST("WW",
		 "WW 640,1440,24:",
		 "\x57\x57\x02\x80\x7f\x05\xa0\x7f\x58\x7f");
	TEST("string-arg",
		 "LC 146,55,SMPCG_01.BMP:",
		 "\x4c\x43\x00\x92\x7f\x00\x37\x7f\x53\x4d\x50\x43\x47\x5f\x30\x31"
		 "\x2e\x42\x4d\x50\x3a");
	TEST("unaryG",
		 "G3:",
		 "\x47\x00\x43\x7f");
	TEST("binaryG",
		 "G3,1:",
		 "\x47\x01\x43\x7f\x41\x7f");

	TEST("comment",
		 "R;AAA\nR",
		 "RR");
	TEST("comment2",
		 "R//AAA\nR",
		 "RR");
	TEST("block-comment",
		 "R/*A*A\nA*/R",
		 "RR");

	TEST("data array",
		 "[1, 0b100000000, 0xffff]",
		 "\x01\x00\x00\x01\xff\xff");
	TEST("string data",
		 "\"\x82\xCD\x82\xA2\"",
		 "\xca\xb2\0");

	TEST("message",
		 "'\x83\x56\x83\x42\x83\x8B'",  // 'シィル' in SJIS
		 "\x83\x56\x83\x42\x83\x8B");
	TEST("message-conv",
		 "'\x81\x40\x81\x75\x82\xCD\x82\xA2\x81\x76'",  // '　「はい」' in SJIS
		 "\x20\xa2\xca\xb2\xa3");
	TEST("message-cp",
		 "'<0x8148>'",
		 "\x81\x48");

	TEST("menu-item",
		 "$l$\x83\x56\x83\x42\x83\x8B$ *l:",
		 "\x24\x2c\x00\x00\x00\x83\x56\x83\x42\x83\x8B\x24");
	TEST("menu-item-cmd",
		 "$l$'\x83\x56\x83\x42\x83\x8B'A$ *l:",
		 "\x24\x2d\x00\x00\x00\x83\x56\x83\x42\x83\x8B\x41\x24");

	TEST("assign",
		 "!V:0!",
		 "\x21\x80\x40\x7f");
	TEST("op-assign",
		 "!V+:0!!V-:0!!V*:0!!V/:0!!V%:0!!V&:0!!V|:0!!V^:0!",
		 "\x10\x80\x40\x7f\x11\x80\x40\x7f\x12\x80\x40\x7f\x13\x80\x40\x7f"
		 "\x14\x80\x40\x7f\x15\x80\x40\x7f\x16\x80\x40\x7f\x17\x80\x40\x7f");
	TEST("var-ref",
		 "!a:0!!b:a!!a:b!",
		 "\x21\x81\x40\x7f\x21\x82\x81\x7f\x21\x81\x82\x7f");
	TEST("array-ref",
		 "!V[1]:V[2]!",
		 "\x21\xc0\x01\x00\x00\x41\x7f\xc0\x01\x00\x00\x42\x7f\x7f");

	TEST("expr_mul",
		 "!V:0*1/2%3!",
		 "\x21\x80\x40\x41\x77\x42\x78\x43\xc0\x02\x7f");
	TEST("expr_add",
		 "!V:0+1-2!",
		 "\x21\x80\x40\x41\x79\x42\x7a\x7f");
	TEST("expr_bit",
		 "!V:0&1|2^3!",
		 "\x21\x80\x40\x41\x74\x42\x75\x43\x76\x7f");
	TEST("expr_compare",
		 "!V:0<1>2<=3>=4!",
		 "\x21\x80\x40\x41\x7c\x42\x7d\x43\xc0\x03\x44\xc0\x04\x7f");
	TEST("expr_equal",
		 "!V:0=1\\2!",
		 "\x21\x80\x40\x41\x7b\x42\x7e\x7f");
	TEST("expr_paren",
		 "!V:0+(1-2)!",
		 "\x21\x80\x40\x41\x42\x7a\x79\x7f");
	TEST("precedence",
		 "!V:0+1*2!",
		 "\x21\x80\x40\x41\x42\x77\x79\x7f");

	TEST("big-int",
		 "!V:65535!",
		 "\x21\x80\x3f\xff\x3f\xff\x3f\xff\x3f\xff\x43\x79\x79\x79\x79\x7f");
	TEST("big-int2",
		 "!V:65532!",
		 "\x21\x80\x3f\xff\x3f\xff\x3f\xff\x3f\xff\x79\x79\x79\x7f");

	TEST("const",
		 "const word C1=1, C2=2: !V:C1+C2!",
		 "\x21\x80\x41\x42\x79\x7f");
	TEST("__LINE__",
		 "!V:__LINE__+\n__LINE__!",
		 "\x21\x80\x41\x42\x79\x7f");

	TEST("label",
		 "*lbl:@lbl:",
		 "\x40\x20\x00\x00\x00");
	TEST("forward-ref",
		 "@lbl:*lbl:",
		 "\x40\x25\x00\x00\x00");
	TEST("forward-refs",
		 "@lbl:@lbl:*lbl:",
		 "\x40\x2a\x00\x00\x00\x40\x2a\x00\x00\x00");
	TEST("label call",
		 "\\lbl:*lbl:\\0:",
		 "\x5c\x25\x00\x00\x00\x5c\x00\x00\x00\x00");
	TEST("# command",
		 "#lbl,0:*lbl:",
		 "\x23\x27\x00\x00\x00\x40\x7f");

	TEST("conditional",
		 "{0:A}",
		 "\x7b\x40\x7f\x28\x00\x00\x00\x41");

	TEST("while-loop",
		 "<@ V=0:!V:1!>",
		 "\x7b\x80\x40\x7b\x7f\x32\x00\x00\x00\x21\x80\x41\x7f\x3e\x20\x00"
		 "\x00\x00");
	TEST("for-loop",
		 "<V,1,10,0,3:A>",
		 "\x21\x80\x41\x7f\x3c\x00\x3c\x01\x3a\x00\x00\x00\x80\x7f\x4a\x7f"
		 "\x40\x7f\x43\x7f\x41\x3e\x26\x00\x00\x00");

	TEST("function",
		 "!X:0!!Y:0! **func Y,X:~0,X: ~func 1,2:",
		 "\x21\x81\x40\x7f\x21\x82\x40\x7f\x7e\x00\x00\x81\x7f\x21\x82\x41"
		 "\x7f\x21\x81\x42\x7f\x7e\x01\x00\x28\x00\x00\x00");
	TEST("function-fw",
		 "!X:0!!Y:0! ~func 1,2: **func Y,X:~0,X:",
		 "\x21\x81\x40\x7f\x21\x82\x40\x7f\x21\x82\x41\x7f\x21\x81\x42\x7f"
		 "\x7e\x01\x00\x37\x00\x00\x00\x7e\x00\x00\x81\x7f");

	config.sys_ver = SYSTEM36;

	TEST("msg-noconv",
		 "'\x81\x40\x81\x75\x82\xCD\x82\xA2\x81\x76'",  // '　「はい」' in SJIS
		 "\x81\x40\x81\x75\x82\xCD\x82\xA2\x81\x76");

	config.sys_ver = SYSTEM38;

	TEST("TOC", "TOC:", "\x2f\x00");

	TEST("msg-ascii",
		 "'Hello'",
		 "/!Hello\0");
	TEST("msg-escape",
		 "'That\\'s it'",
		 "/!That's it\0");
	TEST("msg-nonescape",
		 "'\x95\x5C'",    // '表' in SJIS, where the second byte is '\'
		 "/!\x95\x5C\0");
	TEST("msg-codepoint",
		 "'<0x8148>'",
		 "/!\x81\x48\0");

	TEST("if-keyword",
		 "if{0:}",
		 "\x7b\x40\x7f\x2c\x00\x00\x00\x40\x2c\x00\x00\x00");
	TEST("if-else",
		 "{0:AA} else {A}",
		 "\x7b\x40\x7f\x2e\x00\x00\x00\x41\x41\x40\x2f\x00\x00\x00\x41");
	TEST("if-else-if",
		 "{0:A} else if {1:A} else {A}",
		 "\x7b\x40\x7f\x2d\x00\x00\x00\x41\x40\x3b\x00\x00\x00\x7b\x41\x7f"
		 "\x3a\x00\x00\x00\x41\x40\x3b\x00\x00\x00\x41");

	TEST("newMT",
		 "MT \"Title\":",
		 "/(Title\0");
	TEST("lccmd",
		 "sysAddWebMenu \"Home page\", \"https://kichikuou.github.io/\":",
		 "/IHome page\0https://kichikuou.github.io/\0");

	config.sys_ver = SYSTEM39;

	TEST("ain-msg", "'ABC' R 'DEF'",
		 "\x2f\x7c\x00\x00\x00\x00\x52\x2f\x7c\x01\x00\x00\x00");

	TEST("ain-H", "H3 10: 'a' A 'b'",
		 "\x2f\x7d\x00\x00\x00\x00\x03\x4a\x7f\x41\x2f\x7c\x01\x00\x00\x00");
	TEST("ain-H2", "H3 10: A 'b'",
		 "\x2f\x7d\x00\x00\x00\x00\x03\x4a\x7f\x41\x2f\x7c\x01\x00\x00\x00");
}
