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

#define CHUNK_PBNK "pbNk"

struct vsp_header {
	uint16_t x;        // display location x
	uint16_t y;        // display location y
	uint16_t width;    // width
	uint16_t height;   // height
	uint8_t  reserved; // must be zero
	uint8_t  bank;     // default palette bank
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
	puts("Usage: vsp [options] file...");
	puts("Options:");
	puts("    -e, --encode         Convert PNG files to VSP");
	puts("    -h, --help           Display this message and exit");
	puts("    -i, --info           Display image information");
	puts("    -o, --output <file>  Write output to <file>");
	puts("    -v, --version        Print version information and exit");
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

static void vsp_write_header(struct vsp_header *vsp, FILE *fp) {
	fputw(vsp->x, fp);
	fputw(vsp->y, fp);
	fputw(vsp->x + vsp->width, fp);
	fputw(vsp->y + vsp->height, fp);
	fputc(vsp->reserved, fp);
	fputc(vsp->bank, fp);
}

static void vsp_read_palette(png_color pal[16], FILE *fp) {
	for (int i = 0; i < 16; i++) {
		pal[i].blue  = fgetc(fp) * 17;
		pal[i].red   = fgetc(fp) * 17;
		pal[i].green = fgetc(fp) * 17;
	}
}

static void vsp_write_palette(png_color pal[16], FILE *fp) {
	for (int i = 0; i < 16; i++) {
		fputc(pal[i].blue  >> 4, fp);
		fputc(pal[i].red   >> 4, fp);
		fputc(pal[i].green >> 4, fp);
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

static void vsp_encode(struct vsp_header *vsp, png_bytepp rows, FILE *fp) {
	uint8_t *bc[4]; // the current buffer
	uint8_t *bp[4]; // the previous buffer
	for (int i = 0; i < 4; i++) {
		bc[i] = alloca(vsp->height);
		bp[i] = alloca(vsp->height);
	}

	// for each column...
	for (int x = 0; x < vsp->width; x++) {
		// chunky (bitmap) -> planar conversion
		for (int y = 0; y < vsp->height; y++) {
			png_bytep s = rows[y] + x * 8;
			bc[0][y] = (s[0]<<7 & 128) | (s[1]<<6 & 64) | (s[2]<<5 & 32) | (s[3]<<4 & 16)
					 | (s[4]<<3 &   8) | (s[5]<<2 &  4) | (s[6]<<1 &  2) | (s[7]    &  1);
			bc[1][y] = (s[0]<<6 & 128) | (s[1]<<5 & 64) | (s[2]<<4 & 32) | (s[3]<<3 & 16)
					 | (s[4]<<2 &   8) | (s[5]<<1 &  4) | (s[6]    &  2) | (s[7]>>1 &  1);
			bc[2][y] = (s[0]<<5 & 128) | (s[1]<<4 & 64) | (s[2]<<3 & 32) | (s[3]<<2 & 16)
					 | (s[4]<<1 &   8) | (s[5]    &  4) | (s[6]>>1 &  2) | (s[7]>>2 &  1);
			bc[3][y] = (s[0]<<4 & 128) | (s[1]<<3 & 64) | (s[2]<<2 & 32) | (s[3]<<1 & 16)
					 | (s[4]    &   8) | (s[5]>>1 &  4) | (s[6]>>2 &  2) | (s[7]>>3 &  1);
		}
		// for each plane...
		for (int pl = 0; pl < 4; pl++) {
			// for each row...
			for (int y = 0; y < vsp->height;) {
				// Try each command and choose the one with best "saved bytes",
				// i.e. maximum (decoded_length - encoded_length).
				uint8_t code[4];
				int rawlen, codelen;

				// 1-byte raw data
				// if it's < 0x08, prepend 0x07 to distinguish it from commands
				int c = bc[pl][y];
				if (c < 0x08) {
					code[0] = 0x07; code[1] = c;
					codelen = 2;
				} else {
					code[0] = c;
					codelen = 1;
				}
				rawlen = 1;

				// copy n bytes from previous buffer to current buffer
				// (compression for horizontal repetition)
				if (x > 0) {
					int n = 0;
					while (n < 256 && y + n < vsp->height && bc[pl][y + n] == bp[pl][y + n])
						n++;
					if (n - 2 > rawlen - codelen) {
						code[0] = 0x00; code[1] = n - 1;
						codelen = 2;
						rawlen = n;
					}
				}

				// b0 * n (1-byte RLE compression)
				int n = 1;
				while (n < 256 && y + n < vsp->height && bc[pl][y + n] == c)
					n++;
				if (n - 3 > rawlen - codelen) {
					code[0] = 0x01; code[1] = n - 1; code[2] = c;
					codelen = 3;
					rawlen = n;
				}

				// b0,b1 * n (2-byte RLE compression)
				if (y + 1 < vsp->height) {
					int c2 = bc[pl][y+1];
					int n = 1;
					while (n < 256 && y + 2*n + 1 < vsp->height &&
						   bc[pl][y + 2*n] == c && bc[pl][y + 2*n + 1] == c2)
						n++;
					if (n*2 - 4 > rawlen - codelen) {
						code[0] = 0x02; code[1] = n - 1; code[2] = c; code[3] = c2;
						codelen = 4;
						rawlen = 2 * n;
					}
				}

				// copy n bytes from plane p
				for (int p = 0; p < pl; p++) {
					int n = 0;
					while (n < 256 && y + n < vsp->height && bc[pl][y + n] == bc[p][y + n])
						n++;
					if (n - 2 > rawlen - codelen) {
						code[0] = 0x03 + p; code[1] = n - 1;
						codelen = 2;
						rawlen = n;
					}
				}

				// copy n bytes from plane p, bits inverted
				for (int p = 0; p < pl; p++) {
					int n = 0;
					while (n < 256 && y + n < vsp->height && bc[pl][y + n] == (bc[0][y + n] ^ 0xff))
						n++;
					if (n - 3 > rawlen - codelen) {
						code[0] = 0x06; code[1] = 0x03 + p; code[2] = n - 1;
						codelen = 3;
						rawlen = n;
					}
				}

				// write the encoded data
				fwrite(code, codelen, 1, fp);
				y += rawlen;
			}
		}
		// swap current/previous buffers
		for (int i = 0; i < 4; i++) {
			uint8_t *bt = bp[i];
			bp[i] = bc[i];
			bc[i] = bt;
		}
	}
}

static void vsp_to_png(const char *vsp_path, const char *png_path) {
	FILE *fp = checked_fopen(vsp_path, "rb");

	struct vsp_header vsp;
	if (!vsp_read_header(&vsp, fp)) {
		fprintf(stderr, "%s: not a VSP file\n", vsp_path);
		fclose(fp);
		return;
	}

	png_color pal[16];
	vsp_read_palette(pal, fp);

	png_bytepp rows = vsp_extract(&vsp, fp);
	fclose(fp);
	if (!rows) {
		fprintf(stderr, "%s: broken image\n", vsp_path);
		return;
	}

	PngWriter *w = create_png_writer(png_path);

	png_set_IHDR(w->png, w->info, vsp.width * 8, vsp.height, 4,
				 PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
				 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_set_PLTE(w->png, w->info, pal, 16);
	if (vsp.x || vsp.y)
		png_set_oFFs(w->png, w->info, vsp.x * 8, vsp.y, PNG_OFFSET_PIXEL);

	// Store palette bank in a private chunk named "pbNk".
	png_unknown_chunk chunk = {
		.name = CHUNK_PBNK,
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

static void png_to_vsp(const char *png_path, const char *vsp_path) {
	PngReader *r = create_png_reader(png_path);
	if (!r) {
		fprintf(stderr, "%s: not a PNG file\n", png_path);
		return;
	}

	png_set_keep_unknown_chunks(r->png, PNG_HANDLE_CHUNK_ALWAYS, (const uint8_t *)CHUNK_PBNK, 1);
	png_read_png(r->png, r->info, PNG_TRANSFORM_PACKING, NULL);

	png_uint_32 width, height;
	int bit_depth, color_type;
	png_get_IHDR(r->png, r->info, &width, &height, &bit_depth, &color_type, NULL, NULL, NULL);

	if (color_type != PNG_COLOR_TYPE_PALETTE)
		error("%s: not a 16-color image", png_path);
	if (width % 8)
		error("%s: image width must be a multiple of 8", png_path);

	png_colorp palette;
	int num_palette;
	png_get_PLTE(r->png, r->info, &palette, &num_palette);
	if (num_palette != 16)
		error("%s: not a 16-color image", png_path);

	struct vsp_header vsp = {
		.width = width / 8,
		.height = height,
	};

	if (png_get_valid(r->png, r->info, PNG_INFO_oFFs)) {
		png_int_32 offx, offy;
		int unit_type;
		png_get_oFFs(r->png, r->info, &offx, &offy, &unit_type);
		if (unit_type != PNG_OFFSET_PIXEL)
			error("%s: unit of image offset must be pixels", png_path);
		if (offx % 8)
			error("%s: image x-offset must be a multiple of 8", png_path);
		vsp.x = offx / 8;
		vsp.y = offy;
	}

	png_unknown_chunkp unknowns;
	const int num_unknown_chunks = png_get_unknown_chunks(r->png, r->info, &unknowns);
	for (int i = 0; i < num_unknown_chunks; i++) {
		if (strcmp((const char *)unknowns[i].name, CHUNK_PBNK) == 0 && unknowns[i].size == 1) {
			vsp.bank = unknowns[i].data[0];
		}
	}

	FILE *fp = checked_fopen(vsp_path, "wb");
	vsp_write_header(&vsp, fp);
	vsp_write_palette(palette, fp);
	vsp_encode(&vsp, png_get_rows(r->png, r->info), fp);
	fclose(fp);

	destroy_png_reader(r);
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
		printf(", offset: (%d, %d)", vsp.x * 8, vsp.y);
	if (vsp.reserved)
		printf(", reserved: %d", vsp.reserved);
	if (vsp.bank)
		printf(", palette bank: %d", vsp.bank);
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
		error("vsp: multiple input files with specified output filename");

	for (int i = 0; i < argc; i++) {
		switch (mode) {
		case DECODE:
			vsp_to_png(argv[i], output_path ? output_path : replace_suffix(argv[i], ".png"));
			break;
		case ENCODE:
			png_to_vsp(argv[i], output_path ? output_path : replace_suffix(argv[i], ".vsp"));
			break;
		case INFO:
			vsp_info(argv[i]);
			break;
		}
	}
	return 0;
}
