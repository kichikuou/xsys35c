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
#include "common.h"
#include <ctype.h>
#include <errno.h>
#include <iconv.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
	puts("Usage: ald <command> [<args>]");
	puts("");
	puts("commands:");
	puts("  list     Print list of archive files");
	puts("  extract  Extract file(s) from archive");
	puts("  dump     Print hex dump of file");
	puts("  compare  Compare contents of two archives");
	puts("  help     Display help information about commands");
	puts("");
	puts("Run 'ald help <command>' for more information about a specific command.");
}

static void help_list(void) {
	puts("Usage: ald list <aldfile>");
}

static int do_list(int argc, char *argv[]) {
	if (argc == 0) {
		help_list();
		return 1;
	}
	Vector *ald = ald_read(argv[0]);
	char buf[30];
	for (int i = 0; i < ald->len; i++) {
		AldEntry *e = ald->data[i];
		struct tm *t = localtime(&e->timestamp);
		strftime(buf, sizeof(buf), "%F %H:%M:%S", t);
		printf("%4d  %s  %8d  %s\n", i, buf, e->size, sjis2utf(e->name));
	}
	return 0;
}

static void help_extract(void) {
	puts("Usage: ald extract <aldfile>");
}

static int do_extract(int argc, char *argv[]) {
	if (argc == 0) {
		help_extract();
		return 1;
	}
	Vector *ald = ald_read(argv[0]);
	for (int i = 0; i < ald->len; i++) {
		AldEntry *e = ald->data[i];
		FILE *fp = fopen(sjis2utf(e->name), "wb");
		if (!fp)
			error("%s: %s", sjis2utf(e->name), strerror(errno));
		if (fwrite(e->data, e->size, 1, fp) != 1)
			error("%s: %s", sjis2utf(e->name), strerror(errno));
		fclose(fp);
	}
	return 0;
}

static void help_dump(void) {
	puts("Usage: ald dump <aldfile> [<n>|<file>]");
}

static void print_sjis_2byte(uint8_t c1, uint8_t c2) {
	static iconv_t iconv_s2u = (iconv_t)-1;
	if (iconv_s2u == (iconv_t)-1) {
		iconv_s2u = iconv_open("utf8", "CP932");
		if (iconv_s2u == (iconv_t)-1)
			error("iconv_open(utf8, CP932): %s", strerror(errno));
	}

	char in[2] = {c1, c2};
	char out[8];
	char *ip = in;
	size_t ilen = c2 ? 2 : 1;
	char *op = out;
	size_t olen = 8;
	if (iconv(iconv_s2u, &ip, &ilen, &op, &olen) == (size_t)-1) {
		putchar('.');
		if (c2)
			putchar('.');
		return;
	}
	*op = '\0';
	fputs(out, stdout);
}

static void dump_entry(AldEntry *entry) {
	bool skip_first = false;
	for (int addr = 0; addr < entry->size; addr += 16) {
		int n = (entry->size - addr > 16) ? 16 : entry->size - addr;
		printf("%08x: ", addr);
		for (int i = 0; i < n; i++)
			printf("%02x ", entry->data[addr + i]);
		for (int i = n; i < 16; i++)
			printf("   ");
		putchar(' ');

		const uint8_t *p = entry->data + addr;
		for (int i = 0; i < n; i++) {
			if (i == 0 && skip_first) {
				putchar(' ');
				skip_first = false;
				continue;
			}
			if (is_sjis_byte1(p[i])) {
				if (addr + i + 1 < entry->size && is_sjis_byte2(p[i+1])) {
					print_sjis_2byte(p[i], p[i+1]);
					if (i == 15)
						skip_first = true;
					i++;
				} else {
					putchar('.');
				}
			} else if (is_sjis_half_kana(p[i])) {
				print_sjis_2byte(p[i], 0);
			} else if (isprint(p[i])) {
				putchar(p[i]);
			} else {
				putchar('.');
			}
		}
		putchar('\n');
	}
}

static int do_dump(int argc, char *argv[]) {
	if (argc != 2) {
		help_dump();
		return 1;
	}
	Vector *ald = ald_read(argv[0]);

	char *endptr;
	unsigned long n = strtoul(argv[1], &endptr, 0);
	if (!*endptr) {
		if (n >= ald->len)
			error("Page %d is out of range", n);
		dump_entry(ald->data[n]);
		return 0;
	}

	for (int i = 0; i < ald->len; i++) {
		AldEntry *e = ald->data[i];
		if (!strcasecmp(argv[1], sjis2utf(e->name))) {
			dump_entry(e);
			return 0;
		}
	}
	error("%s: no entry for '%s'", argv[0], argv[1]);
}

static void help_compare(void) {
	puts("Usage: ald compare <aldfile1> <aldfile2>");
}

static bool compare_entry(int page, AldEntry *e1, AldEntry *e2) {
	if (strcasecmp(e1->name, e2->name)) {
		printf("Entry %d: names differ, %s != %s\n", page, sjis2utf(e1->name), sjis2utf(e2->name));
		return true;
	}
	if (e1->size == e2->size && !memcmp(e1->data, e2->data, e1->size))
		return false;

	int i;
	for (i = 0; i < e1->size && i < e2->size; i++) {
		if (e1->data[i] != e2->data[i])
			break;
	}
	printf("%s (%d): differ at %05x\n", sjis2utf(e1->name), page, i);
	return true;
}

static int do_compare(int argc, char *argv[]) {
	if (argc != 2) {
		help_compare();
		return 1;
	}
	Vector *ald1 = ald_read(argv[0]);
	Vector *ald2 = ald_read(argv[1]);

	bool differs = false;
	for (int i = 0; i < ald1->len && i < ald2->len; i++)
		differs |= compare_entry(i, ald1->data[i], ald2->data[i]);

	for (int i = ald2->len; i < ald1->len; i++) {
		AldEntry *e = ald1->data[i];
		printf("%s (%d) only exists in %s\n", sjis2utf(e->name), i, argv[1]);
		differs = true;
	}
	for (int i = ald1->len; i < ald2->len; i++) {
		AldEntry *e = ald2->data[i];
		printf("%s (%d) only exists in %s\n", sjis2utf(e->name), i, argv[2]);
		differs = true;
	}
	return differs ? 1 : 0;
}

static void help_help(void) {
	puts("Usage: ald help <command>");
}

static int do_help(int argc, char *argv[]);

typedef struct {
	const char *name;
	int (*func)(int argc, char *argv[]);
	void (*help)(void);
} Command;

Command commands[] = {
	{"list",    do_list,    help_list},
	{"extract", do_extract, help_extract},
	{"dump",    do_dump,    help_dump},
	{"compare", do_compare, help_compare},
	{"help",    do_help,    help_help},
	{NULL, NULL, NULL}
};

static int do_help(int argc, char *argv[]) {
	if (argc == 0) {
		help_help();
		return 1;
	}
	for (Command *cmd = commands; cmd->name; cmd++) {
		if (!strcmp(argv[0], cmd->name)) {
			cmd->help();
			return 0;
		}
	}
	error("ald help: Invalid subcommand '%s'", argv[0]);
}

int main(int argc, char *argv[]) {
	if (argc == 1) {
		usage();
		return 1;
	}
	for (Command *cmd = commands; cmd->name; cmd++) {
		if (!strcmp(argv[1], cmd->name))
			return cmd->func(argc - 2, argv + 2);
	}
	error("ald: Invalid subcommand '%s'", argv[1]);
}
