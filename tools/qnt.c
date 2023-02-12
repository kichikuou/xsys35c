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

#define QNT1_HEADER_SIZE 52

struct qnt_header {
	uint32_t version;     // QNT version
	uint32_t header_size; // size of the header
	uint32_t x;           // display location x
	uint32_t y;           // display location y
	uint32_t width;       // image width
	uint32_t height;      // image height
	uint32_t bpp;         // bits per pixel, must be 24
	uint32_t unknown;     // must be 1
	uint32_t pixel_size;  // compressed size of pixel data
	uint32_t alpha_size;  // compressed size of alpha data
};

static const char short_options[] = "ehio:p:v";
static const struct option long_options[] = {
	{ "encode",    no_argument,       NULL, 'e' },
	{ "help",      no_argument,       NULL, 'h' },
	{ "info",      no_argument,       NULL, 'i' },
	{ "output",    required_argument, NULL, 'o' },
	{ "position",  required_argument, NULL, 'p' },
	{ "version",   no_argument,       NULL, 'v' },
	{ 0, 0, 0, 0 }
};

static void usage(void) {
	puts("Usage: qnt [options] file...");
	puts("Options:");
	puts("    -e, --encode          Convert PNG files to QNT");
	puts("    -h, --help            Display this message and exit");
	puts("    -i, --info            Display image information");
	puts("    -o, --output=<file>   Write output to <file>");
	puts("    -p, --position=<x,y>  (encode) Set default display position to (<x,y>)");
	puts("    -v, --version         Print version information and exit");
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

static void qnt_write_header(struct qnt_header *qnt, FILE *fp) {
	fputc('Q', fp);
	fputc('N', fp);
	fputc('T', fp);
	fputc('\0', fp);
	fputdw(qnt->version, fp);
	fputdw(qnt->header_size, fp);
	fputdw(qnt->x, fp);
	fputdw(qnt->y, fp);
	fputdw(qnt->width, fp);
	fputdw(qnt->height, fp);
	fputdw(qnt->bpp, fp);
	fputdw(qnt->unknown, fp);
	fputdw(qnt->pixel_size, fp);
	fputdw(qnt->alpha_size, fp);
	for (int i = 44; i < qnt->header_size; i++)
		fputc(0, fp);
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

static uint8_t *encode_pixels(struct qnt_header *qnt, png_bytepp rows) {
	int width = (qnt->width + 1) & ~1;
	int height = (qnt->height + 1) & ~1;

	const int bufsize = width * height * 3;
	uint8_t *buf = malloc(bufsize);
	uint8_t *p = buf;
	for (int c = 2; c >= 0; c--) {
		for (int y = 0; y < height; y += 2) {
			for (int x = 0; x < width; x += 2) {
				*p++ = rows[y  ][ x   *4 + c];
				*p++ = rows[y+1][ x   *4 + c];
				*p++ = rows[y  ][(x+1)*4 + c];
				*p++ = rows[y+1][(x+1)*4 + c];
			}
		}
	}
	assert(p == buf + bufsize);

	unsigned long destsize = compressBound(bufsize);
	uint8_t *compressed = malloc(destsize);
	int r = compress2(compressed, &destsize, buf, bufsize, Z_BEST_COMPRESSION);
	if (r != Z_OK)
		error("qnt: compress() failed with error code %d", r);
	qnt->pixel_size = destsize;

	free(buf);
	return compressed;
}

static png_bytepp extract_alpha(struct qnt_header *qnt, FILE *fp) {
	int width = (qnt->width + 1) & ~1;
	int height = (qnt->height + 1) & ~1;

	png_bytepp rows = allocate_bitmap_buffer(width, height, 1);

	if (!read_and_uncompress(fp, qnt->alpha_size, rows[0], width * height))
		return NULL;

	return rows;
}

static uint8_t *encode_alpha(struct qnt_header *qnt, png_bytepp rows, bool alpha_only) {
	int width = (qnt->width + 1) & ~1;
	int height = (qnt->height + 1) & ~1;

	const int bufsize = width * height;
	uint8_t *buf = malloc(bufsize);
	for (int y = 0; y < height; y++) {
		if (alpha_only) {
			memcpy(&buf[y * width], rows[y], width);
		} else {
			for (int x = 0; x < width; x++) {
				buf[y * width + x] = rows[y][x * 4 + 3];
			}
		}
	}

	unsigned long destsize = compressBound(bufsize);
	uint8_t *compressed = malloc(destsize);
	int r = compress2(compressed, &destsize, buf, bufsize, Z_BEST_COMPRESSION);
	if (r != Z_OK)
		error("qnt: compress() failed with error code %d", r);
	qnt->alpha_size = destsize;

	free(buf);
	return compressed;
}

static void unfilter(png_bytepp rows, int width, int height, int channels) {
	for (int x = 1; x < width; x++) {
		for (int c = 0; c < channels; c++)
			rows[0][x*channels+c] = rows[0][(x-1)*channels+c] - rows[0][x*channels+c];
	}
	for (int y = 1; y < height; y++) {
		for (int c = 0; c < channels; c++)
			rows[y][c] = rows[y-1][c] - rows[y][c];

		for (int x = 1; x < width; x++) {
			for (int c = 0; c < channels; c++) {
				int up = rows[y-1][x*channels+c];
				int left = rows[y][(x-1)*channels+c];
				rows[y][x*channels+c] = ((up + left) >> 1) - rows[y][x*channels+c];
			}
		}
	}
}

static void filter(png_bytepp rows, int width, int height, int channels) {
	for (int y = height - 1; y > 0; y--) {
		for (int x = width - 1; x > 0; x--) {
			for (int c = 0; c < channels; c++) {
				int up = rows[y-1][x*channels+c];
				int left = rows[y][(x-1)*channels+c];
				rows[y][x*channels+c] = ((up + left) >> 1) - rows[y][x*channels+c];
			}
		}

		for (int c = 0; c < channels; c++)
			rows[y][c] = rows[y-1][c] - rows[y][c];
	}
	for (int x = width - 1; x > 0; x--) {
		for (int c = 0; c < channels; c++)
			rows[0][x*channels+c] = rows[0][(x-1)*channels+c] - rows[0][x*channels+c];
	}
}

static void qnt_to_png(const char *qnt_path, const char *png_path) {
	FILE *fp = checked_fopen(qnt_path, "rb");

	struct qnt_header qnt;
	if (!qnt_read_header(&qnt, fp)) {
		fprintf(stderr, "%s: not a QNT file\n", qnt_path);
		fclose(fp);
		return;
	}
	if (fseek(fp, qnt.header_size, SEEK_SET) < 0)
		error("%s: %s", qnt_path, strerror(errno));

	png_bytepp rows = NULL;
	if (qnt.pixel_size) {
		rows = extract_pixels(&qnt, fp);
		if (!rows) {
			fprintf(stderr, "%s: broken image\n", qnt_path);
			fclose(fp);
			return;
		}
	}
	if (qnt.alpha_size) {
		png_bytepp alpha_rows = extract_alpha(&qnt, fp);
		if (!alpha_rows) {
			fprintf(stderr, "%s: broken alpha image\n", qnt_path);
			fclose(fp);
			free_bitmap_buffer(rows);
			return;
		}
		if (rows) {
			merge_alpha_channel(rows, alpha_rows, qnt.width, qnt.height);
			free_bitmap_buffer(alpha_rows);
		} else {
			rows = alpha_rows;
		}
	}
	fclose(fp);

	if (!rows) {
		fprintf(stderr, "%s: no pixel nor alpha data\n", qnt_path);
		fclose(fp);
		return;
	}

	unfilter(rows, qnt.width, qnt.height, qnt.pixel_size ? 4 : 1);

	PngWriter *w = create_png_writer(png_path);

	const int color_type =
		!qnt.pixel_size ? PNG_COLOR_TYPE_GRAY :
		qnt.alpha_size ? PNG_COLOR_TYPE_RGBA :
		PNG_COLOR_TYPE_RGB;
	png_set_IHDR(w->png, w->info, qnt.width, qnt.height, 8,
				 color_type, PNG_INTERLACE_NONE,
				 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	if (qnt.x || qnt.y)
		png_set_oFFs(w->png, w->info, qnt.x, qnt.y, PNG_OFFSET_PIXEL);

	const int transforms = (color_type == PNG_COLOR_TYPE_RGB) ?
		PNG_TRANSFORM_STRIP_FILLER_AFTER : PNG_TRANSFORM_IDENTITY;
	write_png(w, rows, transforms);

	destroy_png_writer(w);
	free_bitmap_buffer(rows);
}

static void png_to_qnt(const char *png_path, const char *qnt_path, const ImageOffset *image_offset) {
	PngReader *r = create_png_reader(png_path);
	if (!r) {
		fprintf(stderr, "%s: not a PNG file\n", png_path);
		return;
	}

	png_read_info(r->png, r->info);

	struct qnt_header qnt = {
		.version = 1,
		.header_size = QNT1_HEADER_SIZE,
		.width = png_get_image_width(r->png, r->info),
		.height = png_get_image_height(r->png, r->info),
		.bpp = 24,
		.unknown = 1,
	};

	png_set_strip_16(r->png);
	png_set_packing(r->png);

	int color_type = png_get_color_type(r->png, r->info);
	int bytes_per_pixel;
	switch (color_type) {
	case PNG_COLOR_TYPE_RGB:
		png_set_filler(r->png, 0, PNG_FILLER_AFTER);
		bytes_per_pixel = 4;
		break;
	case PNG_COLOR_TYPE_RGBA:
		bytes_per_pixel = 4;
		break;
	case PNG_COLOR_TYPE_GRAY:
		bytes_per_pixel = 1;
		break;
	default:
		error("%s: unsupported color type", png_path);
	}

	if (!image_offset)
		image_offset = get_png_image_offset(r);
	if (image_offset) {
		qnt.x = image_offset->x;
		qnt.y = image_offset->y;
	}

	png_read_update_info(r->png, r->info);
	assert(png_get_rowbytes(r->png, r->info) == qnt.width * bytes_per_pixel);

	// Allocate bitmap memory with the rounded-up size, so that encode_pixels()
	// don't have to deal with boundary conditions.
	int width = (qnt.width + 1) & ~1;
	int height = (qnt.height + 1) & ~1;
	png_bytepp rows = allocate_bitmap_buffer(width, height, bytes_per_pixel);

	png_read_image(r->png, rows);
	png_read_end(r->png, r->info);

	filter(rows, qnt.width, qnt.height, bytes_per_pixel);

	uint8_t *pixel_data = NULL;
	if (color_type != PNG_COLOR_TYPE_GRAY) {
		pixel_data = encode_pixels(&qnt, rows);
	}
	uint8_t *alpha_data = NULL;
	if (color_type != PNG_COLOR_TYPE_RGB) {
		alpha_data = encode_alpha(&qnt, rows, color_type == PNG_COLOR_TYPE_GRAY);
	}

	FILE *fp = checked_fopen(qnt_path, "wb");
	qnt_write_header(&qnt, fp);
	if (pixel_data) {
		fwrite(pixel_data, qnt.pixel_size, 1, fp);
		free(pixel_data);
	}
	if (alpha_data) {
		fwrite(alpha_data, qnt.alpha_size, 1, fp);
		free(alpha_data);
	}
	fclose(fp);

	destroy_png_reader(r);
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

	printf("%s: QNT %d, %dx%d", path, qnt.version, qnt.width, qnt.height);
	if (qnt.pixel_size) {
		printf(" %dbpp", qnt.bpp);
		if (qnt.alpha_size)
			printf(" + alpha");
	} else {
		printf(" alpha only");
	}
	if (qnt.x || qnt.y)
		printf(", offset: (%d, %d)", qnt.x, qnt.y);
	if (qnt.unknown != 1)
		printf(", unknown: %d", qnt.unknown);
	putchar('\n');
}

int main(int argc, char *argv[]) {
	init(&argc, &argv);

	enum { DECODE, ENCODE, INFO } mode = DECODE;
	const char *output_path = NULL;
	ImageOffset *image_offset = NULL;

	int opt;
	while ((opt = getopt_long(argc, argv, short_options, long_options, NULL)) != -1) {
		switch (opt) {
		case 'e':
			mode = ENCODE;
			break;
		case 'h':
			usage();
			return 0;
		case 'i':
			mode = INFO;
			break;
		case 'o':
			output_path = optarg;
			break;
		case 'p':
			image_offset = parse_image_offset(optarg);
			if (!image_offset)
				error("qnt: invalid image position: %s", optarg);
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

	if (argc == 0) {
		usage();
		return 1;
	}
	if (output_path && argc > 1)
		error("qnt: multiple input files with specified output filename");

	for (int i = 0; i < argc; i++) {
		switch (mode) {
		case DECODE:
			qnt_to_png(argv[i], output_path ? output_path : replace_suffix(argv[i], ".png"));
			break;
		case ENCODE:
			png_to_qnt(
				argv[i],
				output_path ? output_path : replace_suffix(argv[i], ".qnt"),
				image_offset);
			break;
		case INFO:
			qnt_info(argv[i]);
			break;
		}
	}
	return 0;
}
