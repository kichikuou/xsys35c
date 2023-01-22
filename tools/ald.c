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
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <sys/utime.h>
#endif

static void usage(void) {
	puts("Usage: ald <command> [<args>]");
	puts("");
	puts("commands:");
	puts("  list     Print list of archive files");
	puts("  create   Create a new archive");
	puts("  extract  Extract file(s) from archive");
	puts("  dump     Print hex dump of file");
	puts("  compare  Compare contents of two archives");
	puts("  help     Display help information about commands");
	puts("  version  Display version information and exit");
	puts("");
	puts("Run 'ald help <command>' for more information about a specific command.");
}

static Vector *read_alds(int *pargc, char **pargv[]) {
	int argc = *pargc;
	char **argv = *pargv;

	for (int dd = 0; dd < argc; dd++) {
		if (!strcmp(argv[dd], "--")) {
			Vector *ald = NULL;
			for (int i = 0; i < dd; i++)
				ald = ald_read(ald, argv[i]);
			*pargc -= dd + 1;
			*pargv += dd + 1;
			return ald;
		}
	}

	Vector *ald = NULL;
	for (int i = 0; i < argc; i++) {
		const char *dot = strrchr(argv[i], '.');
		if (!dot || strcasecmp(dot, ".ald"))
			break;
		ald = ald_read(ald, argv[i]);
		*pargc -= 1;
		*pargv += 1;
	}
	return ald;
}

static AldEntry *find_entry(Vector *ald, const char *num_or_name) {
	char *endptr;
	unsigned long idx = strtoul(num_or_name, &endptr, 0);
	if (*endptr == '\0') {
		if (idx == 0 || idx > ald->len) {
			fprintf(stderr, "ald: index %lu is out of range (1-%d)\n", idx, ald->len);
			return NULL;
		}
		if (!ald->data[idx-1]) {
			fprintf(stderr, "ald: No entry for index %lu\n", idx);
			return NULL;
		}
		return ald->data[idx-1];
	}

	for (int i = 0; i < ald->len; i++) {
		AldEntry *e = ald->data[i];
		if (e && !strcasecmp(num_or_name, sjis2utf(e->name)))
			return e;
	}
	fprintf(stderr, "ald: No entry for '%s'\n", num_or_name);
	return NULL;
}

static void write_manifest(Vector *ald, FILE *fp) {
	for (int i = 0; i < ald->len; i++) {
		AldEntry *e = ald->data[i];
		if (e)
			fprintf(fp, "%d,%d,%s\n", e->volume, i + 1, sjis2utf(e->name));
	}
}

// ald list ----------------------------------------

static void help_list(void) {
	puts("Usage: ald list <aldfile>...");
}

static int do_list(int argc, char *argv[]) {
	if (argc == 1) {
		help_list();
		return 1;
	}
	Vector *ald = new_vec();
	for (int i = 1; i < argc; i++)
		ald_read(ald, argv[i]);
	char buf[30];
	for (int i = 0; i < ald->len; i++) {
		AldEntry *e = ald->data[i];
		if (!e)
			continue;
		struct tm *t = localtime(&e->timestamp);
		strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
		printf("%4d %2d  %s  %8d  %s\n", i + 1, e->volume, buf, e->size, sjis2utf(e->name));
	}
	return 0;
}

// ald create ----------------------------------------

static const char create_short_options[] = "m:";
static const struct option create_long_options[] = {
	{ "manifest",  required_argument, NULL, 'm' },
	{ 0, 0, 0, 0 }
};

static void help_create(void) {
	puts("Usage: ald create <aldfile> <file>...");
	puts("       ald create <aldfile> -m <manifest-file>");
	puts("Options:");
	puts("    -m, --manifest <file>    Read manifest from <file>");
}

static void add_file(Vector *ald, int volume, int no, const char *path) {
	FILE *fp = checked_fopen(path, "rb");
	struct stat sbuf;
	if (fstat(fileno(fp), &sbuf) < 0)
		error("%s: %s", path, strerror(errno));
	uint8_t *data = malloc(sbuf.st_size);
	if (!data)
		error("out of memory");
	if (sbuf.st_size > 0 && fread(data, sbuf.st_size, 1, fp) != 1)
		error("%s: %s", path, strerror(errno));
	fclose(fp);

	AldEntry *e = calloc(1, sizeof(AldEntry));
	e->name = strdup(basename_utf8(path));
	e->timestamp = sbuf.st_mtime;
	e->data = data;
	e->size = sbuf.st_size;
	e->volume = volume;
	vec_set(ald, no - 1, e);
}

