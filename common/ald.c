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

#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _POSIX_MAPPED_FILES
#include <sys/mman.h>
#endif
#ifndef _O_BINARY
#define _O_BINARY 0
#endif

#define ALD_SIGNATURE  0x14c4e
#define ALD_SIGNATURE2 0x12020

// 1970-01-01 - 1601-01-01 in 100ns
#define EPOCH_DIFF_100NS 116444736000000000LL

static void write_dword(int n, FILE *fp) {
	fputc(n & 0xff, fp);
	fputc(n >> 8 & 0xff, fp);
	fputc(n >> 16 & 0xff, fp);
	fputc(n >> 24 & 0xff, fp);
}

static void write_ptr(int size, int *sector, FILE *fp) {
	*sector += (size + 0xff) >> 8;
	fputc(*sector & 0xff, fp);
	fputc(*sector >> 8 & 0xff, fp);
	fputc(*sector >> 16 & 0xff, fp);
}

static void pad(FILE *fp) {
	// Align to next sector boundary
	long pos = ftell(fp);
	for (; pos & 0xff; pos++)
		fputc(0, fp);
}

static int entry_header_size(AldEntry *e) {
	int namelen = strlen(e->name) + 1;  // length including null terminator
	return (namelen + 31) & ~0xf;
}

static void write_entry(AldEntry *entry, FILE *fp) {
	uint64_t wtime = entry->timestamp * 10000000LL + EPOCH_DIFF_100NS;
	int hdrlen = entry_header_size(entry);
	write_dword(hdrlen, fp);
	write_dword(entry->size, fp);
	write_dword(wtime & 0xffffffff, fp);
	write_dword(wtime >> 32, fp);
	fputs(entry->name, fp);
	for (int i = 16 + strlen(entry->name); i < hdrlen; i++)
		fputc(0, fp);
	fwrite(entry->data, entry->size, 1, fp);
}

void ald_write(Vector *entries, int volume, FILE *fp) {
	int sector = 0;

	int ptr_count = 0;
	for (int i = 0; i < entries->len; i++) {
		AldEntry *entry = entries->data[i];
		if (entry->volume == volume)
			ptr_count++;
	}

	write_ptr((ptr_count + 2) * 3, &sector, fp);
	write_ptr(entries->len * 3, &sector, fp);
	for (int i = 0; i < entries->len; i++) {
		AldEntry *entry = entries->data[i];
		if (entry->volume == volume)
			write_ptr(entry_header_size(entry) + entry->size, &sector, fp);
	}
	pad(fp);

	uint16_t link[256];
	memset(link, 0, sizeof(link));
	for (int i = 0; i < entries->len; i++) {
		AldEntry *entry = entries->data[i];
		fputc(entry->volume, fp);
		link[entry->volume]++;
		fputc(link[entry->volume] & 0xff, fp);
		fputc(link[entry->volume] >> 8, fp);
	}
	pad(fp);

	for (int i = 0; i < entries->len; i++) {
		AldEntry *entry = entries->data[i];
		if (entry->volume != volume)
			continue;
		write_entry(entry, fp);
		pad(fp);
	}

	// Footer
	write_dword(ALD_SIGNATURE, fp);
	write_dword(0x10, fp);
	write_dword(ptr_count << 8 | volume, fp);
	write_dword(0, fp);
}

static inline uint8_t *ald_sector(uint8_t *ald, int index) {
	uint8_t *p = ald + index * 3;
	return ald + (p[0] << 8 | p[1] << 16 | p[2] << 24);
}

static time_t to_unix_time(uint32_t wtime_l, uint32_t wtime_h) {
	uint64_t wtime = (uint64_t)wtime_h << 32 | wtime_l;
	return (wtime - EPOCH_DIFF_100NS) / 10000000LL;
}

static void ald_read_entries(Vector *entries, int volume, uint8_t *data, int len) {
	uint8_t *link_sector = ald_sector(data, 0);
	uint8_t *link_sector_end = ald_sector(data, 1);

	for (uint8_t *link = link_sector; link < link_sector_end; link += 3) {
		uint8_t vol_nr = link[0];
		uint16_t ptr_nr = link[1] | link[2] << 8;
		if (vol_nr != volume)
			continue;
		uint8_t *entry_ptr = ald_sector(data, ptr_nr);
		AldEntry *e = calloc(1, sizeof(AldEntry));
		e->volume = volume;
		e->name = (char *)entry_ptr + 16;
		e->timestamp = to_unix_time(le32(entry_ptr + 8), le32(entry_ptr + 12));
		e->data = entry_ptr + le32(entry_ptr);
		e->size = le32(entry_ptr + 4);
		vec_set(entries, (link - link_sector) / 3, e);
	}
}

Vector *ald_read(Vector *entries, const char *path) {
	if (!entries)
		entries = new_vec();

	int fd = checked_open(path, O_RDONLY | _O_BINARY);

	struct stat sbuf;
	if (fstat(fd, &sbuf) < 0)
		error("%s: %s", path, strerror(errno));

#ifdef _POSIX_MAPPED_FILES
	uint8_t *p = mmap(NULL, sbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		error("%s: %s", path, strerror(errno));
#else
	uint8_t *p = malloc(sbuf.st_size);
	size_t bytes = 0;
	while (bytes < sbuf.st_size) {
		ssize_t ret = read(fd, p + bytes, sbuf.st_size - bytes);
		if (ret <= 0)
			error("%s: %s", path, strerror(errno));
		bytes += ret;
	}
#endif
	close(fd);

	if ((sbuf.st_size & 0xff) != 16)
		error("%s: unexpected file size (not an ald file?)", path);
	uint8_t *footer = p + sbuf.st_size - 16;
	if (le32(footer) != ALD_SIGNATURE && le32(footer) != ALD_SIGNATURE2)
		error("%s: invalid ALD signature", path);

	ald_read_entries(entries, footer[8], p, sbuf.st_size);

	return entries;
}
