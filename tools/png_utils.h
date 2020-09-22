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
#ifndef PNG_UTILS_H_
#define PNG_UTILS_H_

#include <stdint.h>
#include <stdio.h>
#include <png.h>

typedef struct {
	png_structp png;
	png_infop info;
	FILE *fp;
} PngWriter;

extern PngWriter *create_png_writer(const char *path);
extern void write_png(PngWriter* w, png_bytepp rows, int transforms);
extern void destroy_png_writer(PngWriter* w);

typedef struct {
	png_structp png;
	png_infop info;
	FILE *fp;
} PngReader;

extern PngReader *create_png_reader(const char *path);
extern void destroy_png_reader(PngReader* w);

extern png_bytepp allocate_bitmap_buffer(int width, int height, int bytes_per_pixel);
extern void free_bitmap_buffer(png_bytepp rows);

extern void merge_alpha_channel(png_bytepp rgb_rows, png_bytepp alpha_rows, int width, int height);

extern char *replace_suffix(const char *path, const char *ext);

extern uint16_t fgetw(FILE *fp);
extern uint32_t fgetdw(FILE *fp);
extern void fputw(uint16_t n, FILE *fp);
extern void fputdw(uint32_t n, FILE *fp);

#endif // PNG_UTILS_H_
