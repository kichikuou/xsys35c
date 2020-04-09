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
#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
	LOPT_OBJDIR = 256,
	LOPT_TIMESTAMP,
};

typedef struct {
	const char *opt_val;
	SysVer sys_ver;
	ScoVer sco_ver;
} SysVerOptValue;

static const SysVerOptValue sys_ver_opt_values[] = {
	{"3.5", SYSTEM35, SCO_S351},
	{"3.6", SYSTEM36, SCO_S360},
	{"3.8", SYSTEM38, SCO_S380},
	{"3.9", SYSTEM39, SCO_S380},
	{"S350", SYSTEM35, SCO_S350},
	{"S351", SYSTEM35, SCO_S351},
	{"153S", SYSTEM36, SCO_153S},
	{"S360", SYSTEM36, SCO_S360},
	{"S380", SYSTEM39, SCO_S380},
	{NULL, 0, 0},
};

static const char short_options[] = "hi:o:s:V:v";
static const struct option long_options[] = {
	{ "help",        no_argument,       NULL, 'h' },
	{ "output",      required_argument, NULL, 'o' },
	{ "objdir",      required_argument, NULL, LOPT_OBJDIR },
	{ "source-list", required_argument, NULL, 'i' },
	{ "sys-ver",     required_argument, NULL, 's' },
	{ "timestamp",   required_argument, NULL, LOPT_TIMESTAMP },
	{ "variables",   required_argument, NULL, 'V' },
	{ "version",     no_argument,       NULL, 'v' },
	{ 0, 0, 0, 0 }
};

static time_t timestamp = (time_t)-1;

static void usage(void) {
	puts("Usage: xsys35c [options] file...");
	puts("Options:");
	puts("    -h, --help                Display this message and exit");
	puts("        --objdir <directory>  Write object (.sco) files into <directory>");
	puts("    -o, --output <file>       Write output to <file> (default: adisk.ald)");
	puts("    -i, --source-list <file>  Read list of source files from <file>");
	puts("    -s, --sys-ver <ver>       Target System version (3.5|3.6|3.8(default)|3.9)");
	puts("        --timestamp <time>    Set timestamp of ALD entries, in UNIX timestamp");
	puts("    -V, --variables <file>    Read list of variables from <file>");
	puts("    -v, --version             Print version information and exit");
}

static void version(void) {
	puts("xsys35c " VERSION);
}

static char *read_file(const char *path) {
	FILE *fp = fopen(path, "rb");
	if (!fp)
		error("%s: %s", path, strerror(errno));
	if (fseek(fp, 0, SEEK_END) != 0)
		error("%s: %s", path, strerror(errno));
	long size = ftell(fp);
	if (size < 0)
		error("%s: %s", path, strerror(errno));
	if (fseek(fp, 0, SEEK_SET) != 0)
		error("%s: %s", path, strerror(errno));
	char *buf = malloc(size + 1);
	if (size > 0 && fread(buf, size, 1, fp) != 1)
		error("%s: read error", path);
	fclose(fp);
	buf[size] = '\0';
	return buf;
}

static char *trim_right(char *str) {
	for (char *p = str + strlen(str) - 1; p >= str && isspace(*p); p--)
		*p = '\0';
	return str;
}

static Vector *read_var_list(const char *path) {
	FILE *fp = fopen(path, "r");
	if (!fp)
		error("%s: %s", path, strerror(errno));
	Vector *vars = new_vec();
	char line[256];
	while (fgets(line, sizeof(line), fp))
		vec_push(vars, strdup(trim_right(line)));
	fclose(fp);
	return vars;
}

// Read a list of source files from `path`, in the "comp.hed" format of System3.x SDK.
static Vector *read_source_list(const char *path) {
	FILE *fp = fopen(path, "r");
	if (!fp)
		error("%s: %s", path, strerror(errno));
	char *dir = dirname(path);
	Vector *files = new_vec();
	char line[256];
	while (fgets(line, sizeof(line), fp)) {
		if (line[0] == '#' || line[0] == '\x1a')
			continue;
		char *sc = strchr(line, ';');
		if (sc)
			*sc = '\0';
		trim_right(line);
		if (!line[0])
			continue;
		vec_push(files, path_join(dir, sjis2utf(line)));
	}
	fclose(fp);
	return files;
}

