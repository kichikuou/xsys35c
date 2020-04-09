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
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

const char *input_name;
int input_page;
const char *input_buf;
const char *input;

noreturn void error_at(const char *pos, char *fmt, ...) {
	int line = 1;
	for (const char *begin = input_buf;; line++) {
		const char *end = strchr(begin, '\n');
		if (!end)  // last line
			end = strchr(begin, '\0');
		if (pos <= end) {
			int col = pos - begin;
			fprintf(stderr, "%s line %d column %d: ", sjis2utf(input_name), line, col);
			va_list args;
			va_start(args, fmt);
			vfprintf(stderr, fmt, args);
			fputc('\n', stderr);
			fprintf(stderr, "%s\n", sjis2utf(strndup(begin, end - begin)));
			for (const char *p = begin; p < pos; p++)
				fputc(*p == '\t' ? '\t' : ' ', stderr);
			fprintf(stderr, "^\n");
			break;
		}
		if (!*end)
			error("BUG: cannot find error location");
		begin = end + 1;
	}
	exit(1);
}

void lexer_init(const char *source, const char *name, int pageno) {
	input_buf = input = source;
	input_name = name;
	input_page = pageno;
}

void skip_whitespaces(void) {
	while (*input) {
		if (isspace(*input)) {
			input++;
		} else if (*input == ';' || (*input == '/' && *(input+1) == '/')) {
			input = strchr(input, '\n');
		} else if (*input == '/' && *(input+1) == '*') {
			const char* top = input;
			do {
				input = strchr(input + 2, '*');
				if (!input)
					error_at(top, "unfinished comment");
			} while (*++input != '/');
			input++;
		} else if (input[0] == (char)0x81 && input[1] == (char)0x40) {  // SJIS full-width space
			input += 2;
		} else {
			break;
		}
	}
	return;
}

char next_char(void) {
	skip_whitespaces();
	return *input;
}

bool consume(char c) {
	if (next_char() != c)
		return false;
	input++;
	return true;
}

void expect(char c) {
	if (next_char() != c)
		error_at(input, "'%c' expected", c);
	input++;
}

bool consume_keyword(const char *keyword) {
	skip_whitespaces();
	if (*input != *keyword)
		return false;
	int len = strlen(keyword);
	if (!strncmp(input, keyword, len) && !isalnum(input[len]) && input[len] != '_') {
		input += len;
		return true;
	}
	return false;
}

uint8_t echo(Buffer *b) {
	uint8_t c = *input++;
	emit(b, c);
	return c;
}

static bool is_identifier(uint8_t c) {
	return isalnum(c) || is_sjis_byte1(c) || is_sjis_half_kana(c)
		|| c == '_' || c == '.';
}

char *get_identifier(void) {
	skip_whitespaces();
	const char *top = input;
	if (!is_identifier(*top) || isdigit(*top))
		error_at(top, "identifier expected");
	while (is_identifier(*input)) {
		if (is_sjis_byte1(input[0]) && is_sjis_byte2(input[1]))
			input++;
		input++;
	}
	return strndup(top, input - top);
}

char *get_label(void) {
	skip_whitespaces();
	const char *top = input;
	while (isalnum(*input) || *input == '_' || *input == '-' ||
		   is_sjis_byte1(*input) || is_sjis_half_kana(*input)) {
		if (is_sjis_byte1(input[0]) && is_sjis_byte2(input[1]))
			input++;
		input++;
	}
	if (input == top)
		error_at(top, "label expected");
	return strndup(top, input - top);
}

char *get_filename(void) {
	const char *top = input;
	while (isalnum(*input) || *input == '.' || *input == '_' || is_sjis_byte1(*input) || is_sjis_half_kana(*input)) {
		if (is_sjis_byte1(input[0]) && is_sjis_byte2(input[1]))
			input++;
		input++;
	}
	if (input == top)
		error_at(top, "file name expected");

	return strndup(top, input - top);
}

// number ::= [0-9]+ | '0' [xX] [0-9a-fA-F]+ | '0' [bB] [01]+
int get_number(void) {
	if (!isdigit(next_char()))
		error_at(input, "number expected");
	int base = 10;
	if (input[0] == '0' && tolower(input[1]) == 'x') {
		base = 16;
		input += 2;
	} else if (input[0] == '0' && tolower(input[1]) == 'b') {
		base = 2;
		input += 2;
	}
	char *p;
	long n = strtol(input, &p, base);
	input = p;
	return n;
}

