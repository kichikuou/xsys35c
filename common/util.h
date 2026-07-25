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
#ifndef COMMON_UTIL_H
#define COMMON_UTIL_H

#include <dirent.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdnoreturn.h>
#include <time.h>

static inline uint32_t le32(const uint8_t *p) {
	return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

static inline uint64_t le64(const uint8_t *p) {
	return le32(p) | (uint64_t)le32(p + 4) << 32;
}

void init(int *argc, char ***argv);
char *strndup_(const char *s, size_t n);
noreturn void error(char *fmt, ...);
FILE *checked_fopen(const char *path_utf8, const char *mode);
int checked_open(const char *path_utf8, int oflag);

char *basename_utf8(const char *path);
char *dirname_utf8(const char *path);
char *path_join(const char *dir, const char *path);
int make_dir(const char *path_utf8);
void mkdir_p(const char *path_utf8);

#ifdef _WIN32
typedef _WDIR UDIR;
typedef struct _stat64 ustat;
#else
typedef DIR UDIR;
typedef struct stat ustat;
#endif

UDIR *opendir_utf8(const char *path);
int closedir_utf8(UDIR *dir);
char *readdir_utf8(UDIR *dir);
int stat_utf8(const char *path, ustat *st);

uint16_t fgetw(FILE *fp);
uint32_t fgetdw(FILE *fp);
uint64_t fget64(FILE *fp);
void fputw(uint16_t n, FILE *fp);
void fputdw(uint32_t n, FILE *fp);
void fput64(uint64_t n, FILE *fp);

time_t win_filetime_to_time_t(uint64_t filetime);
uint64_t time_t_to_win_filetime(time_t t);
uint64_t current_win_filetime(void);

#endif
