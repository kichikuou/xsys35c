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

struct vsp_header {
	uint16_t x;        // display location x
	uint16_t y;        // display location y
	uint16_t width;    // width
	uint16_t height;   // height
	uint8_t  reserved; // must be zero
	uint8_t  bank;     // default palette bank
};

static const char short_options[] = "hiv";
static const struct option long_options[] = {
	{ "help",      no_argument,       NULL, 'h' },
	{ "info",      no_argument,       NULL, 'i' },
	{ "version",   no_argument,       NULL, 'v' },
	{ 0, 0, 0, 0 }
};

static void usage(void) {
	puts("Usage: vsp [options] file...");
	puts("Options:");
	puts("    -h, --help       Display this message and exit");
	puts("    -i, --info       Display image information");
	puts("    -v, --version    Print version information and exit");
}

static void version(void) {
	puts("vsp " VERSION);
}

static bool vsp_read_header(struct vsp_header *vsp, FILE *fp) {
	vsp->x = fgetw(fp);
	vsp->y = fgetw(fp);
	vsp->width = fgetw(fp) - vsp->x;
	vsp->height = fgetw(fp) - vsp->y;
	vsp->reserved = fgetc(fp);
	vsp->bank = fgetc(fp);

	// 401 for dalk's broken CG
	if (vsp->x > 80 || vsp->y > 400 || vsp->width > 80 || vsp->height > 401 || vsp->bank > 15)
		return false;
	return true;
}

static void vsp_read_palette(png_color pal[16], FILE *fp) {
	for (int i = 0; i < 16; i++) {
		pal[i].blue  = fgetc(fp) * 17;
		pal[i].red   = fgetc(fp) * 17;
		pal[i].green = fgetc(fp) * 17;
	}
}

/*
 * Convert VSP planar image data to 8-bit indexed bitmap.
 * Based on xsystem35 implementation, with commentary by Nunuhara [1].
 * [1] https://haniwa.technology/tech/vsp.html
 */
static png_bytepp vsp_extract(struct vsp_header *vsp, FILE *fp) {
	png_bytepp rows = allocate_bitmap_buffer(vsp->width * 8, vsp->height, 1);

	// Extraction buffers. The planar image data is decompressed and read into
	// these buffers before being converted to a chunky format.
	uint8_t *bc[4]; // the current buffer
	uint8_t *bp[4]; // the previous buffer

	for (int i = 0; i < 4; i++) {
		bc[i] = alloca(vsp->height);
		bp[i] = alloca(vsp->height);
	}

	uint8_t mask = 0;

	// for each column...
	// NOTE: Every byte contains 8 pixels worth of data for single plane, so
	//       each column is actually 8 pixels wide.
	for (int x = 0; x < vsp->width; x++) {
		// for each plane...
		for (int pl = 0; pl < 4; pl++) {
			// for each row...
			for (int y = 0; y < vsp->height;) {
				// read a byte; if it's < 0x08, it's a command byte,
				// otherwise it's image data
				int c0 = fgetc(fp);
				if (c0 == EOF)
					error("unexpected EOF");
				// copy byte into buffer
				if (c0 >= 0x08) {
					bc[pl][y] = c0;
					y++;
				}
				// copy n bytes from previous buffer to current buffer
				// (compression for horizontal repetition)
				else if (c0 == 0x00) {
					int n = fgetc(fp) + 1;
					if (y + n > vsp->height)
						goto err;
					memcpy(bc[pl] + y, bp[pl] + y, n);
					y += n;
				}
				// b0 * n (1-byte RLE compression)
				else if (c0 == 0x01) {
					int n = fgetc(fp) + 1;
					uint8_t b0 = fgetc(fp);
					if (y + n > vsp->height)
						goto err;
					memset(bc[pl] + y, b0, n);
					y += n;
				}
				// b0,b1 * n (2-byte RLE compression)
				else if (c0 == 0x02) {
					int n = fgetc(fp) + 1;
					uint8_t b0 = fgetc(fp);
					uint8_t b1 = fgetc(fp);
					if (y + n * 2 > vsp->height)
						goto err;
					for (int i = 0; i < n; i++) {
						bc[pl][y++] = b0;
						bc[pl][y++] = b1;
					}
				}
				// copy n bytes from plane 0 XOR'd by the current mask
				else if (c0 == 0x03) {
					int n = fgetc(fp) + 1;
					if (y + n > vsp->height)
						goto err;
					for (int i = 0; i < n; i++) {
						bc[pl][y] = bc[0][y] ^ mask;
						y++;
					}
					mask = 0;
				}
				// copy n bytes from plane 1 XOR'd by the current mask
				else if (c0 == 0x04) {
					int n = fgetc(fp) + 1;
					if (y + n > vsp->height)
						goto err;
					for (int i = 0; i < n; i++) {
						bc[pl][y] = bc[1][y] ^ mask;
						y++;
					}
					mask = 0;
				}
				// copy n bytes from plane 2 XOR'd by the current mask
				else if (c0 == 0x05) {
					int n = fgetc(fp) + 1;
					if (y + n > vsp->height)
						goto err;
					for (int i = 0; i < n; i++) {
						bc[pl][y] = bc[2][y] ^ mask;
						y++;
					}
					mask = 0;
				}
				// invert the mask
				else if (c0 == 0x06) {
					mask = 0xff;
				}
				// escape: next byte is image data
				else if (c0 == 0x07) {
					bc[pl][y] = fgetc(fp);
					y++;
				}
			}
		}
		// planar -> chunky (bitmap) conversion
		for (int y = 0; y < vsp->height; y++) {
			png_bytep dst = rows[y] + x * 8;
			uint8_t b0 = bc[0][y];
			uint8_t b1 = bc[1][y];
			uint8_t b2 = bc[2][y];
			uint8_t b3 = bc[3][y];
			// NOTE: Half of every byte is wasted, since VSP is actually
			//	   a 4-bit format.
			dst[0] = ((b0>>7)&1) | ((b1>>6)&2) | ((b2>>5)&4) | ((b3>>4)&8);
			dst[1] = ((b0>>6)&1) | ((b1>>5)&2) | ((b2>>4)&4) | ((b3>>3)&8);
			dst[2] = ((b0>>5)&1) | ((b1>>4)&2) | ((b2>>3)&4) | ((b3>>2)&8);
			dst[3] = ((b0>>4)&1) | ((b1>>3)&2) | ((b2>>2)&4) | ((b3>>1)&8);
			dst[4] = ((b0>>3)&1) | ((b1>>2)&2) | ((b2>>1)&4) | ((b3   )&8);
			dst[5] = ((b0>>2)&1) | ((b1>>1)&2) | ((b2   )&4) | ((b3<<1)&8);
			dst[6] = ((b0>>1)&1) | ((b1   )&2) | ((b2<<1)&4) | ((b3<<2)&8);
			dst[7] = ((b0   )&1) | ((b1<<1)&2) | ((b2<<2)&4) | ((b3<<3)&8);
		}
		// swap current/previous buffers
		for (int i = 0; i < 4; i++) {
			uint8_t *bt = bp[i];
			bp[i] = bc[i];
			bc[i] = bt;
		}
	}
	return rows;

 err:
	free_bitmap_buffer(rows);
	return NULL;
}

