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
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_ALD_BASENAME "out"
#define DEFAULT_OUTPUT_AIN "System39.ain"

static const char short_options[] = "a:E:hi:o:p:s:V:v";
static const struct option long_options[] = {
	{ "ain",       required_argument, NULL, 'a' },
	{ "ald",       required_argument, NULL, 'o' },
	{ "encoding",  required_argument, NULL, 'E' },
	{ "hed",       required_argument, NULL, 'i' },
	{ "help",      no_argument,       NULL, 'h' },
	{ "project",   required_argument, NULL, 'p' },
	{ "sys-ver",   required_argument, NULL, 's' },
	{ "variables", required_argument, NULL, 'V' },
	{ "version",   no_argument,       NULL, 'v' },
	{ 0, 0, 0, 0 }
};

static void usage(void) {
	puts("Usage: xsys35c [options] file...");
	puts("Options:");
	puts("    -a, --ain <file>          Write .ain output to <file> (default: " DEFAULT_OUTPUT_AIN ")");
	puts("    -o, --ald <name>          Write output to <name>SA.ALD, <name>SB.ALD, ... (default: " DEFAULT_ALD_BASENAME ")");
	puts("    -Es, --encoding=sjis      Set input coding system to SJIS");
	puts("    -Eu, --encoding=utf8      Set input coding system to UTF-8 (default)");
	puts("    -i, --hed <file>          Read compile header (.hed) from <file>");
	puts("    -h, --help                Display this message and exit");
	puts("    -p, --project <file>      Read project configuration from <file>");
	puts("    -s, --sys-ver <ver>       Target System version (3.5|3.6|3.8|3.9(default))");
	puts("    -V, --variables <file>    Read list of variables from <file>");
	puts("    -v, --version             Print version information and exit");
}

static void version(void) {
	puts("xsys35c " VERSION);
}

static char *read_line(FILE *fp) {
	char buf[256];
	if (!fgets(buf, sizeof(buf), fp))
		return NULL;
	return config.utf8 ? strdup(buf) : sjis2utf(buf);
}

static char *read_file(const char *path) {
	FILE *fp = checked_fopen(path, "rb");
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
	return config.utf8 ? buf : sjis2utf(buf);
}

static char *trim_right(char *str) {
	for (char *p = str + strlen(str) - 1; p >= str && isspace(*p); p--)
		*p = '\0';
	return str;
}

static Vector *read_var_list(const char *path) {
	FILE *fp = checked_fopen(path, "r");
	Vector *vars = new_vec();
	char *line;
	while ((line = read_line(fp)) != NULL)
		vec_push(vars, trim_right(line));
	fclose(fp);
	return vars;
}

