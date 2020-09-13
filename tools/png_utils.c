/*
 * Copyright (C) 2020 <KichikuouChrome@gmail.com>
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
#include "png_utils.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>

static void handle_png_error(png_structp png, png_const_charp error_msg) {
	error("PNG error: %s", error_msg);
}

PngWriter *create_png_writer(const char *path) {
	PngWriter *w = calloc(1, sizeof(PngWriter));
	w->png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, handle_png_error, NULL);
	if (!w->png)
		error("png_create_write_struct failed");

	w->info = png_create_info_struct(w->png);
	if (!w->info)
		error("png_create_info_struct failed");

	w->fp = checked_fopen(path, "wb");
	png_init_io(w->png, w->fp);

	return w;
}

void write_png(PngWriter* w, png_bytepp rows, int transforms) {
	png_set_rows(w->png, w->info, rows);
	png_write_png(w->png, w->info, transforms, NULL);
}

void destroy_png_writer(PngWriter* w) {
	png_destroy_write_struct(&w->png, &w->info);
	fclose(w->fp);
	memset(w, 0, sizeof(PngWriter));
}

png_bytepp allocate_bitmap_buffer(int width, int height, int bytes_per_pixel) {
	png_bytepp rows = malloc(sizeof(png_bytep) * height);
	png_bytep buffer = malloc(height * width * bytes_per_pixel);
	for (int y = 0; y < height; y++)
		rows[y] = buffer + y * width * bytes_per_pixel;
	return rows;
}

void free_bitmap_buffer(png_bytepp rows) {
	free(rows[0]);
	free(rows);
}

void merge_alpha_channel(png_bytepp rgb_rows, png_bytepp alpha_rows, int width, int height) {
	for (int y = 0; y < height; y++) {
		uint8_t *dst = rgb_rows[y] + 3;
		uint8_t *src = alpha_rows[y];
		for (int x = 0; x < width; x++) {
			*dst = *src;
			dst += 4;
			src += 1;
		}
	}
}

char *replace_suffix(const char *path, const char *ext) {
	const char *dot = strrchr(path, '.');
	if (!dot || strchr(dot + 1, '/'))
		dot = path + strlen(path);
	char *buf = malloc(dot - path + strlen(ext) + 1);
	strncpy(buf, path, dot - path);
	strcat(buf, ext);
	return buf;
}

uint16_t fgetw(FILE *fp) {
	int lo = fgetc(fp);
	int hi = fgetc(fp);
	return lo | hi << 8;
}

uint32_t fgetdw(FILE *fp) {
	int lo = fgetw(fp);
	int hi = fgetw(fp);
	return lo | hi << 16;
}
