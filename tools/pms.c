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

#define PMS1_HEADER_SIZE 48
#define CHUNK_PMSK "pmSk"

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
	uint32_t reserved3;    // must be zero
};

static const char short_options[] = "ehio:v";
static const struct option long_options[] = {
	{ "encode",    no_argument,       NULL, 'e' },
	{ "help",      no_argument,       NULL, 'h' },
	{ "info",      no_argument,       NULL, 'i' },
	{ "output",    required_argument, NULL, 'o' },
	{ "version",   no_argument,       NULL, 'v' },
	{ 0, 0, 0, 0 }
};

static void usage(void) {
	puts("Usage: pms [options] file...");
	puts("Options:");
	puts("    -e, --encode         Convert PNG files to PMS");
	puts("    -h, --help           Display this message and exit");
	puts("    -i, --info           Display image information");
	puts("    -o, --output <file>  Write output to <file>");
	puts("    -v, --version        Print version information and exit");
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
	pms->reserved3    = fgetdw(fp);
	return true;
}

static void pms_write_header(struct pms_header *pms, FILE *fp) {
	fputc('P', fp);
	fputc('M', fp);
	fputw(pms->version, fp);
	fputw(pms->header_size, fp);
	fputc(pms->bpp, fp);
	fputc(pms->alpha_bpp, fp);
	fputc(pms->trans_pal, fp);
	fputc(pms->reserved1, fp);
	fputw(pms->palette_mask, fp);
	fputdw(pms->reserved2, fp);
	fputdw(pms->x, fp);
	fputdw(pms->y, fp);
	fputdw(pms->width, fp);
	fputdw(pms->height, fp);
	fputdw(pms->data_off, fp);
	fputdw(pms->auxdata_off, fp);
	fputdw(pms->comment_off, fp);
	fputdw(pms->reserved3, fp);
}

static void pms_read_palette(png_color pal[256], FILE *fp) {
	for (int i = 0; i < 256; i++) {
		pal[i].red   = fgetc(fp);
		pal[i].green = fgetc(fp);
		pal[i].blue  = fgetc(fp);
	}
}