static void vsp_to_png(const char *path) {
	FILE *fp = checked_fopen(path, "rb");

	struct vsp_header vsp;
	if (!vsp_read_header(&vsp, fp)) {
		fprintf(stderr, "%s: not a VSP file\n", path);
		fclose(fp);
		return;
	}

	png_color pal[16];
	vsp_read_palette(pal, fp);

	png_bytepp rows = vsp_extract(&vsp, fp);
	fclose(fp);
	if (!rows) {
		fprintf(stderr, "%s: broken image\n", path);
		return;
	}

	PngWriter *w = create_png_writer(replace_suffix(path, ".png"));

	png_set_IHDR(w->png, w->info, vsp.width * 8, vsp.height, 4,
				 PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
				 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_set_PLTE(w->png, w->info, pal, 16);
	if (vsp.x || vsp.y)
		png_set_oFFs(w->png, w->info, vsp.x, vsp.y, PNG_OFFSET_PIXEL);

	// Store palette bank in a private chunk named "pbNk".
	png_unknown_chunk chunk = {
		.name = "pbNk",
		.data = &vsp.bank,
		.size = 1,
		.location = PNG_HAVE_IHDR
	};
	if (vsp.bank)
		png_set_unknown_chunks(w->png, w->info, &chunk, 1);

	write_png(w, rows, PNG_TRANSFORM_PACKING);

	destroy_png_writer(w);
	free_bitmap_buffer(rows);
}

static void vsp_info(const char *path) {
	struct vsp_header vsp;
	FILE *fp = checked_fopen(path, "rb");
	if (!vsp_read_header(&vsp, fp)) {
		fprintf(stderr, "%s: not a VSP file\n", path);
		fclose(fp);
		return;
	}
	fclose(fp);

	printf("%s: %dx%d", path, vsp.width * 8, vsp.height);
	if (vsp.x || vsp.y)
		printf(", offset: (%d, %d)", vsp.x, vsp.y);
	if (vsp.reserved)
		printf(", reserved: %d", vsp.reserved);
	if (vsp.bank)
		printf(", palette bank: %d", vsp.bank);
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
			vsp_info(argv[i]);
		else
			vsp_to_png(argv[i]);
	}
	return 0;
}
