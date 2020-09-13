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
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct pms_header {
	uint16_t version;      // PMS version
	uint16_t header_size;  // size of the header
	uint8_t  bpp;          // bits per pixel, 8 or 16
	uint8_t  alpha_bpp;    // alpha channel bit-depth, if exists
	uint8_t  trans_pal;    // transparent color index
	uint8_t  reserved1;    // must be zero
	uint16_t palette_mask; // palette mask
	uint32_t reserved2;    // must be zero
	uint32_t x;            // display location x
	uint32_t y;            // display location y
	uint32_t width;        // image width
	uint32_t height;       // image height
	uint32_t data_off;     // offset to image data
	uint32_t auxdata_off;  // offset to palette or alpha
	uint32_t comment_off;  // offset to comment
};

static const char short_options[] = "hiv";
static const struct option long_options[] = {
	{ "help",      no_argument,       NULL, 'h' },
	{ "info",      no_argument,       NULL, 'i' },
	{ "version",   no_argument,       NULL, 'v' },
	{ 0, 0, 0, 0 }
};

static void usage(void) {
	puts("Usage: pms [options] file...");
	puts("Options:");
	puts("    -h, --help       Display this message and exit");
	puts("    -i, --info       Display image information");
	puts("    -v, --version    Print version information and exit");
}

static void version(void) {
	puts("pms " VERSION);
}

static bool pms_read_header(struct pms_header *pms, FILE *fp) {
	if (fgetc(fp) != 'P' || fgetc(fp) != 'M')
		return false;

	pms->version      = fgetw(fp);
	pms->header_size  = fgetw(fp);
	pms->bpp          = fgetc(fp);
	pms->alpha_bpp    = fgetc(fp);
	pms->trans_pal    = fgetc(fp);
	pms->reserved1    = fgetc(fp);
	pms->palette_mask = fgetw(fp);
	pms->reserved2    = fgetdw(fp);
	pms->x            = fgetdw(fp);
	pms->y            = fgetdw(fp);
	pms->width        = fgetdw(fp);
	pms->height       = fgetdw(fp);
	pms->data_off     = fgetdw(fp);
	pms->auxdata_off  = fgetdw(fp);
	pms->comment_off  = fgetdw(fp);
	return true;
}

static void pms_read_palette(png_color pal[256], FILE *fp) {
	for (int i = 0; i < 256; i++) {
		pal[i].red   = fgetc(fp);
		pal[i].green = fgetc(fp);
		pal[i].blue  = fgetc(fp);
	}
}

/* 
 * Convert PMS8 image to 8-bit indexed bitmap.
 * Based on xsystem35 implementation, with commentary by Nunuhara [1].
 * [1] https://haniwa.technology/tech/pms8.html
 */
static png_bytepp pms8_extract(struct pms_header *pms, FILE *fp) {
	png_bytepp rows = allocate_bitmap_buffer(pms->width, pms->height, 1);

	// for each line...
	for (int y = 0; y < pms->height; y ++) {
		// for each pixel...
		for (int x = 0; x < pms->width; ) {
			uint8_t *dst = rows[y] + x;
			int c0 = fgetc(fp);
			if (c0 == EOF)
				goto err;
			// non-command byte: read 1 pixel into buffer
			if (c0 <= 0xf7) {
				*dst = c0;
				x++;
			}
			// copy n+3 pixels from previous line
			else if (c0 == 0xff) {
				int n = fgetc(fp) + 3;
				if (y < 1 || x + n > pms->width)
					goto err;
				memcpy(dst, rows[y - 1] + x, n);
				x += n;
			}
			// copy n+3 pixels from 2 lines previous
			else if (c0 == 0xfe) {
				int n = fgetc(fp) + 3;
				if (y < 2 || x + n > pms->width)
					goto err;
				memcpy(dst, rows[y - 2] + x, n);
				x += n;
			}
			// repeat 1 pixel n+4 times (1-byte RLE)
			else if (c0 == 0xfd) {
				int n = fgetc(fp) + 4;
				int c0 = fgetc(fp);
				if (x + n > pms->width)
					goto err;
				memset(dst, c0, n);
				x += n;
			}
			// repeat a sequence of 2 pixels n+3 times (2-byte RLE)
			else if (c0 == 0xfc) {
				int n = fgetc(fp) + 3;
				int c0 = fgetc(fp);
				int c1 = fgetc(fp);
				if (x + n * 2 > pms->width)
					goto err;
				for (int i = 0; i < n; i++) {
					*dst++ = c0;
					*dst++ = c1;
				}
				x += n * 2;
			}
			// escape: next byte is image data
			else {
				*dst = fgetc(fp);
				x++;
			}
		}
	}
	return rows;

 err:
	free_bitmap_buffer(rows);
	return NULL;
}

