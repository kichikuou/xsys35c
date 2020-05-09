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
#include "xsys35dc.h"
#include <errno.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define make_dir(path, mode) _mkdir(path)
#else
#define make_dir(path, mode) mkdir(path, mode)
#endif

static const char short_options[] = "aE:ho:sVv";
static const struct option long_options[] = {
	{ "address",  no_argument,       NULL, 'a' },
	{ "encoding", required_argument, NULL, 'E' },
	{ "help",     no_argument,       NULL, 'h' },
	{ "outdir",   required_argument, NULL, 'o' },
	{ "seq",      no_argument,       NULL, 's' },
	{ "verbose",  no_argument,       NULL, 'V' },
	{ "version",  no_argument,       NULL, 'v' },
	{ 0, 0, 0, 0 }
};

static void usage(void) {
	puts("Usage: xsys35dc [options] aldfile [ainfile]");
	puts("Options:");
	puts("    -a, --address             Prefix each line with address");
	puts("    -Es, --encoding=sjis      Output files in SJIS encoding (default)");
	puts("    -Eu, --encoding=utf8      Output files in UTF-8 encoding");
	puts("    -h, --help                Display this message and exit");
	puts("    -o, --outdir <directory>  Write output into <directory>");
	puts("    -s, --seq                 Output with sequential filenames (0.adv, 1.adv, ...)");
	puts("    -V, --verbose             Be verbose");
	puts("    -v, --version             Print version information and exit");
}

static void version(void) {
	puts("xsys35dc " VERSION);
}

Sco *sco_new(const char *name, const uint8_t *data, int len) {
	Sco *sco = calloc(1, sizeof(Sco));
	sco->data = data;
	sco->mark = calloc(1, len + 1);
	sco->sco_name = name;
	if (!memcmp(data, "S350", 4))
		sco->version = SCO_S350;
	else if (!memcmp(data, "S351", 4))
		sco->version = SCO_S351;
	else if (!memcmp(data, "153S", 4))
		sco->version = SCO_153S;
	else if (!memcmp(data, "S360", 4))
		sco->version = SCO_S360;
	else if (!memcmp(data, "S380", 4))
		sco->version = SCO_S380;
	else
		error("%s: unknown SCO signature", sjis2utf(name));
	sco->hdrsize = le32(data + 4);
	sco->filesize = le32(data + 8);
	sco->page = le32(data + 12);
	int namelen = data[16] | data[17] << 8;
	sco->src_name = strndup_((char *)data + 18, namelen);

	if (len != sco->filesize) {
		error("%s: unexpected file size in SCO header (expected %d, got %d)",
			  sjis2utf(name), len, sco->filesize);
	}
	return sco;
}

void convert_to_utf8(FILE *fp) {
	fseek(fp, 0, SEEK_SET);
	Vector *lines = new_vec();
	char buf[2048];
	while (fgets(buf, sizeof(buf), fp))
		vec_push(lines, sjis2utf(buf));

	fseek(fp, 0, SEEK_SET);
	for (int i = 0; i < lines->len; i++)
		fputs(lines->data[i], fp);
	// No truncation needed because UTF-8 encoding is no shorter than SJIS.
}

int main(int argc, char *argv[]) {
	const char *outdir = NULL;
	bool seq = false;

	int opt;
	while ((opt = getopt_long(argc, argv, short_options, long_options, NULL)) != -1) {
		switch (opt) {
		case 'a':
			config.address = true;
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
		case 'o':
			outdir = optarg;
			break;
		case 's':
			seq = true;
			break;
		case 'V':
			config.verbose = true;
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

	if (argc != 1 && argc != 2) {
		usage();
		return 1;
	}

	Vector *scos = ald_read(NULL, argv[0]);
	for (int i = 0; i < scos->len; i++) {
		AldEntry *e = scos->data[i];
		Sco *sco = sco_new(e->name, e->data, e->size);
		scos->data[i] = sco;
		if (seq) {
			char buf[16];
			sprintf(buf, "%d.adv", i);
			sco->src_name = strdup(buf);
		}
	}
	Ain *ain = NULL;
	if (argc >= 2)
		ain = ain_read(argv[1]);

	if (outdir && make_dir(outdir, 0777) != 0 && errno != EEXIST)
		error("cannot create directory %s: %s", outdir, strerror(errno));

	decompile(scos, ain, outdir);

	return 0;
}
