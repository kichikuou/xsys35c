/*
 * Copyright (C) 2020 <KichikuouChrome@gmail.com>
 * Copyright (C) 1997-1998 Masaki Chikama (Wren) <chikama@kasumi.ipl.mech.nagoya-u.ac.jp>
 *               1998-                           <masaki-c@is.aist-nara.ac.jp>
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
#include "png_utils.h"
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

struct qnt_header {
	uint32_t version;     // QNT version
	uint32_t header_size; // size of the header
	uint32_t x;           // display location x
	uint32_t y;           // display location y
	uint32_t width;       // image width
	uint32_t height;      // image height
	uint32_t bpp;         // bits per pixel, 8 or 16
	uint32_t unknown;     // must be 1
	uint32_t pixel_size;  // compressed size of pixel data
	uint32_t alpha_size;  // compressed size of alpha data
	
};

static const char short_options[] = "hiv";
static const struct option long_options[] = {
	{ "help",      no_argument,       NULL, 'h' },
	{ "info",      no_argument,       NULL, 'i' },
	{ "version",   no_argument,       NULL, 'v' },
	{ 0, 0, 0, 0 }
};

static void usage(void) {
	puts("Usage: qnt [options] file...");
	puts("Options:");
	puts("    -h, --help       Display this message and exit");
	puts("    -i, --info       Display image information");
	puts("    -v, --version    Print version information and exit");
}

static void version(void) {
	puts("qnt " VERSION);
}

static bool read_and_uncompress(FILE *fp, uint32_t compressed_size,
								uint8_t *raw, unsigned long raw_size) {
	uint8_t *compressed = malloc(compressed_size);
	if (!compressed)
		return NULL;
	if (fread(compressed, compressed_size, 1, fp) != 1)
		goto err;
	unsigned long uncompressed_size = raw_size;
	if (uncompress(raw, &uncompressed_size, compressed, compressed_size) != Z_OK)
		goto err;
	if (uncompressed_size != raw_size)
		goto err;
	free(compressed);
	return true;

 err:
	free(compressed);
	return false;
}

static bool qnt_read_header(struct qnt_header *qnt, FILE *fp) {
	if (fgetc(fp) != 'Q' || fgetc(fp) != 'N' || fgetc(fp) != 'T' || fgetc(fp) != 0)
		return false;

	qnt->version     = fgetdw(fp);
	qnt->header_size = qnt->version ? fgetdw(fp) : 48;
	qnt->x           = fgetdw(fp);
	qnt->y           = fgetdw(fp);
	qnt->width       = fgetdw(fp);
	qnt->height      = fgetdw(fp);
	qnt->bpp         = fgetdw(fp);
	qnt->unknown     = fgetdw(fp);
	qnt->pixel_size  = fgetdw(fp);
	qnt->alpha_size  = fgetdw(fp);
	return true;
}

static png_bytepp extract_pixels(struct qnt_header *qnt, FILE *fp) {
	int width = (qnt->width + 1) & ~1;
	int height = (qnt->height + 1) & ~1;

	const int bufsize = width * height * 3;
	uint8_t *raw = malloc(bufsize);
	if (!raw || !read_and_uncompress(fp, qnt->pixel_size, raw, bufsize))
		return NULL;

	png_bytepp rows = allocate_bitmap_buffer(width, height, 4);

	uint8_t *p = raw;
	for (int c = 2; c >= 0; c--) {
		for (int y = 0; y < height; y += 2) {
			for (int x = 0; x < width; x += 2) {
				rows[y  ][ x    * 4 + c] = *p++;
				rows[y+1][ x    * 4 + c] = *p++;
				rows[y  ][(x+1) * 4 + c] = *p++;
				rows[y+1][(x+1) * 4 + c] = *p++;
			}
		}
	}
	assert(p == raw + bufsize);
	free(raw);

	return rows;
}

static png_bytepp extract_alpha(struct qnt_header *qnt, FILE *fp) {
	int width = (qnt->width + 1) & ~1;
	int height = (qnt->height + 1) & ~1;

	png_bytepp rows = allocate_bitmap_buffer(width, height, 1);

	if (!read_and_uncompress(fp, qnt->alpha_size, rows[0], width * height))
		return NULL;

	return rows;
}

static void unfilter(png_bytepp rows, int width, int height) {
	for (int x = 1; x < width; x++) {
		for (int c = 0; c < 4; c++)
			rows[0][x*4+c] = rows[0][(x-1)*4+c] - rows[0][x*4+c];
	}
	for (int y = 1; y < height; y++) {
		for (int c = 0; c < 4; c++)
			rows[y][c] = rows[y-1][c] - rows[y][c];

		for (int x = 1; x < width; x++) {
			for (int c = 0; c < 4; c++) {
				int up = rows[y-1][x*4+c];
				int left = rows[y][(x-1)*4+c];
				rows[y][x*4+c] = ((up + left) >> 1) - rows[y][x*4+c];
			}
		}
	}
}

static void qnt_to_png(const char *path) {
	FILE *fp = checked_fopen(path, "rb");

	struct qnt_header qnt;
	if (!qnt_read_header(&qnt, fp)) {
		fprintf(stderr, "%s: not a QNT file\n", path);
		fclose(fp);
		return;
	}

	if (fseek(fp, qnt.header_size, SEEK_SET) < 0)
		error("%s: %s", path, strerror(errno));
	png_bytepp rows = extract_pixels(&qnt, fp);
	if (!rows) {
		fprintf(stderr, "%s: broken image\n", path);
		fclose(fp);
		return;
	}
	if (qnt.alpha_size) {
		png_bytepp alpha_rows = extract_alpha(&qnt, fp);
		if (!alpha_rows) {
			fprintf(stderr, "%s: broken alpha image\n", path);
			fclose(fp);
			free_bitmap_buffer(rows);
			return;
		}
		merge_alpha_channel(rows, alpha_rows, qnt.width, qnt.height);
		free_bitmap_buffer(alpha_rows);
	}
	fclose(fp);

	unfilter(rows, qnt.width, qnt.height);

	PngWriter *w = create_png_writer(replace_suffix(path, ".png"));

	const int color_type = qnt.alpha_size ?
		PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB;
	png_set_IHDR(w->png, w->info, qnt.width, qnt.height, 8,
				 color_type, PNG_INTERLACE_NONE,
				 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	if (qnt.x || qnt.y)
		png_set_oFFs(w->png, w->info, qnt.x, qnt.y, PNG_OFFSET_PIXEL);

	const int transforms = qnt.alpha_size ?
		PNG_TRANSFORM_IDENTITY : PNG_TRANSFORM_STRIP_FILLER_AFTER;
	write_png(w, rows, transforms);

	destroy_png_writer(w);
	free_bitmap_buffer(rows);
}

static void qnt_info(const char *path) {
	struct qnt_header qnt;
	FILE *fp = checked_fopen(path, "rb");
	if (!qnt_read_header(&qnt, fp)) {
		fprintf(stderr, "%s: not a QNT file\n", path);
		fclose(fp);
		return;
	}
	fclose(fp);

	printf("%s: QNT %d, %dx%d %dbpp", path, qnt.version, qnt.width, qnt.height, qnt.bpp);
	if (qnt.alpha_size)
		printf(" + alpha");
	if (qnt.x || qnt.y)
		printf(", offset: (%d, %d)", qnt.x, qnt.y);
	if (qnt.unknown != 1)
		printf(", unknown: %d", qnt.unknown);
	putchar('\n');
}

int main(int argc, char *argv[]) {
	init(argc, argv);

	bool opt_info = false;
	int opt;
	while ((opt = getopt_long(argc, argv, short_options, long_options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			usage();
			return 0;
		case 'i':
			opt_info = true;
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

	for (int i = 0; i < argc; i++) {
		if (opt_info)
			qnt_info(argv[i]);
		else
			qnt_to_png(argv[i]);
	}
	return 0;
}
