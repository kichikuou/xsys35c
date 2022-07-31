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
#include <assert.h>
#include <stdio.h>
#include <string.h>

Config config = {
	.ain_version = 1,
	.sys_ver = SYSTEM39,
	.sco_ver = SCO_S380,
	.utf8 = true,
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

static const char *get_sys_ver(void) {
	for (const SysVerOptValue *v = sys_ver_opt_values; v->opt_val; v++) {
		if (config.sys_ver == v->sys_ver && config.sco_ver == v->sco_ver)
			return v->opt_val;
	}
	assert("cannot happen");
	return NULL;
}

void load_config(FILE *fp, const char *cfg_dir) {
	char line[256];
	while (fgets(line, sizeof(line), fp)) {
		char val[256];
		int intval;
		if (sscanf(line, "sys_ver = %s", val)) {
			set_sys_ver(val);
		} else if (sscanf(line, "encoding = %s", val)) {
			if (!strcasecmp(val, "sjis"))
				config.utf8 = false;
			else if (!strcasecmp(val, "utf8"))
				config.utf8 = true;
			else
				error("Unknown encoding %s", val);
		} else if (sscanf(line, "hed = %s", val)) {
			config.hed = path_join(cfg_dir, val);
		} else if (sscanf(line, "variables = %s", val)) {
			config.var_list = path_join(cfg_dir, val);
		} else if (sscanf(line, "disable_else = %s", val)) {
			config.disable_else = to_bool(val);
		} else if (sscanf(line, "disable_ain_message = %s", val)) {
			config.disable_ain_message = to_bool(val);
		} else if (sscanf(line, "disable_ain_variable = %s", val)) {
			config.disable_ain_variable = to_bool(val);
		} else if (sscanf(line, "old_SR = %s", val)) {
			config.old_SR = to_bool(val);
		} else if (sscanf(line, "ald_basename = %s", val)) {
			config.ald_basename = path_join(cfg_dir, val);
		} else if (sscanf(line, "output_ain = %s", val)) {
			config.output_ain = path_join(cfg_dir, val);
		} else if (sscanf(line, "ain_version = %d", &intval)) {
			config.ain_version = intval;
		} else if (sscanf(line, "unicode = %s", val)) {
			config.unicode = to_bool(val);
		} else if (sscanf(line, "debug = %s", val)) {
			config.debug = to_bool(val);
		}
	}
}

int init_project(const char *project, const char *hed, const char *ald_basename) {
	if (!project)
		project = "xsys35c.cfg";
	if (!hed)
		hed = "sources.hed";

	FILE *fp = checked_fopen(project, "w");
	fprintf(fp, "sys_ver = %s\n", get_sys_ver());
	if (!config.utf8)
		fputs("encoding = sjis\n", fp);
	fprintf(fp, "hed = %s\n", hed);
	if (ald_basename)
		fprintf(fp, "ald_basename = %s\n", ald_basename);
	if (config.unicode)
		fputs("unicode = true\n", fp);
	if (config.debug)
		fputs("debug = true\n", fp);
	fclose(fp);

	fp = checked_fopen(hed, "w");
	fputs("#SYSTEM35\n", fp);
	fputs("initial.adv\n", fp);
	fclose(fp);

	fp = checked_fopen("initial.adv", "w");
	fputs("\t!RND:0!\n", fp);
	fputs("\t!D01:0!!D02:0!!D03:0!!D04:0!!D05:0!!D06:0!!D07:0!!D08:0!!D09:0!!D10:0!\n", fp);
	fputs("\t!D11:0!!D12:0!!D13:0!!D14:0!!D15:0!!D16:0!!D17:0!!D18:0!!D19:0!!D20:0!\n", fp);
	fputs("\tWW 640, 1440, 24:\n", fp);
	fputs("\tWV 0, 0, 640, 480:\n", fp);
	fputs("\tB1 1, 450, 20, 172, 240, 1:\n", fp);
	fputs("\tB2 1, 1, 0, 0, 0, 0:\n", fp);
	fputs("\tB3 1, 200, 380, 430, 90, 0:\n", fp);
	fputs("\tB4 1, 1, 0, 0, 1, 0:\n", fp);
	fclose(fp);

	return 0;
}