static uint32_t add_files_from_manifest(Vector *ald, const char *manifest) {
	FILE *fp = checked_fopen(manifest, "r");
	char line[200];
	int lineno = 0;
	uint32_t vol_bits = 0;
	int prev_link_no = 0;
	while (fgets(line, sizeof(line), fp)) {
		lineno++;
		if (line[0] == '\n')
			continue;
		int volume;
		int link_no;
		char fname[200];
		if (sscanf(line, " %d, %d, %s", &volume, &link_no, fname) == 3)  // NL5 format
			prev_link_no = link_no;
		else if (sscanf(line, " %d, %s", &volume, fname) == 2)  // NL4 format
			link_no = ++prev_link_no;
		else
			error("%s:%d manifest syntax error", manifest, lineno);

		if (volume < 1 || volume > 26)
			error("%s:%d invalid volume id %d", manifest, lineno, volume);
		if (link_no < 1 || link_no > 65535)
			error("%s:%d invalid link number %d", manifest, lineno, link_no);
		if (link_no - 1 < ald->len && ald->data[link_no - 1])
			error("%s:%d duplicated link number %d", manifest, lineno, link_no);
		vol_bits |= 1 << volume;
		add_file(ald, volume, link_no, fname);
	}
	fclose(fp);
	return vol_bits;
}

static int do_create(int argc, char *argv[]) {
	const char *manifest = NULL;
	int opt;
	while ((opt = getopt_long(argc, argv, create_short_options, create_long_options, NULL)) != -1) {
		switch (opt) {
		case 'm':
			manifest = optarg;
			break;
		default:
			help_create();
			return 1;
		}
	}
	argc -= optind;
	argv += optind;

	if ((manifest && argc != 1) || (!manifest && argc <= 1)) {
		help_create();
		return 1;
	}
	char *ald_path = strdup(argv[0]);
	Vector *entries = new_vec();

	if (manifest) {
		int len = strlen(ald_path);
		char *volume_letter = ald_path + len - 5;
		if (strcasecmp(volume_letter, "a.ald"))
			error("ald: output filename must end with \"a.ald\"");
		char base = *volume_letter - 1;

		uint32_t vol_bits = add_files_from_manifest(entries, manifest);
		for (int vol = 1; vol <= 26; vol++) {
			if ((vol_bits & 1 << vol) == 0)
				continue;
			*volume_letter = base + vol;
			FILE *fp = checked_fopen(ald_path, "wb");
			ald_write(entries, vol, fp);
			fclose(fp);
		}
	} else {
		for (int i = 1; i < argc; i++)
			add_file(entries, 1, i, argv[i]);
		FILE *fp = checked_fopen(ald_path, "wb");
		ald_write(entries, 1, fp);
		fclose(fp);
	}
	return 0;
}

// ald extract ----------------------------------------

static const char extract_short_options[] = "d:m:";
static const struct option extract_long_options[] = {
	{ "directory", required_argument, NULL, 'd' },
	{ "manifest",  required_argument, NULL, 'm' },
	{ 0, 0, 0, 0 }
};

static void help_extract(void) {
	puts("Usage: ald extract [options] <aldfile>... [--] [(<n>|<file>)...]");
	puts("Options:");
	puts("    -d, --directory <dir>    Extract files into <dir>");
	puts("    -m, --manifest <file>    Write manifest to <file>");
}

static void extract_entry(AldEntry *e, const char *directory) {
	const char *utf_name = sjis2utf(e->name);
	puts(utf_name);
	FILE *fp = checked_fopen(path_join(directory, utf_name), "wb");
	if (e->size > 0 && fwrite(e->data, e->size, 1, fp) != 1)
		error("%s: %s", sjis2utf(e->name), strerror(errno));

	fflush(fp);
#ifdef _WIN32
	struct _utimbuf times = {
		.actime = e->timestamp,
		.modtime = e->timestamp
	};
	_futime(_fileno(fp), &times);
#else
	struct timespec times[2] = {
		[0].tv_nsec = UTIME_OMIT,
		[1].tv_sec = e->timestamp
	};
	futimens(fileno(fp), times);
#endif

	fclose(fp);
}

