/* Copyright (C) 2021 kichikuou <KichikuouChrome@gmail.com>
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
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _POSIX_MAPPED_FILES
#include <sys/mman.h>
#endif
#ifndef _O_BINARY
#define _O_BINARY 0
#endif

typedef struct {
	const uint8_t *data;
	int size;
} AlkEntry;

static Vector *alk_read(const char *path) {
	int fd = checked_open(path, O_RDONLY | _O_BINARY);

	struct stat sbuf;
	if (fstat(fd, &sbuf) < 0)
		error("%s: %s", path, strerror(errno));
	if (sbuf.st_size < 8)
		error("%s: not an ALK file");

#ifdef _POSIX_MAPPED_FILES
	uint8_t *p = mmap(NULL, sbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		error("%s: %s", path, strerror(errno));
#else
	uint8_t *p = malloc(sbuf.st_size);
	size_t bytes = 0;
	while (bytes < sbuf.st_size) {
		ssize_t ret = read(fd, p + bytes, sbuf.st_size - bytes);
		if (ret <= 0)
			error("%s: %s", path, strerror(errno));
		bytes += ret;
	}
#endif
	close(fd);

	if (strncmp((char *)p, "ALK0", 4))
		error("%s: invalid ALK signature", path);

	uint32_t nfile = le32(p + 4);
	if (sbuf.st_size < 8 + nfile * 8)
		error("%s: not an ALK file");

	AlkEntry *es = calloc(nfile, sizeof(AlkEntry));
	Vector *alk = new_vec();
	for (int i = 0; i < nfile; i++) {
		uint32_t offset = le32(p + 8 + (i * 8));
		uint32_t length = le32(p + 12 + (i * 8));
		if (offset + length > sbuf.st_size)
			error("%s: invalid ALK file");
		es[i].data = p + offset;
		es[i].size = length;
		vec_push(alk, &es[i]);
	}
	return alk;
}

static void alk_write(Vector *entries, const char *path) {
	FILE *fp = checked_fopen(path, "wb");
	fputs("ALK0", fp);
	fputdw(entries->len, fp);

	uint32_t offset = 8 + 8 * entries->len;
	for (int i = 0; i < entries->len; i++) {
		AlkEntry *e = entries->data[i];
		fputdw(offset, fp);
		fputdw(e->size, fp);
		offset += e->size;
	}

	for (int i = 0; i < entries->len; i++) {
		AlkEntry *e = entries->data[i];
		if (e->size > 0 && fwrite(e->data, e->size, 1, fp) != 1)
			error("%s: %s", path, strerror(errno));
	}
	fclose(fp);
}

static void usage(void) {
	puts("Usage: alk <command> [<args>]");
	puts("");
	puts("commands:");
	puts("  list     Print list of archive files");
	puts("  create   Create a new archive");
	puts("  extract  Extract file(s) from archive");
	puts("  help     Display help information about commands");
	puts("  version  Display version information and exit");
	puts("");
	puts("Run 'alk help <command>' for more information about a specific command.");
}

const char *guess_filetype(AlkEntry *e) {
	if (e->size == 0)
		return "";
	if (e->size >= 4 && !memcmp(e->data, "PM\x01\0", 4))
		return "pms";
	if (e->size >= 4 && !memcmp(e->data, "PM\x02\0", 4))
		return "pms";
	if (e->size >= 4 && !memcmp(e->data, "QNT\0", 4))
		return "qnt";
	if (e->size >= 4 && !memcmp(e->data, "\xff\xd8\xff\xe0", 4))
		return "jpeg";
	if (e->size >= 2 && !memcmp(e->data, "BM", 2))
		return "bmp";
	return NULL;
}

// alk list ----------------------------------------

static void help_list(void) {
	puts("Usage: alk list <alkfile>");
}

static int do_list(int argc, char *argv[]) {
	if (argc != 2) {
		help_list();
		return 1;
	}
	Vector *alk = alk_read(argv[1]);
	for (int i = 0; i < alk->len; i++) {
		AlkEntry *e = alk->data[i];
		if (!e->size)
			continue;
		const char *type = guess_filetype(e);
		if (!type)
			type = "?";
		printf("%4d  %4s  %8d\n", i + 1, type, e->size);
	}
	return 0;
}

// alk create ----------------------------------------

static void help_create(void) {
	puts("Usage: alk create <alkfile> <file>...");
}

static int do_create(int argc, char *argv[]) {
	if (argc < 3) {
		help_create();
		return 1;
	}
	const char *alk_path = argv[1];
	Vector *entries = new_vec();
	for (int i = 2; i < argc; i++) {
		FILE *fp = checked_fopen(argv[i], "rb");
		struct stat sbuf;
		if (fstat(fileno(fp), &sbuf) < 0)
			error("%s: %s", argv[1], strerror(errno));
		uint8_t *data = malloc(sbuf.st_size);
		if (!data)
			error("out of memory");
		if (sbuf.st_size > 0 && fread(data, sbuf.st_size, 1, fp) != 1)
			error("%s: %s", argv[1], strerror(errno));
		fclose(fp);

		AlkEntry *e = calloc(1, sizeof(AlkEntry));
		e->data = data;
		e->size = sbuf.st_size;
		vec_push(entries, e);
	}
	alk_write(entries, alk_path);
	return 0;
}

// alk extract ----------------------------------------

static const char extract_short_options[] = "d:";
static const struct option extract_long_options[] = {
	{ "directory", required_argument, NULL, 'd' },
	{ 0, 0, 0, 0 }
};

static void help_extract(void) {
	puts("Usage: alk extract [options] <alkfile> [<index>...]");
	puts("Options:");
	puts("    -d, --directory <dir>    Extract files into <dir>");
}

static void extract_entry(AlkEntry *e, int index, const char *directory) {
	char fname[20];
	const char *type = guess_filetype(e);
	if (type)
		sprintf(fname, "%d.%s", index, type);
	else
		sprintf(fname, "%d", index);

	puts(fname);
	FILE *fp = checked_fopen(path_join(directory, fname), "wb");
	if (e->size > 0 && fwrite(e->data, e->size, 1, fp) != 1)
		error("%s: %s", fname, strerror(errno));
	fclose(fp);
}

static int do_extract(int argc, char *argv[]) {
	const char *directory = NULL;
	int opt;
	while ((opt = getopt_long(argc, argv, extract_short_options, extract_long_options, NULL)) != -1) {
		switch (opt) {
		case 'd':
			directory = optarg;
			break;
		default:
			help_extract();
			return 1;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0) {
		help_extract();
		return 1;
	}
	Vector *alk = alk_read(argv[0]);

	if (directory && make_dir(directory) != 0 && errno != EEXIST)
		error("cannot create directory %s: %s", directory, strerror(errno));

	if (argc == 1) {
		// Extract all files.
		for (int i = 0; i < alk->len; i++) {
			AlkEntry *e = alk->data[i];
			if (e->size > 0)
				extract_entry(e, i + 1, directory);
		}
	} else {
		for (int i = 1; i < argc; i++) {
			int idx = atoi(argv[i]);
			if (idx <= 0 || idx > alk->len) {
				fprintf(stderr, "alk: index %d is out of range (1-%d)\n", idx, alk->len);
				continue;
			}
			AlkEntry *e = alk->data[idx - 1];
			if (e->size == 0) {
				fprintf(stderr, "alk: No entry for index %d\n", idx);
				continue;
			}
			extract_entry(e, idx, directory);
		}
	}
	return 0;
}

// alk help ----------------------------------------

static void help_help(void) {
	puts("Usage: alk help <command>");
}

static int do_help(int argc, char *argv[]);  // defined below

// alk version ----------------------------------------

static void help_version(void) {
	puts("Usage: alk version");
}

static int do_version(int argc, char *argv[]) {
	puts("alk " VERSION);
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
	error("alk help: Invalid subcommand '%s'", argv[1]);
}

int main(int argc, char *argv[]) {
	init(&argc, &argv);

	if (argc == 1) {
		usage();
		return 1;
	}
	for (Command *cmd = commands; cmd->name; cmd++) {
		if (!strcmp(argv[1], cmd->name))
			return cmd->func(argc - 1, argv + 1);
	}
	error("alk: Invalid subcommand '%s'", argv[1]);
}