void compile_string(Buffer *b, char terminator) {
	const char *top = input;
	while (*input != terminator) {
		if (!*input)
			error_at(top, "unfinished message");
		uint8_t c1 = *input++;
		uint8_t c2 = *input++;
		if (!is_sjis_byte1(c1) || !is_sjis_byte2(c2))
			error_at(input - 2, "invalid SJIS character: %02x %02x", c1, c2);
		uint8_t hk = 0;
		if (sys_ver == SYSTEM35)
			hk = to_sjis_half_kana(c1, c2);
		if (hk) {
			emit(b, hk);
		} else {
			emit(b, c1);
			emit(b, c2);
		}
	}
	expect(terminator);
}

void compile_message(Buffer *b) {
	// TODO: Support data embedding ("<0x...>")
	while (*input && *input != '\'') {
		if (*input == '\\')
			input++;
		if (is_sjis_byte1(*input) && is_sjis_byte2(*(input+1)))
			emit(b, *input++);
		emit(b, *input++);
	}
	expect('\'');
	emit(b, 0);
}

#define ISKEYWORD(s, len, kwd) ((len) == sizeof(kwd) - 1 && !memcmp((s), (kwd), (len)))

static int lower_case_command(const char *s, int len) {
#define LCCMD(cmd) if (ISKEYWORD(s, len, #cmd)) return COMMAND_ ## cmd
	LCCMD(inc);
	LCCMD(dec);
	LCCMD(wavLoad);
	LCCMD(wavPlay);
	LCCMD(wavStop);
	LCCMD(wavUnload);
	LCCMD(wavIsPlay);
	LCCMD(wavFade);
	LCCMD(wavIsFade);
	LCCMD(wavStopFade);
	LCCMD(trace);
	LCCMD(wav3DSetPos);
	LCCMD(wav3DCommit);
	LCCMD(wav3DGetPos);
	LCCMD(wav3DSetPosL);
	LCCMD(wav3DGetPosL);
	LCCMD(wav3DFadePos);
	LCCMD(wav3DIsFadePos);
	LCCMD(wav3DStopFadePos);
	LCCMD(wav3DFadePosL);
	LCCMD(wav3DIsFadePosL);
	LCCMD(wav3DStopFadePosL);
	LCCMD(sndPlay);
	LCCMD(sndStop);
	LCCMD(sndIsPlay);
	LCCMD(msg);
	LCCMD(wavWaitTime);
	LCCMD(wavGetPlayPos);
	LCCMD(wavWaitEnd);
	LCCMD(wavGetWaveTime);
	LCCMD(menuSetCbkSelect);
	LCCMD(menuSetCbkCancel);
	LCCMD(menuClearCbkSelect);
	LCCMD(menuClearCbkCancel);
	LCCMD(wav3DSetMode);
	LCCMD(grCopyStretch);
	LCCMD(grFilterRect);
	LCCMD(iptClearWheelCount);
	LCCMD(iptGetWheelCount);
	LCCMD(menuGetFontSize);
	LCCMD(msgGetFontSize);
	LCCMD(strGetCharType);
	LCCMD(strGetLengthASCII);
	LCCMD(sysWinMsgLock);
	LCCMD(sysWinMsgUnlock);
	LCCMD(aryCmpCount);
	LCCMD(aryCmpTrans);
	LCCMD(grBlendColorRect);
	LCCMD(grDrawFillCircle);
	LCCMD(menuSetCbkInit);
	LCCMD(menuClearCbkInit);
	LCCMD(menu);
	LCCMD(sysOpenShell);
	LCCMD(sysAddWebMenu);
	LCCMD(iptSetMoveCursorTime);
	LCCMD(iptGetMoveCursorTime);
	LCCMD(grBlt);
	LCCMD(sysGetOSName);
	LCCMD(patchEC);
	LCCMD(mathSetClipWindow);
	LCCMD(mathClip);
	LCCMD(strInputDlg);
	LCCMD(strCheckASCII);
	LCCMD(strCheckSJIS);
	LCCMD(strMessageBox);
	LCCMD(strMessageBoxStr);
	LCCMD(grCopyUseAMapUseA);
	LCCMD(grSetCEParam);
	LCCMD(grEffectMoveView);
	LCCMD(cgSetCacheSize);
	LCCMD(gaijiSet);
	LCCMD(gaijiClearAll);
	LCCMD(menuGetLatestSelect);
	LCCMD(lnkIsLink);
	LCCMD(lnkIsData);
	LCCMD(fncSetTable);
	LCCMD(fncSetTableFromStr);
	LCCMD(fncClearTable);
	LCCMD(fncCall);
	LCCMD(fncSetReturnCode);
	LCCMD(fncGetReturnCode);
	LCCMD(msgSetOutputFlag);
	LCCMD(saveDeleteFile);
	LCCMD(wav3DSetUseFlag);
	LCCMD(wavFadeVolume);
	LCCMD(patchEMEN);
	LCCMD(wmenuEnableMsgSkip);
	LCCMD(winGetFlipFlag);
	LCCMD(cdGetMaxTrack);
	LCCMD(dlgErrorOkCancel);
	LCCMD(menuReduce);
	LCCMD(menuGetNumof);
	LCCMD(menuGetText);
	LCCMD(menuGoto);
	LCCMD(menuReturnGoto);
	LCCMD(menuFreeShelterDIB);
	LCCMD(msgFreeShelterDIB);
	return 0;
#undef LCCMD
}