static void pms_write_palette(png_color pal[256], int n, FILE *fp) {
	for (int i = 0; i < n; i++) {
		fputc(pal[i].red, fp);
		fputc(pal[i].green, fp);
		fputc(pal[i].blue, fp);
	}
	for (int i = n; i < 256; i++) {
		fputc(0, fp);
		fputc(0, fp);
		fputc(0, fp);
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

static void pms8_encode(struct pms_header *pms, png_bytepp rows, FILE *fp) {
	// for each line...
	for (int y = 0; y < pms->height; y ++) {
		// for each pixel...
		for (int x = 0; x < pms->width; ) {
			// Try each command and choose the one with best "saved bytes",
			// i.e. maximum (decoded_length - encoded_length).
			uint8_t code[4];
			int rawlen, codelen;

			// 1-pixel raw data
			// if it's >= 0xf8, prepend 0xf8 to distinguish it from commands
			int c = rows[y][x];
			if (c >= 0xf8) {
				code[0] = 0xf8; code[1] = c;
				codelen = 2;
			} else {
				code[0] = c;
				codelen = 1;
			}
			rawlen = 1;

			// copy n+3 pixels from previous line
			if (y > 0) {
				int n = 0;
				while (n - 3 < 255 && x + n < pms->width && rows[y][x + n] == rows[y - 1][x + n])
					n++;
				if (n >= 3 && n - 2 > rawlen - codelen) {
					code[0] = 0xff; code[1] = n - 3;
					codelen = 2;
					rawlen = n;
				}
			}

			// copy n+3 pixels from 2 lines previous
			if (y > 1) {
				int n = 0;
				while (n - 3 < 255 && x + n < pms->width && rows[y][x + n] == rows[y - 2][x + n])
					n++;
				if (n >= 3 && n - 2 > rawlen - codelen) {
					code[0] = 0xfe; code[1] = n - 3;
					codelen = 2;
					rawlen = n;
				}
			}

			// repeat 1 pixel n+4 times (1-byte RLE)
			{
				int n = 1;
				while (n - 4 < 255 && x + n < pms->width && rows[y][x + n] == c)
					n++;
				if (n >= 4 && n - 3 > rawlen - codelen) {
					code[0] = 0xfd; code[1] = n - 4; code[2] = c;
					codelen = 3;
					rawlen = n;
				}
			}

			// repeat a sequence of 2 pixels n+3 times (2-byte RLE)
			if (x + 1 < pms->width) {
				int c2 = rows[y][x + 1];
				int n = 1;
				while (n - 3 < 255 && x + 2*n + 1 < pms->width &&
					   rows[y][x + 2*n] == c && rows[y][x + 2*n + 1] == c2)
					n++;
				if (n >= 3 && 2*n - 4 > rawlen - codelen) {
					code[0] = 0xfc; code[1] = n - 3; code[2] = c; code[3] = c2;
					codelen = 4;
					rawlen = 2 * n;
				}
			}

			// write the encoded data
			fwrite(code, codelen, 1, fp);
			x += rawlen;
		}
	}
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
			// copy the upper-left pixel
			else if (c0 == 0xfb) {
				if (y < 1 || x < 1)
					goto err;
				*dst = ((uint32_t *)rows[y - 1])[x - 1];
				x++;
			}
			// copy the upper-right pixel
			else if (c0 == 0xfa) {
				if (y < 1 || x + 1 >= pms->width)
					goto err;
				*dst = ((uint32_t *)rows[y - 1])[x + 1];
				x++;
			}
			// use common upper 3-2-3 bits of RGB565 in the next n+1 pixels
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

static void convert_rgba8888_to_rgb565(png_bytepp src_rows, png_bytepp dst_rows, int width, int height) {
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int r = src_rows[y][x * 4];
			int g = src_rows[y][x * 4 + 1];
			int b = src_rows[y][x * 4 + 2];
			((uint16_t *)dst_rows[y])[x] = (r & 0xf8) << 8 | (g & 0xfc) << 3 | (b >> 3);
		}
	}
}

static void convert_rgba8888_to_alpha(png_bytepp src_rows, png_bytepp dst_rows, int width, int height) {
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++)
			dst_rows[y][x] = src_rows[y][x * 4 + 3];
	}
}

// Write a run of raw pixel data, using the 0xf9 command when possible
static void write_raw_pixel_run(uint16_t *pixels, int len, FILE *fp) {
	for (int x = 0; x < len; ) {
		const int mask = 0xe61c;
		int upper = pixels[x] & mask;
		int n = 1;
		while (n - 1 < 255 && x + n < len && (pixels[x + n] & mask) == upper)
			n++;
		if (n > 2) {
			// use common upper 3-2-3 bits of RGB565 in the next n+1 pixels
			fputc(0xf9, fp);
			fputc(n - 1, fp);
			fputc((upper & 0xe000) >> 8 | (upper & 0x600) >> 6 | (upper & 0x1c) >> 2, fp);
			for (int i = 0; i < n; i++) {
				int c = pixels[x + i];
				fputc((c & 0x1800) >> 5 | (c & 0x1e0) >> 3 | (c & 0x3), fp);
			}
			x += n;
		} else {
			// 1-pixel raw data
			// if the first byte is >= 0xf8, prepend 0xf8 to distinguish it from commands
			if ((pixels[x] & 0xff) >= 0xf8)
				fputc(0xf8, fp);
			fputw(pixels[x], fp);
			x++;
		}
	}
}

