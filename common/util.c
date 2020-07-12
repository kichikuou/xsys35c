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
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#ifdef _WIN32
static char *native_to_utf8(const char *native) {
	int native_len = strlen(native);
	wchar_t *wstr = malloc((native_len + 1) * sizeof(wchar_t));
	if (!MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, native, -1, wstr, native_len + 1))
		error("MultiByteToWideChar(\"%s\") failed with error code 0x%x", native, GetLastError());
	int utf_len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
	char *utf = malloc(utf_len);
	if (!WideCharToMultiByte(CP_UTF8, 0, wstr, -1, utf, utf_len, NULL, NULL))
		error("WideCharToMultiByte(\"%s\") failed with error code 0x%x", native, GetLastError());
	free(wstr);
	return utf;
}
#endif

void init(int argc, char **argv) {
#ifdef _WIN32
	for (int i = 0; i < argc; i++)
		argv[i] = native_to_utf8(argv[i]);
	SetConsoleOutputCP(CP_UTF8);
#endif
}

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

FILE *checked_fopen(const char *path_utf8, const char *mode) {
#ifdef _WIN32
	wchar_t wpath[PATH_MAX + 1];
	if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path_utf8, -1, wpath, PATH_MAX + 1))
		error("MultiByteToWideChar(\"%s\") failed with error code 0x%x", path_utf8, GetLastError());
	wchar_t wmode[64];
	mbstowcs(wmode, mode, 64);
	FILE *fp = _wfopen(wpath, wmode);
#else
	FILE *fp = fopen(path_utf8, mode);
#endif
	if (!fp)
		error("cannot open %s: %s", path_utf8, strerror(errno));
	return fp;
}

int checked_open(const char *path_utf8, int oflag) {
#ifdef _WIN32
	wchar_t wpath[PATH_MAX + 1];
	if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path_utf8, -1, wpath, PATH_MAX + 1))
		error("MultiByteToWideChar(\"%s\") failed with error code 0x%x", path_utf8, GetLastError());
	int fd = _wopen(wpath, oflag);
#else
	int fd = open(path_utf8, oflag);
#endif
	if (fd == -1)
		error("cannot open %s: %s", path_utf8, strerror(errno));
	return fd;
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
