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
#ifndef COMMON_ALD_H
#define COMMON_ALD_H

#include <stdint.h>
#include <stdio.h>
#include "container.h"

typedef struct {
	const char *name;  // in SJIS
	uint64_t timestamp;  // Windows FILETIME (100ns intervals since 1601-01-01)
	const uint8_t *data;
	int size;
	int volume;  // volume id (1 for *A.ALD, 2 for *B.ALD, ...)
} AldEntry;

void ald_write(Vector *entries, int volume, FILE *fp);
Vector *ald_read(Vector *entries, const char *path);

#endif