static char *sconame(const char *advname) {
	// If advname ends with ".adv", replace it with ".sco"
	char *dot = strrchr(advname, '.');
	if (dot) {
		if (!strcmp(dot + 1, "adv")) {
			char *s = strdup(advname);
			strcpy(s + strlen(advname) - 3, "sco");
			return s;
		}
		if (!strcmp(dot + 1, "ADV")) {
			char *s = strdup(advname);
			strcpy(s + strlen(advname) - 3, "SCO");
			return s;
		}
	}
	// Otherwise appends ".sco" to advname
	char *s = malloc(strlen(advname) + 5);
	strcpy(s, advname);
	strcat(s, ".sco");
	return s;
}

static Vector *build_ald(Vector *src_paths, Vector *variables, const char *objdir) {
	Map *srcs = new_map();
	for (int i = 0; i < src_paths->len; i++) {
		char *path = src_paths->data[i];
		const char *name = basename(path);
		map_put(srcs, utf2sjis(name), read_file(path));
	}

	Compiler compiler;
	compiler_init(&compiler, srcs->keys, variables);

	for (int i = 0; i < srcs->keys->len; i++) {
		const char *source = srcs->vals->data[i];
		preprocess(&compiler, source, i);
	}

	if (objdir) {
		char *tblpath = path_join(objdir, "variables.tbl");
		FILE *fp = fopen(tblpath, "w");
		if (!fp)
			error("%s: %s", tblpath, strerror(errno));
		for (int i = 0; i < compiler.variables->len; i++)
			fprintf(fp, "%s\n", compiler.variables->data[i]);
		fclose(fp);
	}

	preprocess_done(&compiler);

	Vector *ald = new_vec();
	for (int i = 0; i < srcs->keys->len; i++) {
		const char *source = srcs->vals->data[i];
		Buffer *sco = compile(&compiler, source, i);
		AldEntry *e = calloc(1, sizeof(AldEntry));
		e->name = sconame(srcs->keys->data[i]);
		e->timestamp = (timestamp == (time_t)-1) ? time(NULL) : timestamp;
		e->data = sco->buf;
		e->size = sco->len;
		vec_push(ald, e);
	}

	if (objdir) {
		for (int i = 0; i < ald->len; i++) {
			AldEntry *e = ald->data[i];
			char *objpath = path_join(objdir, sjis2utf(e->name));
			FILE *fp = fopen(objpath, "wb");
			if (!fp)
				error("%s: %s", objpath, strerror(errno));
			fwrite(e->data, e->size, 1, fp);
			fclose(fp);
		}
	}

	if (sys_ver == SYSTEM39) {
		FILE *fp = fopen("System39.ain", "wb");
		if (!fp)
			error("System39.ain: %s", strerror(errno));
		ain_write(&compiler, fp);
		fclose(fp);
	}

	return ald;
}

int main(int argc, char *argv[]) {
	const char *objdir = NULL;
	const char *output = "adisk.ald";
	const char *source_list = NULL;
	const char *var_list = NULL;

	int opt;
	while ((opt = getopt_long(argc, argv, short_options, long_options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			usage();
			return 0;
		case 'i':
			source_list = optarg;
			break;
		case 'o':
			output = optarg;
			break;
		case 's':
			for (const SysVerOptValue *v = sys_ver_opt_values;; v++) {
				if (!v->opt_val)
					error("Unknown system version '%s'", optarg);
				if (!strcmp(optarg, v->opt_val)) {
					sys_ver = v->sys_ver;
					sco_ver = v->sco_ver;
					break;
				}
			}
			break;
		case 'V':
			var_list = optarg;
			break;
		case 'v':
			version();
			return 0;
		case LOPT_OBJDIR:
			objdir = optarg;
			break;
		case LOPT_TIMESTAMP:
			timestamp = atol(optarg);
			break;
		case '?':
			usage();
			return 1;
		}
	}
	argc -= optind;
	argv += optind;

	if (!source_list && argc < 1) {
		usage();
		return 1;
	}

	Vector *srcs = source_list ? read_source_list(source_list) : new_vec();
	for (int i = 0; i < argc; i++)
		vec_push(srcs, argv[i]);

	Vector *vars = var_list ? read_var_list(var_list) : NULL;

	Vector *ald = build_ald(srcs, vars, objdir);

	FILE *fp = fopen(output, "wb");
	if (!fp)
		error("%s: %s", output, strerror(errno));
	ald_write(ald, fp);
	fclose(fp);
}
