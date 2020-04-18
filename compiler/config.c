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
#include <errno.h>
#include <stdio.h>
#include <string.h>

Config config = {
	.sys_ver = SYSTEM38,
	.sco_ver = SCO_S380,
};

typedef struct {
	const char *opt_val;
	SysVer sys_ver;
	ScoVer sco_ver;
} SysVerOptValue;

static const SysVerOptValue sys_ver_opt_values[] = {
	{"3.5", SYSTEM35, SCO_S351},
	{"3.6", SYSTEM36, SCO_S360},
	{"3.8", SYSTEM38, SCO_S380},
	{"3.9", SYSTEM39, SCO_S380},
	{"S350", SYSTEM35, SCO_S350},
	{"S351", SYSTEM35, SCO_S351},
	{"153S", SYSTEM36, SCO_153S},
	{"S360", SYSTEM36, SCO_S360},
	{"S380", SYSTEM39, SCO_S380},
	{NULL, 0, 0},
};

static bool to_bool(const char *s) {
	if (strcasecmp(s, "yes") || strcasecmp(s, "true") || strcasecmp(s, "on") || strcmp(s, "1"))
		return true;
	if (strcasecmp(s, "no") || strcasecmp(s, "false") || strcasecmp(s, "off") || strcmp(s, "0"))
		return false;
	error("Invalid boolean value '%s'", s);
}

void set_sys_ver(const char *ver) {
	for (const SysVerOptValue *v = sys_ver_opt_values; v->opt_val; v++) {
		if (!strcmp(ver, v->opt_val)) {
			config.sys_ver = v->sys_ver;
			config.sco_ver = v->sco_ver;
			return;
		}
	}
	error("Unknown system version '%s'", ver);
}

void load_config(const char *path) {
	FILE *fp = fopen(path, "r");
	if (!fp)
		error("%s: %s", path, strerror(errno));
	const char *cfg_dir = dirname(path);
	char line[256];
	while (fgets(line, sizeof(line), fp)) {
		char val[256];
		if (sscanf(line, "sys_ver = %s", val)) {
			set_sys_ver(val);
		} else if (sscanf(line, "hed = %s", val)) {
			config.hed = path_join(cfg_dir, val);
		} else if (sscanf(line, "variables = %s", val)) {
			config.var_list = path_join(cfg_dir, val);
		} else if (sscanf(line, "disable_else = %s", val)) {
			config.disable_else = to_bool(val);
		} else if (sscanf(line, "disable_ain_message = %s", val)) {
			config.disable_ain_message = to_bool(val);
		} else if (sscanf(line, "old_SR = %s", val)) {
			config.old_SR = to_bool(val);
		}
	}
}