static int replace_command(int cmd) {
	switch (cmd) {
	case CMD3('T', 'A', 'A'): return COMMAND_TAA;
	case CMD3('T', 'A', 'B'): return COMMAND_TAB;
	}

	if (sys_ver < SYSTEM38)
		return cmd;

	switch (cmd) {
	case CMD3('T', 'O', 'C'): return COMMAND_TOC;
	case CMD3('T', 'O', 'S'): return COMMAND_TOS;
	case CMD3('T', 'P', 'C'): return COMMAND_TPC;
	case CMD3('T', 'P', 'S'): return COMMAND_TPS;
	case CMD3('T', 'O', 'P'): return COMMAND_TOP;
	case CMD3('T', 'P', 'P'): return COMMAND_TPP;
	case CMD2('H', 'H'): return sys_ver == SYSTEM39 ? COMMAND_ainHH : COMMAND_newHH;
	case CMD2('L', 'C'): return COMMAND_newLC;
	case CMD2('L', 'E'): return COMMAND_newLE;
	case CMD3('L', 'X', 'G'): return COMMAND_newLXG;
	case CMD2('M', 'I'): return COMMAND_newMI;
	case CMD2('M', 'S'): return COMMAND_newMS;
	case CMD2('M', 'T'): return COMMAND_newMT;
	case CMD2('N', 'T'): return COMMAND_newNT;
	case CMD2('Q', 'E'): return COMMAND_newQE;
	case CMD2('U', 'P'): return COMMAND_newUP;
	case 'F': return COMMAND_newF;
	case 'H': return sys_ver == SYSTEM39 ? COMMAND_ainH : cmd;
	case CMD3('M', 'H', 'H'): return COMMAND_MHH;
	case CMD2(COMMAND_LXW, 'T'): return COMMAND_LXWT;
	case CMD2(COMMAND_LXW, 'S'): return COMMAND_LXWS;
	case CMD2(COMMAND_LXW, 'E'): return COMMAND_LXWE;
	case CMD2(COMMAND_LXW, 'H'): return COMMAND_LXWH;
	case CMD3(COMMAND_LXW, 'H', 'H'): return COMMAND_LXWHH;
	case CMD3('L', 'X', 'F'): return COMMAND_LXF;
	case 'X': return sys_ver == SYSTEM39 ? COMMAND_ainX : cmd;
	default: return cmd;
	}
}

int get_command(Buffer *b) {
	const char *command_top = input;

	if (!*input || *input == '}' || *input == '>')
		return *input;
	if (*input == 'A' || *input == 'R')
		return echo(b);
	if (isupper(*input)) {
		int cmd = *input++;
		if (isupper(*input))
			cmd |= *input++ << 8;
		if (isupper(*input))
			cmd |= *input++ << 16;

		if (cmd == CMD3('L', 'X', 'W')) {
			cmd = COMMAND_LXW;
			if (isupper(*input))
				cmd |= *input++ << 8;
			if (isupper(*input))
				cmd |= *input++ << 16;
		}

		if (isupper(*input))
			error_at(command_top, "Unknown command %.4s", command_top);

		if (cmd == 'N' && strchr("+-*/><=\\&|^~", *input))
			cmd |= *input++ << 8;
		if (cmd == CMD2('N', 'D') && strchr("+-*/", *input))
			cmd |= *input++ << 16;

		cmd = replace_command(cmd);
		emit_command(b, cmd);
		return cmd;
	}
	if (islower(*input)) {
		while (isalnum(*++input))
			;
		int len = input - command_top;
		if (ISKEYWORD(command_top, len, "if"))
			return COMMAND_IF;
		int cmd = lower_case_command(command_top, len);
		if (cmd) {
			emit_command(b, cmd);
			return cmd;
		}
		error_at(command_top, "Unknown command %.*s", len, command_top);
	}
	return *input++;
}