static uint32_t RGB565to888(uint16_t pc) {
	unsigned r = pc & 0xf800;
	unsigned g = pc & 0x07e0;
	unsigned b = pc & 0x001f;
	r = r >> 8 | r >> 13;
	g = g >> 3 | g >> 9;
	b = b << 3 | b >> 2;
	return r | g << 8 | b << 16;
}

/*
 * Convert PMS16 image to RGB888 bitmap. Based on xsystem35 implementation.
 */
static png_bytepp pms16_extract(struct pms_header *pms, FILE *fp) {
	png_bytepp rows = allocate_bitmap_buffer(pms->width, pms->height, 4);

	// for each line...
	for (int y = 0; y < pms->height; y++) {
		// for each pixel...
		for (int x = 0; x < pms->width;) {
			uint32_t *dst = (uint32_t *)rows[y] + x;
			int c0 = fgetc(fp);
			if (c0 == EOF)
				goto err;
			// non-command byte: read 1 pixel into buffer
			if (c0 <= 0xf7) {
				int c1 = fgetc(fp);
				*dst = RGB565to888(c0 | (c1 << 8));
				x++;
			}
			// copy n+2 pixels from previous line
			else if (c0 == 0xff) {
				int n = fgetc(fp) + 2;
				if (y < 1 || x + n > pms->width)
					goto err;
				memcpy(dst, (uint32_t *)rows[y - 1] + x, n * 4);
				x += n;
			}
			// copy n+2 pixels from 2 lines previous
			else if (c0 == 0xfe) {
				int n = fgetc(fp) + 2;
				if (y < 2 || x + n > pms->width)
					goto err;
				memcpy(dst, (uint32_t *)rows[y - 2] + x, n * 4);
				x += n;
			}
			// repeat 1 pixel n+3 times (2-byte RLE)
			else if (c0 == 0xfd) {
				int n = fgetc(fp) + 3;
				uint32_t pc = RGB565to888(fgetw(fp));
				if (x + n > pms->width)
					goto err;
				for (int i = 0; i < n; i++)
					dst[i] = pc;
				x += n;
			}
			// repeat a sequence of 2 pixels n+2 times (4-byte RLE)
			else if (c0 == 0xfc) {
				int n = fgetc(fp) + 2;
				uint32_t pc0 = RGB565to888(fgetw(fp));
				uint32_t pc1 = RGB565to888(fgetw(fp));
				if (x + n * 2 > pms->width)
					goto err;
				for (int i = 0; i < n; i++) {
					*dst++ = pc0;
					*dst++ = pc1;
				}
				x += n * 2;
			}
			// copy 1 pixel from the left pixel of the previous line
			else if (c0 == 0xfb) {
				if (y < 1 || x < 1)
					goto err;
				*dst = ((uint32_t *)rows[y - 1])[x - 1];
				x++;
			}
			// copy 1 pixel from the right pixel of the previous line
			else if (c0 == 0xfa) {
				if (y < 1 || x + 1 >= pms->width)
					goto err;
				*dst = ((uint32_t *)rows[y - 1])[x + 1];
				x++;
			}
			// the next n+1 pixels use common upper 3-2-3 bits of RGB565
			else if (c0 == 0xf9) {
				int n = fgetc(fp) + 1;
				int c0 = fgetc(fp); // read the upper RGB323
				int pc0 = ((c0 & 0xe0) << 8) + ((c0 & 0x18) << 6) + ((c0 & 0x07) << 2);
				if (x + n > pms->width)
					goto err;
				for (int i = 0; i < n; i++) {
					int c1 = fgetc(fp); // read a lower RGB242
					int pc1 = ((c1 & 0xc0) << 5) + ((c1 & 0x3c) << 3) + (c1 & 0x03);
					dst[i] = RGB565to888(pc0 | pc1);
				}
				x += n;
			}
			// escape: next 2 bytes are image data
			else {
				*dst = RGB565to888(fgetw(fp));
				x++;
			}
		}
	}
	return rows;

 err:
	free_bitmap_buffer(rows);
	return NULL;
}