static void pms16_encode(struct pms_header *pms, png_bytepp rgb8888_rows, FILE *fp) {
	png_bytepp rows = allocate_bitmap_buffer(pms->width, pms->height, 2);
	convert_rgba8888_to_rgb565(rgb8888_rows, rows, pms->width, pms->height);

	// for each line...
	for (int y = 0; y < pms->height; y ++) {
		uint16_t *row = (uint16_t *)rows[y];
		int raw_pixel_run_length = 0;
		// for each pixel...
		for (int x = 0; x < pms->width; ) {
			// Try commands except for 0xf9, because greedy use of the 0xf9 command
			// worsen the compression rate.
			uint8_t code[6];
			int rawlen, codelen;

#define SCORE(pixels, encoded_length) (2 * (pixels) - (encoded_length))

			// 1-pixel raw data
			int c = row[x];
			code[0] = 0;  // raw pixel data will be encoded in write_raw_pixel_run()
			codelen = ((c & 0xff) >= 0xf8) ? 3 : 2;
			rawlen = 1;

			// copy n+2 pixels from previous line
			if (y > 0) {
				int n = 0;
				while (n - 2 < 255 && x + n < pms->width && row[x + n] == ((uint16_t *)rows[y - 1])[x + n])
					n++;
				if (n >= 2 && SCORE(n, 2) > SCORE(rawlen, codelen)) {
					code[0] = 0xff; code[1] = n - 2;
					codelen = 2;
					rawlen = n;
				}
			}

			// copy n+2 pixels from 2 lines previous
			if (y > 1) {
				int n = 0;
				while (n - 2 < 255 && x + n < pms->width && row[x + n] == ((uint16_t *)rows[y - 2])[x + n])
					n++;
				if (n >= 2 && SCORE(n, 2) > SCORE(rawlen, codelen)) {
					code[0] = 0xfe; code[1] = n - 2;
					codelen = 2;
					rawlen = n;
				}
			}

			// repeat 1 pixel n+3 times (2-byte RLE)
			{
				int n = 1;
				while (n - 3 < 255 && x + n < pms->width && row[x + n] == c)
					n++;
				if (n >= 3 && SCORE(n, 4) > SCORE(rawlen, codelen)) {
					code[0] = 0xfd; code[1] = n - 3; code[2] = c & 0xff; code[3] = c >> 8;
					codelen = 4;
					rawlen = n;
				}
			}

			// repeat a sequence of 2 pixels n+2 times (4-byte RLE)
			if (x + 1 < pms->width && c != row[x + 1]) {
				int c2 = row[x + 1];
				int n = 1;
				while (n - 2 < 255 && x + 2*n + 1 < pms->width &&
					   row[x + 2*n] == c && row[x + 2*n + 1] == c2)
					n++;
				if (n >= 2 && SCORE(2 * n, 6) > SCORE(rawlen, codelen)) {
					code[0] = 0xfc; code[1] = n - 2;
					code[2] = c & 0xff; code[3] = c >> 8;
					code[4] = c2 & 0xff; code[5] = c2 >> 8;
					codelen = 6;
					rawlen = 2 * n;
				}
			}

			// copy the upper-left pixel
			if (y > 0 && x > 0 &&
				c == ((uint16_t *)rows[y-1])[x-1] &&
				SCORE(1, 1) > SCORE(rawlen, codelen)) {
				code[0] = 0xfb;
				codelen = 1;
				rawlen = 1;
			}

			// copy the upper-right pixel
			if (y > 0 && x + 1 < pms->width &&
				c == ((uint16_t *)rows[y-1])[x+1] &&
				SCORE(1, 1) > SCORE(rawlen, codelen)) {
				code[0] = 0xfa;
				codelen = 1;
				rawlen = 1;
			}
#undef SCORE

			if (!code[0]) {
				raw_pixel_run_length++;
				x++;
			} else {
				// flush the pending raw pixel data
				write_raw_pixel_run(&row[x - raw_pixel_run_length], raw_pixel_run_length, fp);
				raw_pixel_run_length = 0;

				// write the encoded data
				fwrite(code, codelen, 1, fp);
				x += rawlen;
			}
		}
		write_raw_pixel_run(&row[pms->width - raw_pixel_run_length], raw_pixel_run_length, fp);
	}

	free_bitmap_buffer(rows);
}

