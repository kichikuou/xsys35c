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
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

char *strndup_(const char *s, size_t n) {
	char *buf = malloc(n + 1);
	strncpy(buf, s, n);
	buf[n] = '\0';
	return buf;
}

noreturn void error(char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	exit(1);
}

FILE *checked_fopen(const char *path, const char *mode) {
	FILE *fp = fopen(path, mode);
	if (!fp)
		error("cannot open %s: %s", path, strerror(errno));
	return fp;
}

const char *basename(const char *path) {
	char *p = strrchr(path, PATH_SEPARATOR);
	if (!p)
		return path;
	return p + 1;
}

char *dirname(const char *path) {
	char *p = strrchr(path, PATH_SEPARATOR);
	if (!p)
		return ".";
	return strndup_(path, p - path);
}

char *path_join(const char *dir, const char *path) {
	if (!dir || path[0] == PATH_SEPARATOR)
		return strdup(path);
	char *buf = malloc(strlen(dir) + strlen(path) + 2);
	sprintf(buf, "%s%c%s", dir, PATH_SEPARATOR, path);
	return buf;
}

int make_dir(const char *path) {
#if defined(_WIN32)
	return _mkdir(path);
#else
	return mkdir(path, 0777);
#endif
}