static void pms8_to_png(const char *path, struct pms_header *pms, FILE *fp) {
	png_color pal[256];
	if (fseek(fp, pms->auxdata_off, SEEK_SET) < 0)
		error("%s: %s", path, strerror(errno));
	pms_read_palette(pal, fp);

	if (fseek(fp, pms->data_off, SEEK_SET) < 0)
		error("%s: %s", path, strerror(errno));
	png_bytepp rows = pms8_extract(pms, fp);
	if (!rows) {
		fprintf(stderr, "%s: broken image\n", path);
		return;
	}

	PngWriter *w = create_png_writer(replace_suffix(path, ".png"));

	png_set_IHDR(w->png, w->info, pms->width, pms->height, 8,
				 PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
				 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_set_PLTE(w->png, w->info, pal, 256);
	if (pms->x || pms->y)
		png_set_oFFs(w->png, w->info, pms->x, pms->y, PNG_OFFSET_PIXEL);

	// Store palette mask in a private chunk named "pmSk".
	uint8_t pmsk_data[2] = { pms->palette_mask >> 8, pms->palette_mask & 0xff };  // network byte order
	png_unknown_chunk chunk = {
		.name = "pmSk",
		.data = pmsk_data,
		.size = 2,
		.location = PNG_HAVE_IHDR
	};
	if (pms->palette_mask != 0xffff)
		png_set_unknown_chunks(w->png, w->info, &chunk, 1);

	write_png(w, rows, PNG_TRANSFORM_IDENTITY);

	destroy_png_writer(w);
	free_bitmap_buffer(rows);
}

static void pms16_to_png(const char *path, struct pms_header *pms, FILE *fp) {
	if (fseek(fp, pms->data_off, SEEK_SET) < 0)
		error("%s: %s", path, strerror(errno));
	png_bytepp rows = pms16_extract(pms, fp);
	if (!rows) {
		fprintf(stderr, "%s: broken image\n", path);
		return;
	}

	if (pms->auxdata_off) {
		if (fseek(fp, pms->auxdata_off, SEEK_SET) < 0)
			error("%s: %s", path, strerror(errno));
		png_bytepp alpha_rows = pms8_extract(pms, fp);
		if (!alpha_rows) {
			fprintf(stderr, "%s: broken alpha image\n", path);
			free_bitmap_buffer(rows);
			return;
		}
		merge_alpha_channel(rows, alpha_rows, pms->width, pms->height);
		free_bitmap_buffer(alpha_rows);
	}

	PngWriter *w = create_png_writer(replace_suffix(path, ".png"));

	const int color_type = pms->auxdata_off ?
		PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB;
	png_set_IHDR(w->png, w->info, pms->width, pms->height, 8,
				 color_type, PNG_INTERLACE_NONE,
				 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	png_color_8 sig_bit = { .red = 5, .green = 6, .blue = 5 };
	if (pms->auxdata_off)
		sig_bit.alpha = 8;
	png_set_sBIT(w->png, w->info, &sig_bit);

	if (pms->x || pms->y)
		png_set_oFFs(w->png, w->info, pms->x, pms->y, PNG_OFFSET_PIXEL);

	const int transforms = pms->auxdata_off ?
		PNG_TRANSFORM_IDENTITY : PNG_TRANSFORM_STRIP_FILLER_AFTER;
	write_png(w, rows, transforms);

	destroy_png_writer(w);
	free_bitmap_buffer(rows);
}

static void pms_to_png(const char *path) {
	FILE *fp = checked_fopen(path, "rb");

	struct pms_header pms;
	if (!pms_read_header(&pms, fp)) {
		fprintf(stderr, "%s: not a PMS file\n", path);
		fclose(fp);
		return;
	}

	switch (pms.bpp) {
	case 8:
		pms8_to_png(path, &pms, fp);
		break;
	case 16:
		pms16_to_png(path, &pms, fp);
		break;
	default:
		fprintf(stderr, "%s: invalid bpp %d", path, pms.bpp);
	}
	fclose(fp);
}

static void pms_info(const char *path) {
	struct pms_header pms;
	FILE *fp = checked_fopen(path, "rb");
	if (!pms_read_header(&pms, fp)) {
		fprintf(stderr, "%s: not a PMS file\n", path);
		fclose(fp);
		return;
	}
	fclose(fp);

	printf("%s: PMS %d, %dx%d %dbpp", path, pms.version, pms.width, pms.height, pms.bpp);
	if (pms.bpp == 16 && pms.auxdata_off)
		printf(", %dbit alpha", pms.alpha_bpp ? pms.alpha_bpp : 8);
	if (pms.trans_pal)
		printf(", transparent color: %d", pms.trans_pal);
	if (pms.reserved1)
		printf(", reserved1: %d", pms.reserved1);
	if (pms.palette_mask != 0xffff)
		printf(", palette mask: 0x%x", pms.palette_mask);
	if (pms.reserved2)
		printf(", reserved2: %d", pms.reserved2);
	if (pms.x || pms.y)
		printf(", offset: (%d, %d)", pms.x, pms.y);
	if (pms.comment_off)
		printf(", comment_off: %d", pms.comment_off);
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
			pms_info(argv[i]);
		else
			pms_to_png(argv[i]);
	}
	return 0;
}