static void pms8_to_png(struct pms_header *pms, FILE *fp, const char *pms_path, const char *png_path) {
	png_color pal[256];
	if (fseek(fp, pms->auxdata_off, SEEK_SET) < 0)
		error("%s: %s", pms_path, strerror(errno));
	pms_read_palette(pal, fp);

	if (fseek(fp, pms->data_off, SEEK_SET) < 0)
		error("%s: %s", pms_path, strerror(errno));
	png_bytepp rows = pms8_extract(pms, fp);
	if (!rows) {
		fprintf(stderr, "%s: broken image\n", pms_path);
		return;
	}

	PngWriter *w = create_png_writer(png_path);

	png_set_IHDR(w->png, w->info, pms->width, pms->height, 8,
				 PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
				 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_set_PLTE(w->png, w->info, pal, 256);
	if (pms->x || pms->y)
		png_set_oFFs(w->png, w->info, pms->x, pms->y, PNG_OFFSET_PIXEL);

	// Store palette mask in a private chunk named "pmSk".
	uint8_t pmsk_data[2] = { pms->palette_mask >> 8, pms->palette_mask & 0xff };  // network byte order
	png_unknown_chunk chunk = {
		.name = CHUNK_PMSK,
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

static void pms16_to_png(struct pms_header *pms, FILE *fp, const char *pms_path, const char *png_path) {
	if (fseek(fp, pms->data_off, SEEK_SET) < 0)
		error("%s: %s", pms_path, strerror(errno));
	png_bytepp rows = pms16_extract(pms, fp);
	if (!rows) {
		fprintf(stderr, "%s: broken image\n", pms_path);
		return;
	}

	if (pms->auxdata_off) {
		if (fseek(fp, pms->auxdata_off, SEEK_SET) < 0)
			error("%s: %s", pms_path, strerror(errno));
		png_bytepp alpha_rows = pms8_extract(pms, fp);
		if (!alpha_rows) {
			fprintf(stderr, "%s: broken alpha image\n", pms_path);
			free_bitmap_buffer(rows);
			return;
		}
		merge_alpha_channel(rows, alpha_rows, pms->width, pms->height);
		free_bitmap_buffer(alpha_rows);
	}

	PngWriter *w = create_png_writer(png_path);

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

static void pms_to_png(const char *pms_path, const char *png_path) {
	FILE *fp = checked_fopen(pms_path, "rb");

	struct pms_header pms;
	if (!pms_read_header(&pms, fp)) {
		fprintf(stderr, "%s: not a PMS file\n", pms_path);
		fclose(fp);
		return;
	}

	switch (pms.bpp) {
	case 8:
		pms8_to_png(&pms, fp, pms_path, png_path);
		break;
	case 16:
		pms16_to_png(&pms, fp, pms_path, png_path);
		break;
	default:
		fprintf(stderr, "%s: invalid bpp %d", pms_path, pms.bpp);
	}
	fclose(fp);
}

static void png_to_pms8(PngReader *r, const char *png_path, const char *pms_path) {
	struct pms_header pms = {
		.version      = 1,
		.header_size  = PMS1_HEADER_SIZE,
		.bpp          = 8,
		.palette_mask = 0xffff,
		.width        = png_get_image_width(r->png, r->info),
		.height       = png_get_image_height(r->png, r->info),
		.data_off     = PMS1_HEADER_SIZE + 3 * 256,
		.auxdata_off  = PMS1_HEADER_SIZE,
	};

	png_colorp palette;
	int num_palette;
	png_get_PLTE(r->png, r->info, &palette, &num_palette);
	if (num_palette > 256)
		error("%s: not a 256-color image", png_path);

	if (png_get_valid(r->png, r->info, PNG_INFO_oFFs)) {
		png_int_32 offx, offy;
		int unit_type;
		png_get_oFFs(r->png, r->info, &offx, &offy, &unit_type);
		if (unit_type != PNG_OFFSET_PIXEL)
			error("%s: unit of image offset must be pixels", png_path);
		pms.x = offx;
		pms.y = offy;
	}

	png_unknown_chunkp unknowns;
	const int num_unknown_chunks = png_get_unknown_chunks(r->png, r->info, &unknowns);
	for (int i = 0; i < num_unknown_chunks; i++) {
		if (strcmp((const char *)unknowns[i].name, CHUNK_PMSK) == 0 && unknowns[i].size == 2) {
			pms.palette_mask = unknowns[i].data[0] << 8 | unknowns[i].data[1];
		}
	}

	png_bytepp rows = allocate_bitmap_buffer(pms.width, pms.height, 1);
	png_read_image(r->png, rows);
	png_read_end(r->png, r->info);

	FILE *fp = checked_fopen(pms_path, "wb");
	pms_write_header(&pms, fp);
	pms_write_palette(palette, num_palette, fp);
	pms8_encode(&pms, rows, fp);
	fclose(fp);

	free_bitmap_buffer(rows);
}

static void png_to_pms16(PngReader *r, const char *png_path, const char *pms_path) {
	png_set_strip_16(r->png);
	png_set_packing(r->png);

	int color_type = png_get_color_type(r->png, r->info);
	if (color_type == PNG_COLOR_TYPE_RGB)
		png_set_filler(r->png, 0, PNG_FILLER_AFTER);

	struct pms_header pms = {
		.version      = 1,
		.header_size  = PMS1_HEADER_SIZE,
		.bpp          = 16,
		.alpha_bpp    = color_type == PNG_COLOR_TYPE_RGBA ? 8 : 0,
		.palette_mask = 0xffff,
		.width        = png_get_image_width(r->png, r->info),
		.height       = png_get_image_height(r->png, r->info),
		.data_off     = PMS1_HEADER_SIZE,
	};

	png_color_8p sig_bit = NULL;
	if (png_get_valid(r->png, r->info, PNG_INFO_sBIT))
		png_get_sBIT(r->png, r->info, &sig_bit);
	if (!sig_bit || sig_bit->red != 5 || sig_bit->green != 6 || sig_bit->blue != 5)
		fprintf(stderr, "%s: not an RGB565 image; conversion will be lossy.\n", png_path);

	if (png_get_valid(r->png, r->info, PNG_INFO_oFFs)) {
		png_int_32 offx, offy;
		int unit_type;
		png_get_oFFs(r->png, r->info, &offx, &offy, &unit_type);
		if (unit_type != PNG_OFFSET_PIXEL)
			error("%s: unit of image offset must be pixels", png_path);
		pms.x = offx;
		pms.y = offy;
	}

	png_read_update_info(r->png, r->info);
	assert(png_get_rowbytes(r->png, r->info) == pms.width * 4);

	png_bytepp rows = allocate_bitmap_buffer(pms.width, pms.height, 4);
	png_read_image(r->png, rows);
	png_read_end(r->png, r->info);

	FILE *fp = checked_fopen(pms_path, "wb");
	pms_write_header(&pms, fp);

	pms16_encode(&pms, rows, fp);

	if (color_type == PNG_COLOR_TYPE_RGBA) {
		pms.auxdata_off = ftell(fp);

		png_bytepp alpha_rows = allocate_bitmap_buffer(pms.width, pms.height, 1);
		convert_rgba8888_to_alpha(rows, alpha_rows, pms.width, pms.height);
		pms8_encode(&pms, alpha_rows, fp);
		free_bitmap_buffer(alpha_rows);

		fseek(fp, 0, SEEK_SET);
		pms_write_header(&pms, fp);  // Update the auxdata_off field
	}

	fclose(fp);

	free_bitmap_buffer(rows);
}

static void png_to_pms(const char *png_path, const char *pms_path) {
	PngReader *r = create_png_reader(png_path);
	if (!r) {
		fprintf(stderr, "%s: not a PNG file\n", png_path);
		return;
	}
	png_set_keep_unknown_chunks(r->png, PNG_HANDLE_CHUNK_ALWAYS, (const uint8_t *)CHUNK_PMSK, 1);

	png_read_info(r->png, r->info);

	switch (png_get_color_type(r->png, r->info)) {
	case PNG_COLOR_TYPE_PALETTE:
		png_to_pms8(r, png_path, pms_path);
		break;
	case PNG_COLOR_TYPE_RGB:
	case PNG_COLOR_TYPE_RGBA:
		png_to_pms16(r, png_path, pms_path);
		break;
	default:
		error("%s: grayscale png is not supported", png_path);
	}
	destroy_png_reader(r);
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

	enum { DECODE, ENCODE, INFO } mode = DECODE;
	const char *output_path = NULL;

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

	if (output_path && argc > 1)
		error("pms: multiple input files with specified output filename");

	for (int i = 0; i < argc; i++) {
		switch (mode) {
		case DECODE:
			pms_to_png(argv[i], output_path ? output_path : replace_suffix(argv[i], ".png"));
			break;
		case ENCODE:
			png_to_pms(argv[i], output_path ? output_path : replace_suffix(argv[i], ".pms"));
			break;
		case INFO:
			pms_info(argv[i]);
			break;
		}
	}
	return 0;
}