static int do_extract(int argc, char *argv[]) {
	const char *directory = NULL;
	const char *manifest = NULL;
	int opt;
	while ((opt = getopt_long(argc, argv, extract_short_options, extract_long_options, NULL)) != -1) {
		switch (opt) {
		case 'd':
			directory = optarg;
			break;
		case 'm':
			manifest = optarg;
			break;
		default:
			help_extract();
			return 1;
		}
	}
	argc -= optind;
	argv += optind;

	Vector *ald = read_alds(&argc, &argv);
	if (!ald) {
		help_extract();
		return 1;
	}

	if (directory && make_dir(directory) != 0 && errno != EEXIST)
		error("cannot create directory %s: %s", directory, strerror(errno));

	if (manifest) {
		FILE *fp = checked_fopen(manifest, "w");
		write_manifest(ald, fp);
		fclose(fp);
	}

	if (!argc) {
		// Extract all files.
		for (int i = 0; i < ald->len; i++) {
			AldEntry *e = ald->data[i];
			if (e)
				extract_entry(e, directory);
		}
	} else {
		for (int i = 0; i < argc; i++) {
			AldEntry *e = find_entry(ald, argv[i]);
			if (e)
				extract_entry(e, directory);
		}
	}
	return 0;
}

// ald dump ----------------------------------------

static void help_dump(void) {
	puts("Usage: ald dump <aldfile>... [--] <n>|<file>");
}

static void print_sjis_2byte(uint8_t c1, uint8_t c2) {
	char in[3] = {c1, c2, 0};
	char *out = sjis2utf_sub(in, '.');
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
	argc--;
	argv++;
	Vector *ald = read_alds(&argc, &argv);
	if (!ald || argc != 1) {
		help_dump();
		return 1;
	}

	AldEntry *e = find_entry(ald, argv[0]);
	if (!e)
		return 1;
	dump_entry(e);
	return 0;
}

// ald compare ----------------------------------------

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
	if (argc != 3) {
		help_compare();
		return 1;
	}
	const char *aldfile1 = argv[1];
	const char *aldfile2 = argv[2];
	Vector *ald1 = ald_read(NULL, aldfile1);
	Vector *ald2 = ald_read(NULL, aldfile2);

	bool differs = false;
	for (int i = 0; i < ald1->len && i < ald2->len; i++) {
		if (ald1->data[i] && ald2->data[i]) {
			differs |= compare_entry(i, ald1->data[i], ald2->data[i]);
		} else if (ald1->data[i]) {
			AldEntry *e = ald1->data[i];
			printf("%s (%d) only exists in %s\n", sjis2utf(e->name), i, aldfile1);
			differs = true;
		} else if (ald2->data[i]) {
			AldEntry *e = ald2->data[i];
			printf("%s (%d) only exists in %s\n", sjis2utf(e->name), i, aldfile2);
			differs = true;
		}
	}

	for (int i = ald2->len; i < ald1->len; i++) {
		AldEntry *e = ald1->data[i];
		printf("%s (%d) only exists in %s\n", sjis2utf(e->name), i, aldfile1);
		differs = true;
	}
	for (int i = ald1->len; i < ald2->len; i++) {
		AldEntry *e = ald2->data[i];
		printf("%s (%d) only exists in %s\n", sjis2utf(e->name), i, aldfile2);
		differs = true;
	}
	return differs ? 1 : 0;
}

// ald help ----------------------------------------

static void help_help(void) {
	puts("Usage: ald help <command>");
}

static int do_help(int argc, char *argv[]);  // defined below

// ald version ----------------------------------------

static void help_version(void) {
	puts("Usage: ald version");
}

static int do_version(int argc, char *argv[]) {
	puts("ald " VERSION);
	return 0;
}

// main ----------------------------------------

typedef struct {
	const char *name;
	int (*func)(int argc, char *argv[]);
	void (*help)(void);
} Command;

static Command commands[] = {
	{"list",    do_list,    help_list},
	{"create",  do_create,  help_create},
	{"extract", do_extract, help_extract},
	{"dump",    do_dump,    help_dump},
	{"compare", do_compare, help_compare},
	{"help",    do_help,    help_help},
	{"version", do_version, help_version},
	{NULL, NULL, NULL}
};

// Defined here because it accesses the commands table.
static int do_help(int argc, char *argv[]) {
	if (argc == 1) {
		help_help();
		return 1;
	}
	for (Command *cmd = commands; cmd->name; cmd++) {
		if (!strcmp(argv[1], cmd->name)) {
			cmd->help();
			return 0;
		}
	}
	error("ald help: Invalid subcommand '%s'", argv[1]);
}

int main(int argc, char *argv[]) {
	init(argc, argv);

	if (argc == 1) {
		usage();
		return 1;
	}
	for (Command *cmd = commands; cmd->name; cmd++) {
		if (!strcmp(argv[1], cmd->name))
			return cmd->func(argc - 1, argv + 1);
	}
	error("ald: Invalid subcommand '%s'", argv[1]);
}
