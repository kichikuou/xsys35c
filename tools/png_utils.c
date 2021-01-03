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

ImageOffset *parse_image_offset(const char *s) {
	static ImageOffset offs;
	if (sscanf(s, "%d,%d", &offs.x, &offs.y) != 2)
		return NULL;
	return &offs;
}

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

void write_png(PngWriter *w, png_bytepp rows, int transforms) {
	png_set_rows(w->png, w->info, rows);
	png_write_png(w->png, w->info, transforms, NULL);
}

void destroy_png_writer(PngWriter *w) {
	png_destroy_write_struct(&w->png, &w->info);
	fclose(w->fp);
	memset(w, 0, sizeof(PngWriter));
}

PngReader *create_png_reader(const char *path) {
	FILE *fp = checked_fopen(path, "rb");
	png_byte sig_bytes[8];
	if (fread(sig_bytes, sizeof(sig_bytes), 1, fp) != 1)
		return NULL;
	if (png_sig_cmp(sig_bytes, 0, sizeof(sig_bytes)) != 0)
		return NULL;

	PngReader *r = calloc(1, sizeof(PngReader));
	r->png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, handle_png_error, NULL);
	if (!r->png)
		error("png_create_read_struct failed");

	r->info = png_create_info_struct(r->png);
	if (!r->info)
		error("png_create_info_struct failed");

	r->fp = fp;
	png_init_io(r->png, r->fp);
	png_set_sig_bytes(r->png, sizeof(sig_bytes));

	r->path = strdup(path);

	return r;
}

void destroy_png_reader(PngReader *r) {
	png_destroy_read_struct(&r->png, &r->info, NULL);
	fclose(r->fp);
	free(r->path);
	memset(r, 0, sizeof(PngReader));
}

ImageOffset *get_png_image_offset(PngReader *r) {
	static ImageOffset offs;
	if (!png_get_valid(r->png, r->info, PNG_INFO_oFFs))
		return NULL;
	int unit_type;
	png_get_oFFs(r->png, r->info, &offs.x, &offs.y, &unit_type);
	if (unit_type != PNG_OFFSET_PIXEL)
		error("%s: unit of image offset must be pixels", r->path);
	return &offs;
}

png_unknown_chunkp get_png_unknown_chunk(PngReader *r, const char *name) {
	png_unknown_chunkp unknown;
	const int n = png_get_unknown_chunks(r->png, r->info, &unknown);
	for (int i = 0; i < n; i++, unknown++) {
		if (strcmp((const char *)unknown->name, name) == 0)
			return unknown;
	}
	return NULL;
}

png_bytepp allocate_bitmap_buffer(int width, int height, int bytes_per_pixel) {
	png_bytepp rows = malloc(sizeof(png_bytep) * height);
	png_bytep buffer = calloc(1, height * width * bytes_per_pixel);
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
	strcpy(buf + (dot - path), ext);
	return buf;
}
