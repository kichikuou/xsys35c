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
#include "xsys35c.h"
#include <stdlib.h>
#include <string.h>

static Ain ain;

void ain_init(void) {
	memset(&ain, 0, sizeof(ain));
	ain.msg_buf = calloc(1, 4096);
	ain.msg_cap = 4096;
}

void ain_msg_emit(uint8_t b) {
	if (!ain.msg_buf)
		return;
	if (ain.msg_len == ain.msg_cap) {
		ain.msg_cap *= 2;
		ain.msg_buf = realloc(ain.msg_buf, ain.msg_cap);
	}
	ain.msg_buf[ain.msg_len++] = b;
	if (b == '\0')
		ain.msg_count++;
}

int ain_msg_num(void) {
	return ain.msg_count - 1;
}