static void read_hed(const char *path, Vector *sources, Map *dlls) {
	FILE *fp = checked_fopen(path, "r");
	char *dir = dirname(path);
	enum { INITIAL, SYSTEM35, DLLHeader } section = INITIAL;

	char *line;
	while ((line = read_line(fp)) != NULL) {
		if (line[0] == '\x1a')  // DOS EOF
			break;
		if (line[0] == '#') {  // Section header
			trim_right(line);
			if (!strcmp("#SYSTEM35", line))
				section = SYSTEM35;
			else if (!strcmp("#DLLHeader", line))
				section = DLLHeader;
			else
				error("%s: unknown section %s", path, line);
			continue;
		}
		char *sc = strchr(line, ';');
		if (sc)
			*sc = '\0';
		trim_right(line);
		if (!line[0])
			continue;
		switch (section) {
		case INITIAL:
			error("%s: syntax error", path);
			break;
		case SYSTEM35:
			vec_push(sources, path_join(dir, line));
			break;
		case DLLHeader:
			{
				char *dot = strchr(line, '.');
				if (dot && !strcasecmp(dot + 1, "dll")) {
					*dot = '\0';
					map_put(dlls, line, new_vec());
				} else {
					char *hel_text = read_file(path_join(dir, line));
					Vector *funcs = parse_hel(hel_text, line);
					if (dot)
						*dot = '\0';
					map_put(dlls, line, funcs);
				}
			}
			break;
		}
	}
	fclose(fp);
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
		if (!strcasecmp(dot + 1, "ADV")) {
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

static void build(Vector *src_paths, Vector *variables, Map *dlls, const char *ald_basename, const char *ain_path) {
	Map *srcs = new_map();
	for (int i = 0; i < src_paths->len; i++) {
		char *path = src_paths->data[i];
		const char *name = basename(path);
		map_put(srcs, name, read_file(path));
	}

	Compiler *compiler = new_compiler(srcs->keys, variables, dlls);

	for (int i = 0; i < srcs->keys->len; i++) {
		const char *source = srcs->vals->data[i];
		preprocess(compiler, source, i);
	}

	preprocess_done(compiler);

	uint32_t ald_mask = 0;
	Vector *ald = new_vec();
	for (int i = 0; i < srcs->keys->len; i++) {
		const char *source = srcs->vals->data[i];
		Sco *sco = compile(compiler, source, i);
		AldEntry *e = calloc(1, sizeof(AldEntry));
		e->disk = sco->ald_file_id;
		e->name = utf2sjis_sub(sconame(srcs->keys->data[i]), '?');
		e->timestamp = time(NULL);
		e->data = sco->buf->buf;
		e->size = sco->buf->len;
		vec_push(ald, e);
		if (0 < e->disk && e->disk <= 26)
			ald_mask |= 1 << e->disk;
	}

	if (config.sys_ver == SYSTEM39) {
		FILE *fp = checked_fopen(ain_path, "wb");
		ain_write(compiler, fp);
		fclose(fp);
	}

	for (int i = 1; i <= 26; i++) {
		if (!(ald_mask & 1 << i))
			continue;
		char ald_path[PATH_MAX+1];
		snprintf(ald_path, sizeof(ald_path), "%sS%c.ALD", ald_basename, 'A' + i - 1);
		FILE *fp = checked_fopen(ald_path, "wb");
		ald_write(ald, i, fp);
		fclose(fp);
	}
}

int main(int argc, char *argv[]) {
	init(argc, argv);

	const char *project = NULL;
	const char *ald_basename = NULL;
	const char *output_ain = NULL;
	const char *hed = NULL;
	const char *var_list = NULL;

	int opt;
	while ((opt = getopt_long(argc, argv, short_options, long_options, NULL)) != -1) {
		switch (opt) {
		case 'a':
			output_ain = optarg;
			break;
		case 'E':
			switch (optarg[0]) {
			case 's': case 'S': config.utf8 = false; break;
			case 'u': case 'U': config.utf8 = true; break;
			default: error("Unknown encoding %s", optarg);
			}
			break;
		case 'h':
			usage();
			return 0;
		case 'i':
			hed = optarg;
			break;
		case 'o':
			ald_basename = optarg;
			break;
		case 'p':
			project = optarg;
			break;
		case 's':
			set_sys_ver(optarg);
			break;
		case 'V':
			var_list = optarg;
			break;
		case 'v':
			version();
			return 0;
		case '?':
			usage();
			return 1;
		}
	}
	argc -= optind;
	argv += optind;

	if (project) {
		FILE *fp = checked_fopen(project, "r");
		load_config(fp, dirname(project));
		fclose(fp);
	} else if (!hed && argc == 0) {
		FILE *fp = fopen("xsys35c.cfg", "r");
		if (fp) {
			load_config(fp, NULL);
			fclose(fp);
		} else {
			usage();
			return 1;
		}
	}
	if (!hed && config.hed)
		hed = config.hed;
	if (!var_list && config.var_list)
		var_list = config.var_list;
	if (!ald_basename)
		ald_basename = config.ald_basename ? config.ald_basename : DEFAULT_ALD_BASENAME;
	if (!output_ain)
		output_ain = config.output_ain ? config.output_ain : DEFAULT_OUTPUT_AIN;

	Vector *srcs = new_vec();
	Map *dlls = new_map();
	if (hed)
		read_hed(hed, srcs, dlls);

	for (int i = 0; i < argc; i++)
		vec_push(srcs, argv[i]);

	Vector *vars = var_list ? read_var_list(var_list) : NULL;

	build(srcs, vars, dlls, ald_basename, output_ain);
}
