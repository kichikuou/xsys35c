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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdnoreturn.h>
#include <time.h>

#define VERSION "0.1.0"

static inline uint32_t le32(const uint8_t *p) {
	return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

typedef enum {
	SCO_S350,
	SCO_S351,
	SCO_153S,
	SCO_S360,
	SCO_S380
} ScoVer;

// util.c

noreturn void error(char *fmt, ...);
FILE *checked_fopen(const char *path, const char *mode);
char *sjis2utf(const char *str);
char *utf2sjis(const char *str);
uint8_t to_sjis_half_kana(uint8_t c1, uint8_t c2);
uint16_t from_sjis_half_kana(uint8_t c);

static inline bool is_sjis_half_kana(uint8_t c) {
	return 0xa1 <= c && c <= 0xdf;
}

static inline bool is_sjis_byte1(uint8_t c) {
	return (0x81 <= c && c <= 0x9f) || (0xe0 <= c && c <= 0xef);
}

static inline bool is_sjis_byte2(uint8_t c) {
	return 0x40 <= c && c <= 0xfc && c != 0x7f;
}

#define PATH_SEPARATOR '/'
const char *basename(const char *path);
char *dirname(const char *path);
char *path_join(const char *dir, const char *path);

typedef struct {
	void **data;
	int len;
	int cap;
} Vector;

Vector *new_vec(void);
void vec_push(Vector *v, void *e);
void vec_set(Vector *v, int index, void *e);

typedef struct {
	Vector *keys;
	Vector *vals;
} Map;

Map *new_map(void);
void map_put(Map *m, const char *key, void *val);
void *map_get(Map *m, const char *key);

// ald.c

typedef struct {
	const char *name;
	time_t timestamp;
	const uint8_t *data;
	int size;
	int disk;
} AldEntry;

void ald_write(Vector *entries, int disk, FILE *fp);
Vector *ald_read(Vector *entries, const char *path);

// System39.ain

typedef enum {
	HEL_pword = 0,
	HEL_int = 1,
	HEL_ISurface = 2,
	HEL_IString = 3,
	HEL_IWinMsg = 4,
	HEL_ITimer = 5,
	HEL_IUI = 6,
	HEL_ISys3xDIB = 7,
	HEL_ISys3xCG = 9,
	HEL_ISys3xStringTable = 10,
	HEL_ISys3xSystem = 13,
	HEL_ISys3xMusic = 14,
	HEL_ISys3xMsgString = 15,
	HEL_ISys3xInputDevice = 16,
	HEL_ISys3x = 17,
	HEL_IConstString = 18,
} HELType;

typedef struct {
	const char *name;
	uint32_t argc;
	HELType argtypes[];
} DLLFunc;

// opcodes

enum {
	OP_AND = 0x74,
	OP_OR,
	OP_XOR,
	OP_MUL,
	OP_DIV,
	OP_ADD,
	OP_SUB,
	OP_EQ,
	OP_LT,
	OP_GT,
	OP_NE,
	OP_END, // End of expression
};
enum {
	OP_C0_INDEX = 1,
	OP_C0_MOD,
	OP_C0_LE,
	OP_C0_GE,
};

#define CMD2(a, b) (a | b << 8)
#define CMD3(a, b, c) (a | b << 8 | c << 16)
#define CMD2F(b) CMD2(0x2f, b)

enum {
	COMMAND_TOC = CMD2F(0x00),
	COMMAND_TOS = CMD2F(0x01),
	COMMAND_TPC = CMD2F(0x02),
	COMMAND_TPS = CMD2F(0x03),
	COMMAND_TOP = CMD2F(0x04),
	COMMAND_TPP = CMD2F(0x05),
	COMMAND_inc = CMD2F(0x06),
	COMMAND_dec = CMD2F(0x07),
	COMMAND_TAA = CMD2F(0x08),
	COMMAND_TAB = CMD2F(0x09),
	COMMAND_wavLoad = CMD2F(0x0a),
	COMMAND_wavPlay = CMD2F(0x0b),
	COMMAND_wavStop = CMD2F(0x0c),
	COMMAND_wavUnload = CMD2F(0x0d),
	COMMAND_wavIsPlay = CMD2F(0x0e),
	COMMAND_wavFade = CMD2F(0x0f),
	COMMAND_wavIsFade = CMD2F(0x10),
	COMMAND_wavStopFade = CMD2F(0x11),
	COMMAND_trace = CMD2F(0x12),
	COMMAND_wav3DSetPos = CMD2F(0x13),
	COMMAND_wav3DCommit = CMD2F(0x14),
	COMMAND_wav3DGetPos = CMD2F(0x15),
	COMMAND_wav3DSetPosL = CMD2F(0x16),
	COMMAND_wav3DGetPosL = CMD2F(0x17),
	COMMAND_wav3DFadePos = CMD2F(0x18),
	COMMAND_wav3DIsFadePos = CMD2F(0x19),
	COMMAND_wav3DStopFadePos = CMD2F(0x1a),
	COMMAND_wav3DFadePosL = CMD2F(0x1b),
	COMMAND_wav3DIsFadePosL = CMD2F(0x1c),
	COMMAND_wav3DStopFadePosL = CMD2F(0x1d),
	COMMAND_sndPlay = CMD2F(0x1e),
	COMMAND_sndStop = CMD2F(0x1f),
	COMMAND_sndIsPlay = CMD2F(0x20),
	COMMAND_msg = CMD2F(0x21),
	COMMAND_newHH = CMD2F(0x22),
	COMMAND_newLC = CMD2F(0x23),
	COMMAND_newLE = CMD2F(0x24),
	COMMAND_newLXG = CMD2F(0x25),
	COMMAND_newMI = CMD2F(0x26),
	COMMAND_newMS = CMD2F(0x27),
	COMMAND_newMT = CMD2F(0x28),
	COMMAND_newNT = CMD2F(0x29),
	COMMAND_newQE = CMD2F(0x2a),
	COMMAND_newUP = CMD2F(0x2b),
	COMMAND_newF = CMD2F(0x2c),
	COMMAND_wavWaitTime = CMD2F(0x2d),
	COMMAND_wavGetPlayPos = CMD2F(0x2e),
	COMMAND_wavWaitEnd = CMD2F(0x2f),
	COMMAND_wavGetWaveTime = CMD2F(0x30),
	COMMAND_menuSetCbkSelect = CMD2F(0x31),
	COMMAND_menuSetCbkCancel = CMD2F(0x32),
	COMMAND_menuClearCbkSelect = CMD2F(0x33),
	COMMAND_menuClearCbkCancel = CMD2F(0x34),
	COMMAND_wav3DSetMode = CMD2F(0x35),
	COMMAND_grCopyStretch = CMD2F(0x36),
	COMMAND_grFilterRect = CMD2F(0x37),
	COMMAND_iptClearWheelCount = CMD2F(0x38),
	COMMAND_iptGetWheelCount = CMD2F(0x39),
	COMMAND_menuGetFontSize = CMD2F(0x3a),
	COMMAND_msgGetFontSize = CMD2F(0x3b),
	COMMAND_strGetCharType = CMD2F(0x3c),
	COMMAND_strGetLengthASCII = CMD2F(0x3d),
	COMMAND_sysWinMsgLock = CMD2F(0x3e),
	COMMAND_sysWinMsgUnlock = CMD2F(0x3f),
	COMMAND_aryCmpCount = CMD2F(0x40),
	COMMAND_aryCmpTrans = CMD2F(0x41),
	COMMAND_grBlendColorRect = CMD2F(0x42),
	COMMAND_grDrawFillCircle = CMD2F(0x43),
	COMMAND_MHH = CMD2F(0x44),
	COMMAND_menuSetCbkInit = CMD2F(0x45),
	COMMAND_menuClearCbkInit = CMD2F(0x46),
	COMMAND_menu = CMD2F(0x47),
	COMMAND_sysOpenShell = CMD2F(0x48),
	COMMAND_sysAddWebMenu = CMD2F(0x49),
	COMMAND_iptSetMoveCursorTime = CMD2F(0x4a),
	COMMAND_iptGetMoveCursorTime = CMD2F(0x4b),
	COMMAND_grBlt = CMD2F(0x4c),
	COMMAND_LXWT = CMD2F(0x4d),
	COMMAND_LXWS = CMD2F(0x4e),
	COMMAND_LXWE = CMD2F(0x4f),
	COMMAND_LXWH = CMD2F(0x50),
	COMMAND_LXWHH = CMD2F(0x51),
	COMMAND_sysGetOSName = CMD2F(0x52),
	COMMAND_patchEC = CMD2F(0x53),
	COMMAND_mathSetClipWindow = CMD2F(0x54),
	COMMAND_mathClip = CMD2F(0x55),
	COMMAND_LXF = CMD2F(0x56),
	COMMAND_strInputDlg = CMD2F(0x57),
	COMMAND_strCheckASCII = CMD2F(0x58),
	COMMAND_strCheckSJIS = CMD2F(0x59),
	COMMAND_strMessageBox = CMD2F(0x5a),
	COMMAND_strMessageBoxStr = CMD2F(0x5b),
	COMMAND_grCopyUseAMapUseA = CMD2F(0x5c),
	COMMAND_grSetCEParam = CMD2F(0x5d),
	COMMAND_grEffectMoveView = CMD2F(0x5e),
	COMMAND_cgSetCacheSize = CMD2F(0x5f),
	COMMAND_dllCall = CMD2F(0x60),
	COMMAND_gaijiSet = CMD2F(0x61),
	COMMAND_gaijiClearAll = CMD2F(0x62),
	COMMAND_menuGetLatestSelect = CMD2F(0x63),
	COMMAND_lnkIsLink = CMD2F(0x64),
	COMMAND_lnkIsData = CMD2F(0x65),
	COMMAND_fncSetTable = CMD2F(0x66),
	COMMAND_fncSetTableFromStr = CMD2F(0x67),
	COMMAND_fncClearTable = CMD2F(0x68),
	COMMAND_fncCall = CMD2F(0x69),
	COMMAND_fncSetReturnCode = CMD2F(0x6a),
	COMMAND_fncGetReturnCode = CMD2F(0x6b),
	COMMAND_msgSetOutputFlag = CMD2F(0x6c),
	COMMAND_saveDeleteFile = CMD2F(0x6d),
	COMMAND_wav3DSetUseFlag = CMD2F(0x6e),
	COMMAND_wavFadeVolume = CMD2F(0x6f),
	COMMAND_patchEMEN = CMD2F(0x70),
	COMMAND_wmenuEnableMsgSkip = CMD2F(0x71),
	COMMAND_winGetFlipFlag = CMD2F(0x72),
	COMMAND_cdGetMaxTrack = CMD2F(0x73),
	COMMAND_dlgErrorOkCancel = CMD2F(0x74),
	COMMAND_menuReduce = CMD2F(0x75),
	COMMAND_menuGetNumof = CMD2F(0x76),
	COMMAND_menuGetText = CMD2F(0x77),
	COMMAND_menuGoto = CMD2F(0x78),
	COMMAND_menuReturnGoto = CMD2F(0x79),
	COMMAND_menuFreeShelterDIB = CMD2F(0x7a),
	COMMAND_msgFreeShelterDIB = CMD2F(0x7b),
	COMMAND_ainMsg = CMD2F(0x7c),
	COMMAND_ainH = CMD2F(0x7d),
	COMMAND_ainHH = CMD2F(0x7e),
	COMMAND_ainX = CMD2F(0x7f),
	COMMAND_dataSetPointer = CMD2F(0x80),
	COMMAND_dataGetWORD = CMD2F(0x81),
	COMMAND_dataGetString = CMD2F(0x82),
	COMMAND_dataSkipWORD = CMD2F(0x83),
	COMMAND_dataSkipString = CMD2F(0x84),
	COMMAND_varGetNumof = CMD2F(0x85),
	COMMAND_patchG0 = CMD2F(0x86),
	COMMAND_regReadString = CMD2F(0x87),
	COMMAND_fileCheckExist = CMD2F(0x88),
	COMMAND_timeCheckCurDate = CMD2F(0x89),
	COMMAND_dlgManualProtect = CMD2F(0x8a),
	COMMAND_fileCheckDVD = CMD2F(0x8b),
	COMMAND_sysReset = CMD2F(0x8c),
	// Pseudo commands
	COMMAND_IF = 0x80,
	COMMAND_LXWx = 0x81,
};
