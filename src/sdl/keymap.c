/*
  Hatari - keymap.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Map SDL key events to ST scancodes and send them to IKBD as
  pressed/released keys.  Based on Hatari configuration options,
  several different ways can be used to map SDL key events.

  Supports upstream symbolic/scancode mapping and country layouts, simple
  comma-separated host,guest keymap lines, and extended host specifications
  (PC scancode | modifiers | mask -> chained ST scan codes).

  Simple lines: host,guest
    * Host: hexadecimal SDL PC scancode ([0-9a-fA-F]+ or 0x…); otherwise
      SDL_GetKeyFromName (e.g. F1 — lowercase f1 is hex digits only).
    * Guest: one hexadecimal ST scancode, OR a pipe-separated chain (same as
      extended guest) if host is a hex PC scancode only — e.g. 25,LSHIFT|0d

  Extended guest segments (pipe-separated): each hex ST scancode, or LSHIFT,
  RSHIFT, ALTERNATE for convenience.

  See https://tho-otto.de/keyboards/ for the Atari ST keyboard layouts.
*/
const char Keymap_fileid[] = "Hatari keymap.c";

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "keymap.h"
#include "keymap_sdl.h"
#include "configuration.h"
#include "file.h"
#include "ikbd.h"
#include "nvram.h"
#include "joy.h"
#include "shortcut.h"
#include "str.h"
#include "tos.h"
#include "debugui.h"
#include "log.h"

#if ENABLE_SDL3
#include <SDL3/SDL_oldnames.h>
#endif

/* if not able to map */
#define ST_NO_SCANCODE 0xff

#define KEYS_DOWN_MAPPING_SIZE 512

#define MAX_ST_SCANCODES 4

struct KeyMapping
{
	SDL_Keysym SdlKeysym;
	SDL_Keymod modmask;
	uint8_t STScanCodesLength;
	uint8_t STScanCodes[MAX_ST_SCANCODES];
	uint8_t PressedModifiers[MAX_ST_SCANCODES];
};

static struct KeyMapping LoadedKeyMap[KBD_MAX_SCANCODE];
static int extended_mapping_count;

static struct KeyMapping KeysDownMapping[KEYS_DOWN_MAPPING_SIZE];

/* Table for loaded keys: */
static int LoadedKeymap[KBD_MAX_SCANCODE][2];
/** Simple-map host matches SDL_Scancode if true, else SDL_Keycode */
static bool LoadedKeymapHostIsScancode[KBD_MAX_SCANCODE];
static bool simple_keymap_loaded;


/**
 * Initialization.
 */
void Keymap_Init(void)
{
	Keymap_LoadRemapFile(ConfigureParams.Keyboard.szMappingFileName);
}


/**
 * Set defaults for shortcut keys
 */
void Keymap_InitShortcutDefaultKeys(void)
{
	ConfigureParams.Shortcut.withoutModifier[SHORTCUT_OPTIONS] = SDLK_F12;
	ConfigureParams.Shortcut.withoutModifier[SHORTCUT_FULLSCREEN] = SDLK_F11;
	ConfigureParams.Shortcut.withoutModifier[SHORTCUT_PAUSE] = SDLK_PAUSE;

	ConfigureParams.Shortcut.withModifier[SHORTCUT_DEBUG] = SDLK_PAUSE;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_OPTIONS] = SDLK_o;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_FULLSCREEN] = SDLK_f;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_BORDERS] = SDLK_b;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_MOUSEGRAB] = SDLK_m;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_COLDRESET] = SDLK_c;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_WARMRESET] = SDLK_r;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_SCREENSHOT] = SDLK_g;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_BOSSKEY] = SDLK_i;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_CURSOREMU] = SDLK_j;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_FASTFORWARD] = SDLK_x;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_RECANIM] = SDLK_a;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_RECSOUND] = SDLK_y;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_SOUND] = SDLK_s;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_QUIT] = SDLK_q;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_LOADMEM] = SDLK_l;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_SAVEMEM] = SDLK_k;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_INSERTDISKA] = SDLK_d;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_JOY_0] = SDLK_F1;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_JOY_1] = SDLK_F2;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_PAD_A] = SDLK_F3;
	ConfigureParams.Shortcut.withModifier[SHORTCUT_PAD_B] = SDLK_F4;
}


/**
 * Default function for mapping SDL symbolic key to ST scan code.
 * This contains the ST keycode used by the majority of TOS regions for
 * that semantic symbol.
 */
static uint8_t Keymap_SymbolicToStScanCode_default(SDL_Keycode symkey)
{
	uint8_t code;

	switch (symkey)
	{
	 case SDLK_BACKSPACE: code = 0x0E; break;
	 case SDLK_TAB: code = 0x0F; break;
	 case SDLK_CLEAR: code = 0x47; break;
	 case SDLK_RETURN: code = 0x1C; break;
	 case SDLK_ESCAPE: code = ST_ESC; break;
	 case SDLK_SPACE: code = 0x39; break;
	 case SDLK_EXCLAIM: code = 0x09; break;     /* on azerty? */
	 case SDLK_QUOTEDBL: code = 0x04; break;    /* on azerty? */
	 case SDLK_HASH: code = 0x2B; break;        /* DE, UK host only, for FR/UK/DK/NL TOS */
	 case SDLK_DOLLAR: code = 0x1b; break;      /* on azerty */
	 case SDLK_AMPERSAND: code = 0x02; break;   /* on azerty? */
	 case SDLK_QUOTE: code = 0x28; break;
	 case SDLK_LEFTPAREN: code = 0x63; break;
	 case SDLK_RIGHTPAREN: code = 0x64; break;
	 case SDLK_ASTERISK: code = 0x66; break;
	 case SDLK_PLUS: code = 0x4e; break;
	 case SDLK_COMMA: code = 0x33; break;
	 case SDLK_MINUS: code = 0x35; break;       /* default for DE/IT/SE/CH/FI/NO/DK/CZ */
	 case SDLK_PERIOD: code = 0x34; break;
	 case SDLK_SLASH: code = 0x35; break;
	 case SDLK_0: code = 0x0B; break;
	 case SDLK_1: code = 0x02; break;
	 case SDLK_2: code = 0x03; break;
	 case SDLK_3: code = 0x04; break;
	 case SDLK_4: code = 0x05; break;
	 case SDLK_5: code = 0x06; break;
	 case SDLK_6: code = 0x07; break;
	 case SDLK_7: code = 0x08; break;
	 case SDLK_8: code = 0x09; break;
	 case SDLK_9: code = 0x0A; break;
	 case SDLK_COLON: code = 0x34; break;
	 case SDLK_SEMICOLON: code = 0x27; break;
	 case SDLK_LESS: code = 0x60; break;
	 case SDLK_EQUALS: code = 0x0D; break;
	 case SDLK_GREATER : code = 0x34; break;
	 case SDLK_QUESTION: code = 0x35; break;
	 case SDLK_AT: code = 0x28; break;
	 case SDLK_LEFTBRACKET: code = 0x1A; break;
	 case SDLK_BACKSLASH: code = 0x2B; break;
	 case SDLK_RIGHTBRACKET: code = 0x1B; break;
	 case SDLK_CARET: code = 0x2B; break;
	 case SDLK_UNDERSCORE: code = 0x0C; break;
	 case SDLK_BACKQUOTE: code = 0x29; break;
	 case SDLK_a: code = 0x1E; break;
	 case SDLK_b: code = 0x30; break;
	 case SDLK_c: code = 0x2E; break;
	 case SDLK_d: code = 0x20; break;
	 case SDLK_e: code = 0x12; break;
	 case SDLK_f: code = 0x21; break;
	 case SDLK_g: code = 0x22; break;
	 case SDLK_h: code = 0x23; break;
	 case SDLK_i: code = 0x17; break;
	 case SDLK_j: code = 0x24; break;
	 case SDLK_k: code = 0x25; break;
	 case SDLK_l: code = 0x26; break;
	 case SDLK_m: code = 0x32; break;
	 case SDLK_n: code = 0x31; break;
	 case SDLK_o: code = 0x18; break;
	 case SDLK_p: code = 0x19; break;
	 case SDLK_q: code = 0x10; break;
	 case SDLK_r: code = 0x13; break;
	 case SDLK_s: code = 0x1F; break;
	 case SDLK_t: code = 0x14; break;
	 case SDLK_u: code = 0x16; break;
	 case SDLK_v: code = 0x2F; break;
	 case SDLK_w: code = 0x11; break;
	 case SDLK_x: code = 0x2D; break;
	 case SDLK_y: code = 0x15; break;
	 case SDLK_z: code = 0x2C; break;
	 case SDLK_DELETE: code = 0x53; break;
	 /* End of ASCII mapped keysyms */
	 case 167: code = 0x29; break;		/* Swiss § */
	 case 168: code = 0x1B; break;		/* Swiss ¨ */
	 case 176: code = 0x35; break;		/* Spanish ° */
	 case 178: code = 0x29; break;		/* French ² */
	 case 180: code = 0x0D; break;		/* German ' */
	 case 223: code = 0x0C; break;		/* German ß */
	 case 224: code = 0x0B; break;		/* French à */
	 case 225: code = 0x09; break;		/* Czech á */
	 case 228: code = 0x28; break;		/* German ä */
	 case 229: code = 0x1A; break;		/* Swedish å */
	 case 231: code = 0x0A; break;		/* French ç */
	 case 232: code = 0x08; break;		/* French è */
	 case 233: code = 0x03; break;		/* French é */
	 case 236: code = 0x0D; break;		/* Italian ì */
	 case 237: code = 0x0A; break;		/* Czech í */
	 case 241: code = 0x27; break;		/* Spanish ñ */
	 case 242: code = 0x27; break;		/* Italian ò */
	 case 243: code = 0x02; break;		/* Czech ó */
	 case 246: code = 0x27; break;		/* German ö */
	 case 249: code = 0x28; break;		/* French ù */
	 case 250: code = 0x1A; break;		/* Czech ú */
	 case 252: code = 0x1A; break;		/* German ü */
	 case 253: code = 0x08; break;		/* Czech ý */
	 case 269: code = 0x05; break;		/* Czech č */
	 case 271: code = 0x1B; break;		/* Czech ď */
	 case 283: code = 0x03; break;		/* Czech ě */
	 case 328: code = 0x2B; break;		/* Czech ň */
	 case 345: code = 0x06; break;		/* Czech ř */
	 case 353: code = 0x04; break;		/* Czech š */
	 case 357: code = 0x28; break;		/* Czech ť */
	 case 367: code = 0x27; break;		/* Czech ů */
	 case 382: code = 0x07; break;		/* Czech ž */
	 /* Numeric keypad: */
	 case SDLK_KP_0: code = 0x70; break;
	 case SDLK_KP_1: code = 0x6D; break;
	 case SDLK_KP_2: code = 0x6E; break;
	 case SDLK_KP_3: code = 0x6F; break;
	 case SDLK_KP_4: code = 0x6A; break;
	 case SDLK_KP_5: code = 0x6B; break;
	 case SDLK_KP_6: code = 0x6C; break;
	 case SDLK_KP_7: code = 0x67; break;
	 case SDLK_KP_8: code = 0x68; break;
	 case SDLK_KP_9: code = 0x69; break;
	 case SDLK_KP_PERIOD: code = 0x71; break;
	 case SDLK_KP_LEFTPAREN: code = 0x63; break;
	 case SDLK_KP_RIGHTPAREN: code = 0x64; break;
	 case SDLK_KP_DIVIDE: code = 0x65; break;
	 case SDLK_KP_MULTIPLY: code = 0x66; break;
	 case SDLK_KP_MINUS: code = 0x4A; break;
	 case SDLK_KP_PLUS: code = 0x4E; break;
	 case SDLK_KP_ENTER: code = 0x72; break;
	 case SDLK_KP_EQUALS: code = 0x61; break;
	 /* Arrows + Home/End pad */
	 case SDLK_UP: code = 0x48; break;
	 case SDLK_DOWN: code = 0x50; break;
	 case SDLK_RIGHT: code = 0x4D; break;
	 case SDLK_LEFT: code = 0x4B; break;
	 case SDLK_INSERT: code = 0x52; break;
	 case SDLK_HOME: code = 0x47; break;
	 case SDLK_END: code = 0x61; break;         /* ST Undo */
	 case SDLK_PAGEUP: code = 0x63; break;      /* ST ( */
	 case SDLK_PAGEDOWN: code = 0x64; break;    /* ST ) */
	 /* Function keys */
	 case SDLK_F1: code = 0x3B; break;
	 case SDLK_F2: code = 0x3C; break;
	 case SDLK_F3: code = 0x3D; break;
	 case SDLK_F4: code = 0x3E; break;
	 case SDLK_F5: code = 0x3F; break;
	 case SDLK_F6: code = 0x40; break;
	 case SDLK_F7: code = 0x41; break;
	 case SDLK_F8: code = 0x42; break;
	 case SDLK_F9: code = 0x43; break;
	 case SDLK_F10: code = 0x44; break;
	 case SDLK_F11: code = 0x62; break;         /* ST Help */
	 case SDLK_F12: code = 0x61; break;         /* ST Undo */
	 case SDLK_F13: code = 0x62; break;         /* ST Help */
	 /* Key state modifier keys */
	 case SDLK_CAPSLOCK: code = ST_CAPSLOCK; break;
	 case SDLK_SCROLLLOCK: code = 0x61; break;  /* ST Undo */
	 case SDLK_RSHIFT: code = ST_RSHIFT; break;
	 case SDLK_LSHIFT: code = ST_LSHIFT; break;
	 case SDLK_RCTRL: code = ST_CONTROL; break;
	 case SDLK_LCTRL: code = ST_CONTROL; break;
	 case SDLK_RALT: code = ST_ALTERNATE; break;
	 case SDLK_LALT: code = ST_ALTERNATE; break;
	 /* Miscellaneous function keys */
	 case SDLK_HELP: code = 0x62; break;
	 case SDLK_PRINTSCREEN: code = 0x62; break; /* ST Help */
	 case SDLK_UNDO: code = 0x61; break;
	 default: code = ST_NO_SCANCODE;
	}

	return code;
}

static uint8_t (*Keymap_SymbolicToStScanCode)(SDL_Keycode symkey) =
		Keymap_SymbolicToStScanCode_default;

static uint8_t Keymap_SymbolicToStScanCode_US(SDL_Keycode symkey)
{
	switch (symkey)
	{
	 case SDLK_MINUS: return 0x0C;
	 default: return Keymap_SymbolicToStScanCode_default(symkey);
	}
}

static uint8_t Keymap_SymbolicToStScanCode_DE(SDL_Keycode symkey)
{
	switch (symkey)
	{
	 case SDLK_HASH: return 0x29;
	 case SDLK_PLUS: return 0x1B;
	 case SDLK_SLASH: return 0x65;
	 case SDLK_y: return 0x2C;
	 case SDLK_z: return 0x15;
	 default: return Keymap_SymbolicToStScanCode_default(symkey);
	}
}

static uint8_t Keymap_SymbolicToStScanCode_FR(SDL_Keycode symkey)
{
	switch (symkey)
	{
	 case SDLK_QUOTE: return 0x05;
	 case SDLK_LEFTPAREN: return 0x06;
	 case SDLK_RIGHTPAREN: return 0x0c;
	 case SDLK_COMMA: return 0x32;
	 case SDLK_MINUS: return 0x0D;
	 case SDLK_SEMICOLON: return 0x33;
	 case SDLK_EQUALS: return 0x35;
	 case SDLK_CARET: return 0x1A;
	 case SDLK_a: return 0x10;
	 case SDLK_m: return 0x27;
	 case SDLK_q: return 0x1E;
	 case SDLK_w: return 0x2C;
	 case SDLK_z: return 0x11;
	 case 167: return 0x07;		/* French § */
	 default: return Keymap_SymbolicToStScanCode_default(symkey);
	}
}

static uint8_t Keymap_SymbolicToStScanCode_UK(SDL_Keycode symkey)
{
	switch (symkey)
	{
	 case SDLK_MINUS: return 0x0C;
	 case SDLK_BACKSLASH: return 0x60;
	 default: return Keymap_SymbolicToStScanCode_default(symkey);
	}
}

static uint8_t Keymap_SymbolicToStScanCode_ES(SDL_Keycode symkey)
{
	switch (symkey)
	{
	 case SDLK_MINUS: return 0x0C;
	 case SDLK_SEMICOLON: return 0x28;
	 case SDLK_BACKQUOTE: return 0x1B;
	 case 231: return 0x29;		/* Spanish ç */
	 default: return Keymap_SymbolicToStScanCode_default(symkey);
	}
}

static uint8_t Keymap_SymbolicToStScanCode_IT(SDL_Keycode symkey)
{
	switch (symkey)
	{
	 case SDLK_QUOTE: return 0x0C;
	 case SDLK_PLUS: return 0x1B;
	 case 224: return 0x28;		/* Italian à */
	 case 232: return 0x1A;		/* Italian è */
	 case 249: return 0x29;		/* Italian ù */
	 default: return Keymap_SymbolicToStScanCode_default(symkey);
	}
}

static uint8_t Keymap_SymbolicToStScanCode_SE(SDL_Keycode symkey)
{
	switch (symkey)
	{
	 case SDLK_QUOTE: return 0x29;
	 case SDLK_PLUS: return 0x0C;
	 case 252: return 0x1b;		/* ü */
	 default: return Keymap_SymbolicToStScanCode_default(symkey);
	}
}

/* Mapping for both, French and German variant of Swiss keyboard */
static uint8_t Keymap_SymbolicToStScanCode_CH(SDL_Keycode symkey)
{
	switch (symkey)
	{
	 case SDLK_CARET: return 0x0D;
	 case 224: return 0x28;		/* à */
	 case 232: return 0x1A;		/* è */
	 case 233: return 0x27;		/* é */
	 default: return Keymap_SymbolicToStScanCode_default(symkey);
	}
}

static uint8_t Keymap_SymbolicToStScanCode_NO(SDL_Keycode symkey)
{
	switch (symkey)
	{
	 case SDLK_QUOTE: return 0x29;
	 case SDLK_PLUS: return 0x0C;
	 case 230: return 0x28;		/* æ */
	 case 233: return 0x0D;		/* é */
	 case 248: return 0x27;		/* ø */
	 case 252: return 0x1b;		/* ü */
	 default: return Keymap_SymbolicToStScanCode_default(symkey);
	}
}

static uint8_t Keymap_SymbolicToStScanCode_DK(SDL_Keycode symkey)
{
	switch (symkey)
	{
	 case SDLK_QUOTE: return 0x0D;
	 case SDLK_PLUS: return 0x0C;
	 case SDLK_ASTERISK: return 0x1B;
	 case 230: return 0x27;		/* æ */
	 case 233: return 0x29;		/* é */
	 case 248: return 0x28;		/* ø */
	 default: return Keymap_SymbolicToStScanCode_default(symkey);
	}
}

static uint8_t Keymap_SymbolicToStScanCode_NL(SDL_Keycode symkey)
{
	switch (symkey)
	{
	 case SDLK_MINUS: return 0x0C;
	 case SDLK_BACKSLASH: return 0x60;
	 default: return Keymap_SymbolicToStScanCode_default(symkey);
	}
}

static uint8_t Keymap_SymbolicToStScanCode_CZ(SDL_Keycode symkey)
{
	switch (symkey)
	{
	 case SDLK_HASH: return 0x29;
	 case SDLK_QUOTE: return 0x0D;
	 case SDLK_EQUALS: return 0x0C;
	 case SDLK_y: return 0x2C;
	 case SDLK_z: return 0x15;
	 case 233: return 0x0B;		/* é */
	 default: return Keymap_SymbolicToStScanCode_default(symkey);
	}
}


/**
 * Remap SDL scancode key to ST Scan code
 */
static uint8_t Keymap_PcToStScanCode(SDL_Scancode scancode)
{
	switch (scancode)
	{
	 case SDL_SCANCODE_A: return 0x1e;
	 case SDL_SCANCODE_B: return 0x30;
	 case SDL_SCANCODE_C: return 0x2e;
	 case SDL_SCANCODE_D: return 0x20;
	 case SDL_SCANCODE_E: return 0x12;
	 case SDL_SCANCODE_F: return 0x21;
	 case SDL_SCANCODE_G: return 0x22;
	 case SDL_SCANCODE_H: return 0x23;
	 case SDL_SCANCODE_I: return 0x17;
	 case SDL_SCANCODE_J: return 0x24;
	 case SDL_SCANCODE_K: return 0x25;
	 case SDL_SCANCODE_L: return 0x26;
	 case SDL_SCANCODE_M: return 0x32;
	 case SDL_SCANCODE_N: return 0x31;
	 case SDL_SCANCODE_O: return 0x18;
	 case SDL_SCANCODE_P: return 0x19;
	 case SDL_SCANCODE_Q: return 0x10;
	 case SDL_SCANCODE_R: return 0x13;
	 case SDL_SCANCODE_S: return 0x1f;
	 case SDL_SCANCODE_T: return 0x14;
	 case SDL_SCANCODE_U: return 0x16;
	 case SDL_SCANCODE_V: return 0x2f;
	 case SDL_SCANCODE_W: return 0x11;
	 case SDL_SCANCODE_X: return 0x2d;
	 case SDL_SCANCODE_Y: return 0x15;
	 case SDL_SCANCODE_Z: return 0x2c;
	 case SDL_SCANCODE_1: return 0x02;
	 case SDL_SCANCODE_2: return 0x03;
	 case SDL_SCANCODE_3: return 0x04;
	 case SDL_SCANCODE_4: return 0x05;
	 case SDL_SCANCODE_5: return 0x06;
	 case SDL_SCANCODE_6: return 0x07;
	 case SDL_SCANCODE_7: return 0x08;
	 case SDL_SCANCODE_8: return 0x09;
	 case SDL_SCANCODE_9: return 0x0a;
	 case SDL_SCANCODE_0: return 0x0b;
	 case SDL_SCANCODE_RETURN: return 0x1c;
	 case SDL_SCANCODE_ESCAPE: return ST_ESC;
	 case SDL_SCANCODE_BACKSPACE: return 0x0e;
	 case SDL_SCANCODE_TAB: return 0x0f;
	 case SDL_SCANCODE_SPACE: return 0x39;
	 case SDL_SCANCODE_MINUS: return 0x0c;
	 case SDL_SCANCODE_EQUALS: return 0x0d;
	 case SDL_SCANCODE_LEFTBRACKET: return 0x1a;
	 case SDL_SCANCODE_RIGHTBRACKET: return 0x1b;
	 case SDL_SCANCODE_BACKSLASH: return 0x29;  /* for 0x60 see NONUSBACKSLASH */
	 case SDL_SCANCODE_NONUSHASH: return 0x2b;
	 case SDL_SCANCODE_SEMICOLON: return 0x27;
	 case SDL_SCANCODE_APOSTROPHE: return 0x28;
	 case SDL_SCANCODE_GRAVE: return 0x2b;      /* ok? */
	 case SDL_SCANCODE_COMMA: return 0x33;
	 case SDL_SCANCODE_PERIOD: return 0x34;
	 case SDL_SCANCODE_SLASH: return 0x35;
	 case SDL_SCANCODE_CAPSLOCK: return ST_CAPSLOCK;
	 case SDL_SCANCODE_F1: return 0x3b;
	 case SDL_SCANCODE_F2: return 0x3c;
	 case SDL_SCANCODE_F3: return 0x3d;
	 case SDL_SCANCODE_F4: return 0x3e;
	 case SDL_SCANCODE_F5: return 0x3f;
	 case SDL_SCANCODE_F6: return 0x40;
	 case SDL_SCANCODE_F7: return 0x41;
	 case SDL_SCANCODE_F8: return 0x42;
	 case SDL_SCANCODE_F9: return 0x43;
	 case SDL_SCANCODE_F10: return 0x44;
	 case SDL_SCANCODE_F11: return 0x62;
	 case SDL_SCANCODE_F12: return 0x61;
	 case SDL_SCANCODE_PRINTSCREEN: return 0x62;
	 case SDL_SCANCODE_SCROLLLOCK: return 0x61;
	 case SDL_SCANCODE_PAUSE: return 0x61;
	 case SDL_SCANCODE_INSERT: return 0x52;
	 case SDL_SCANCODE_HOME: return 0x47;
	 case SDL_SCANCODE_PAGEUP: return 0x63;
	 case SDL_SCANCODE_DELETE: return 0x53;
	 case SDL_SCANCODE_END: return 0x2b;
	 case SDL_SCANCODE_PAGEDOWN: return 0x64;
	 case SDL_SCANCODE_RIGHT: return 0x4d;
	 case SDL_SCANCODE_LEFT: return 0x4b;
	 case SDL_SCANCODE_DOWN: return 0x50;
	 case SDL_SCANCODE_UP: return 0x48;
	 case SDL_SCANCODE_NUMLOCKCLEAR: return 0x64;
	 case SDL_SCANCODE_KP_DIVIDE: return 0x65;
	 case SDL_SCANCODE_KP_MULTIPLY: return 0x66;
	 case SDL_SCANCODE_KP_MINUS: return 0x4a;
	 case SDL_SCANCODE_KP_PLUS: return 0x4e;
	 case SDL_SCANCODE_KP_ENTER: return 0x72;
	 case SDL_SCANCODE_KP_1: return 0x6d;
	 case SDL_SCANCODE_KP_2: return 0x6e;
	 case SDL_SCANCODE_KP_3: return 0x6f;
	 case SDL_SCANCODE_KP_4: return 0x6a;
	 case SDL_SCANCODE_KP_5: return 0x6b;
	 case SDL_SCANCODE_KP_6: return 0x6c;
	 case SDL_SCANCODE_KP_7: return 0x67;
	 case SDL_SCANCODE_KP_8: return 0x68;
	 case SDL_SCANCODE_KP_9: return 0x69;
	 case SDL_SCANCODE_KP_0: return 0x70;
	 case SDL_SCANCODE_KP_PERIOD: return 0x71;
	 case SDL_SCANCODE_NONUSBACKSLASH: return 0x60;
	 //case SDL_SCANCODE_APPLICATION: return ;
	 case SDL_SCANCODE_KP_EQUALS: return 0x63;
	 case SDL_SCANCODE_F13: return 0x63;
	 case SDL_SCANCODE_F14: return 0x64;
	 case SDL_SCANCODE_HELP: return 0x62;
	 case SDL_SCANCODE_UNDO: return 0x61;
	 case SDL_SCANCODE_KP_COMMA: return 0x71;
	 case SDL_SCANCODE_CLEAR: return 0x47;
	 case SDL_SCANCODE_RETURN2: return 0x1c;
	 case SDL_SCANCODE_KP_LEFTPAREN: return 0x63;
	 case SDL_SCANCODE_KP_RIGHTPAREN: return 0x64;
	 case SDL_SCANCODE_KP_LEFTBRACE: return 0x63;
	 case SDL_SCANCODE_KP_RIGHTBRACE: return 0x64;
	 case SDL_SCANCODE_KP_TAB: return 0x0f;
	 case SDL_SCANCODE_KP_BACKSPACE: return 0x0e;
	 case SDL_SCANCODE_KP_COLON: return 0x33;
	 case SDL_SCANCODE_KP_HASH: return 0x0c;
	 case SDL_SCANCODE_KP_SPACE: return 0x39;
	 case SDL_SCANCODE_KP_CLEAR: return 0x47;
	 case SDL_SCANCODE_LCTRL: return ST_CONTROL;
	 case SDL_SCANCODE_LSHIFT: return ST_LSHIFT;
	 case SDL_SCANCODE_LALT: return ST_ALTERNATE;
	 case SDL_SCANCODE_RCTRL: return ST_CONTROL;
	 case SDL_SCANCODE_RSHIFT: return ST_RSHIFT;
	 default:
		return ST_NO_SCANCODE;
	}
}


/**
 * Remap a keypad key to ST scan code. We use a separate function for this
 * so that we can easily toggle between number and cursor mode with the
 * numlock key.
 */
static uint8_t Keymap_GetKeyPadScanCode(SDL_Keycode symkey)
{
	if (SDL_GetModState() & KMOD_NUM)
	{
		switch (symkey)
		{
		 case SDLK_KP_1:  return 0x6d;  /* NumPad 1 */
		 case SDLK_KP_2:  return 0x6e;  /* NumPad 2 */
		 case SDLK_KP_3:  return 0x6f;  /* NumPad 3 */
		 case SDLK_KP_4:  return 0x6a;  /* NumPad 4 */
		 case SDLK_KP_5:  return 0x6b;  /* NumPad 5 */
		 case SDLK_KP_6:  return 0x6c;  /* NumPad 6 */
		 case SDLK_KP_7:  return 0x67;  /* NumPad 7 */
		 case SDLK_KP_8:  return 0x68;  /* NumPad 8 */
		 case SDLK_KP_9:  return 0x69;  /* NumPad 9 */
		 default:  break;
		}
	}
	else
	{
		switch (symkey)
		{
		 case SDLK_KP_1:  return 0x6d;  /* NumPad 1 */
		 case SDLK_KP_2:  return 0x50;  /* Cursor down */
		 case SDLK_KP_3:  return 0x6f;  /* NumPad 3 */
		 case SDLK_KP_4:  return 0x4b;  /* Cursor left */
		 case SDLK_KP_5:  return 0x50;  /* Cursor down (again?) */
		 case SDLK_KP_6:  return 0x4d;  /* Cursor right */
		 case SDLK_KP_7:  return 0x52;  /* Insert - good for Dungeon Master */
		 case SDLK_KP_8:  return 0x48;  /* Cursor up */
		 case SDLK_KP_9:  return 0x47;  /* Home - again for Dungeon Master */
		 default:  break;
		}
	}
	return ST_NO_SCANCODE;
}


/** LOG_TRACE: loaded keymap matched (enable with --trace keymap). */
static void Keymap_TraceKeymapHit(const char *source,
				  SDL_Scancode scancode, SDL_Keycode symkey,
				  SDL_Keymod mod,
				  const uint8_t *st_codes, unsigned int ncodes)
{
	unsigned int j;

	LOG_TRACE(TRACE_KEYMAP,
		  "keymap hit [%s]: scan=%d sym=%i mod=0x%x -> ",
		  source, (int)scancode, symkey, (unsigned)mod);
	for (j = 0; j < ncodes && j < MAX_ST_SCANCODES; j++)
		LOG_TRACE(TRACE_KEYMAP, "%02x ", st_codes[j]);
	LOG_TRACE(TRACE_KEYMAP, "(loaded keymap)\n");
}

/** LOG_TRACE: no loaded mapping matched this key event. */
static void Keymap_TraceKeymapMiss(const char *source,
				   SDL_Scancode scancode, SDL_Keycode symkey,
				   SDL_Keymod mod)
{
	LOG_TRACE(TRACE_KEYMAP,
		  "keymap miss [%s]: scan=%d sym=%i mod=0x%x\n",
		  source, (int)scancode, symkey, (unsigned)mod);
}


/**
 * Remap SDL Key to ST Scan code
 */
static uint8_t Keymap_RemapKeyToSTScanCode(SDL_Keycode symkey, SDL_Scancode scancode)
{
	/* Use loaded keymap? */
	if (simple_keymap_loaded)
	{
		int i;
		for (i = 0; i < KBD_MAX_SCANCODE && LoadedKeymap[i][1] != 0; i++)
		{
			bool match = LoadedKeymapHostIsScancode[i]
				? ((int)scancode == LoadedKeymap[i][0])
				: (symkey == (SDL_Keycode)LoadedKeymap[i][0]);
			if (match)
			{
				uint8_t guest = LoadedKeymap[i][1];

				Keymap_TraceKeymapHit("simple/remap", scancode, symkey,
						      KMOD_NONE, &guest, 1);
				return guest;
			}
		}
	}

	/* Check for keypad first so we can handle numlock */
	if (symkey >= SDLK_KP_1 && symkey <= SDLK_KP_9)
		return Keymap_GetKeyPadScanCode(symkey);

	/* Remap from PC scancodes? */
	if (ConfigureParams.Keyboard.nKeymapType == KEYMAP_SCANCODE)
	{
		uint8_t ret = Keymap_PcToStScanCode(scancode);
		if (ret != ST_NO_SCANCODE)
			return ret;
		if (symkey == 0)
		{
			Log_Printf(LOG_WARN, "Unhandled scancode 0x%x!\n",
			           scancode);
			return ret;
		}
		/* If we did not get a scancode, but symkey is non-zero,
		 * this likely means we're simulating a key press via
		 * Keymap_SimulateCharacter(), thus use the function
		 * Keymap_SymbolicToStScanCode() below to translate it */
	}

	/* Use symbolic mapping */
	return Keymap_SymbolicToStScanCode(symkey);
}


/**
 * Parse a hexadecimal scan-code token (optional 0x / 0X). The entire token
 * (after any prefix) must be hex digits. Value: 1 .. max_inclusive.
 */
static bool Keymap_ParseHexUint(const char *spec, long max_inclusive, int *out)
{
	const char *hex = spec;
	char *endptr;
	long v;
	size_t i;

	if (!spec || !*spec)
		return false;

	if (spec[0] == '0' && (spec[1] == 'x' || spec[1] == 'X'))
	{
		hex = spec + 2;
		if (!*hex)
			return false;
	}

	for (i = 0; hex[i]; i++)
	{
		if (!isxdigit((unsigned char)hex[i]))
			return false;
	}

	v = strtol(hex, &endptr, 16);
	if (*endptr != '\0' || v <= 0 || v > max_inclusive)
		return false;
	*out = (int)v;
	return true;
}


/**
 * Fill host (PC) key based on host "spec" string.
 * Return true on success
 */
static bool Keymap_ParseHostSpec(const char *spec, int *host_val, bool *host_is_scancode)
{
	if (!spec || !*spec)
		return false;

	*host_is_scancode = false;

	if (Keymap_ParseHexUint(spec, KEYS_DOWN_MAPPING_SIZE - 1, host_val))
	{
		*host_is_scancode = true;
		return true;
	}

	/* SDL key name (left, Return, F1, ...) — token is not all-hex digits */
	{
		int key = atoi(spec);    /* Direct key code? */
		if (key < 10)
		{
			/* If it's not a valid number >= 10, then
			 * assume we've got a symbolic key name
			 */
			int offset = 0;
			/* quoted character (e.g. comment line char)? */
			if (*spec == '\\' && strlen(spec) == 2)
				offset = 1;
			key = Keymap_GetKeyFromName(spec + offset);
		}
		if (key < 8)
		{
			Log_Printf(LOG_WARN, "Invalid PC key: '%s'\n", spec);
			return false;
		}
		*host_val = key;
		*host_is_scancode = false;
		return true;
	}
}


/**
 * Fill guest (ST) key based on guest "spec" string.
 * Return true on success
 */
static bool Keymap_ParseGuestSpec(const char *spec, int *scancode)
{
	if (!Keymap_ParseHexUint(spec, KBD_MAX_SCANCODE, scancode))
	{
		Log_Printf(LOG_WARN, "Invalid ST scancode (hex): '%s'\n", spec);
		return false;
	}
	return true;
}


static SDL_Keymod SDLKeymodFromName(const char *name)
{
	static const struct {
		SDL_Keymod mod;
		const char *nm;
	} keymodNames[] = {
		{ KMOD_NONE, "KMOD_NONE" },
		{ KMOD_LSHIFT, "KMOD_LSHIFT" },
		{ KMOD_RSHIFT, "KMOD_RSHIFT" },
		{ KMOD_LCTRL, "KMOD_LCTRL" },
		{ KMOD_RCTRL, "KMOD_RCTRL" },
		{ KMOD_LALT, "KMOD_LALT" },
		{ KMOD_RALT, "KMOD_RALT" },
		{ KMOD_LGUI, "KMOD_LGUI" },
		{ KMOD_RGUI, "KMOD_RGUI" },
		{ KMOD_NUM, "KMOD_NUM" },
		{ KMOD_CAPS, "KMOD_CAPS" },
		{ KMOD_MODE, "KMOD_MODE" },
		{ KMOD_CTRL, "KMOD_CTRL" },
		{ KMOD_SHIFT, "KMOD_SHIFT" },
		{ KMOD_ALT, "KMOD_ALT" },
		{ KMOD_GUI, "KMOD_GUI" },
		{ 0, NULL }
	};
	int i;

	for (i = 0; keymodNames[i].nm != NULL; i++)
	{
		if (strcmp(name, keymodNames[i].nm) == 0)
			return keymodNames[i].mod;
	}

	LOG_TRACE(TRACE_KEYMAP, "SDLKeymodFromName: unknown \"%s\", using KMOD_NONE.\n", name);
	return KMOD_NONE;
}


/**
 * Parse extended host spec: hex-scancode [| modifier [| mask]]
 * Returns 0 on failure, non-zero on success (matches legacy helper).
 */
static int HostSpecToSDLKeysym(const char *spec, struct KeyMapping* result)
{
	char buf[100];
	char *token;
	SDL_Scancode scancode;

	strcpy(buf, spec);
	result->SdlKeysym.mod = 0;
	result->modmask = 0;

	token = strtok(buf, "|");
	if (token == NULL)
		return 0;
	{
		int sc_int;
		if (!Keymap_ParseHexUint(token, KEYS_DOWN_MAPPING_SIZE - 1, &sc_int))
			return 0;
		scancode = (SDL_Scancode)sc_int;
	}

	result->SdlKeysym.scancode = scancode;

	token = strtok(NULL, "|");
	if (token != NULL)
	{
		result->SdlKeysym.mod = SDLKeymodFromName(token);
		token = strtok(NULL, "|");
		if (token != NULL)
			result->modmask = SDLKeymodFromName(token);
	}

	return -1;
}


static bool GuestSpec_ParseOneSegment(const char *start, uint8_t *scancode_out)
{
	int v;

	if (!start || !*start)
		return false;

	if (Keymap_ParseHexUint(start, KBD_MAX_SCANCODE, &v))
	{
		*scancode_out = (uint8_t)v;
		return true;
	}

	if (strcmp(start, "LSHIFT") == 0)
	{
		*scancode_out = ST_LSHIFT;
		return true;
	}
	if (strcmp(start, "RSHIFT") == 0)
	{
		*scancode_out = ST_RSHIFT;
		return true;
	}
	if (strcmp(start, "ALTERNATE") == 0)
	{
		*scancode_out = ST_ALTERNATE;
		return true;
	}
	return false;
}


static int GuestSpecToSTScanCodes(const char *spec, struct KeyMapping* mapping)
{
	char buf[100];
	char *start;
	char *separator;
	uint8_t *scancodes = mapping->STScanCodes;
	int i = 0;

	strcpy(buf, spec);
	separator = buf - 1;
	do
	{
		start = separator + 1;
		separator = strchr(start, '|');
		if (separator != NULL)
			*separator = '\0';

		if (!GuestSpec_ParseOneSegment(start, &scancodes[i]))
		{
			Log_Printf(LOG_ERROR, "GuestSpecToSTScanCodes: unknown '%s'\n", start);
			i = 0;
			break;
		}

		if (i >= MAX_ST_SCANCODES)
			break;
		i++;
	} while (separator != NULL);

	mapping->STScanCodesLength = (uint8_t)i;

	return i > 0;
}


static bool InputMatchesKeyMapping(const SDL_Keysym* keySym, const struct KeyMapping *mapping)
{
	return keySym->scancode == mapping->SdlKeysym.scancode
		&& (keySym->mod & mapping->modmask) == mapping->SdlKeysym.mod;
}


/** File-driven mapping chain (skip keypad; simple table then symbolic). */
static bool Keymap_StrictMappingFileMode(void)
{
	return ConfigureParams.Keyboard.nKeymapType == KEYMAP_LOADED
	       || extended_mapping_count > 0;
}


/**
 * Build mapping for this key event into *out (multi ST codes supported).
 */
static void Keymap_FillKeysDownMapping(SDL_Keymod modkey, SDL_Keycode symkey,
				       SDL_Scancode scancode, struct KeyMapping *out)
{
	SDL_Keysym ks;
	uint8_t st;
	bool strict_mode;

	memset(out, 0, sizeof(*out));

	ks.sym = symkey;
	ks.scancode = scancode;
	ks.mod = modkey;
	strict_mode = Keymap_StrictMappingFileMode();

	LOG_TRACE(TRACE_KEYMAP,
		  "fill mapping: scan=%d sym=%i mod=0x%x type=%d strict=%d simple=%d extended=%d\n",
		  (int)scancode, symkey, (unsigned)modkey,
		  ConfigureParams.Keyboard.nKeymapType, strict_mode,
		  simple_keymap_loaded ? 1 : 0, extended_mapping_count);

	if (extended_mapping_count > 0)
	{
		int i;

		for (i = 0; i < extended_mapping_count; i++)
		{
			if (!InputMatchesKeyMapping(&ks, &LoadedKeyMap[i]))
				continue;
			{
				char tag[32];

				snprintf(tag, sizeof(tag), "extended #%d", i);
				Keymap_TraceKeymapHit(tag, scancode, symkey, modkey,
						      LoadedKeyMap[i].STScanCodes,
						      LoadedKeyMap[i].STScanCodesLength);
			}
			*out = LoadedKeyMap[i];
			return;
		}
		Keymap_TraceKeymapMiss("extended table", scancode, symkey, modkey);
	}

	if (!strict_mode)
	{
		if (symkey >= SDLK_KP_1 && symkey <= SDLK_KP_9)
		{
			st = Keymap_GetKeyPadScanCode(symkey);
			if (st != ST_NO_SCANCODE)
			{
				out->STScanCodes[0] = st;
				out->STScanCodesLength = 1;
				LOG_TRACE(TRACE_KEYMAP, "keymap hit [keypad]: %02x\n", st);
			}
			else
			{
				Keymap_TraceKeymapMiss("keypad", scancode, symkey, modkey);
			}
			return;
		}
	}

	if (!strict_mode && ConfigureParams.Keyboard.nKeymapType == KEYMAP_SCANCODE)
	{
		st = Keymap_PcToStScanCode(scancode);
		if (st != ST_NO_SCANCODE)
		{
			out->STScanCodes[0] = st;
			out->STScanCodesLength = 1;
			LOG_TRACE(TRACE_KEYMAP, "keymap hit [pc scancode]: %02x\n", st);
			return;
		}
		if (symkey == 0)
		{
			Log_Printf(LOG_WARN, "Unhandled scancode 0x%x!\n", scancode);
			return;
		}
	}
	else if (strict_mode && ConfigureParams.Keyboard.nKeymapType == KEYMAP_SCANCODE)
	{
		LOG_TRACE(TRACE_KEYMAP,
			  "strict keymap mode active: skipping pc scancode fallback\n");
	}

	if (strict_mode)
	{
		if (simple_keymap_loaded)
		{
			int i;

			for (i = 0; i < KBD_MAX_SCANCODE && LoadedKeymap[i][1] != 0; i++)
			{
				bool match = LoadedKeymapHostIsScancode[i]
					? ((int)scancode == LoadedKeymap[i][0])
					: (symkey == (SDL_Keycode)LoadedKeymap[i][0]);
				if (match)
				{
					uint8_t guest = LoadedKeymap[i][1];

					Keymap_TraceKeymapHit("simple table", scancode, symkey,
							      modkey, &guest, 1);
					out->STScanCodes[0] = guest;
					out->STScanCodesLength = 1;
					return;
				}
			}
			Keymap_TraceKeymapMiss("simple table", scancode, symkey, modkey);
		}
		st = Keymap_SymbolicToStScanCode(symkey);
		if (st != ST_NO_SCANCODE)
		{
			LOG_TRACE(TRACE_KEYMAP, "keymap hit [symbolic strict fallback]: %02x\n", st);
		}
		else
			Keymap_TraceKeymapMiss("symbolic strict fallback", scancode, symkey, modkey);
	}
	else
	{
		st = Keymap_RemapKeyToSTScanCode(symkey, scancode);
		if (st != ST_NO_SCANCODE)
		{
			LOG_TRACE(TRACE_KEYMAP, "keymap hit [legacy remap path]: %02x\n", st);
		}
		else
			Keymap_TraceKeymapMiss("legacy remap path", scancode, symkey, modkey);
	}

	if (st != ST_NO_SCANCODE)
	{
		out->STScanCodes[0] = st;
		out->STScanCodesLength = 1;
	}
}


/**
 * Load keyboard remap file
 */
void Keymap_LoadRemapFile(const char *pszFileName)
{
	FILE *in;
	int linenro, fails;
	int idx_simple = 0;
	int idx_ext = 0;

	/* Initialize table with default values */
	memset(LoadedKeymap, 0, sizeof(LoadedKeymap));
	memset(LoadedKeymapHostIsScancode, 0, sizeof(LoadedKeymapHostIsScancode));
	memset(LoadedKeyMap, 0, sizeof(LoadedKeyMap));
	simple_keymap_loaded = false;
	extended_mapping_count = 0;

	if (!*pszFileName)
		return;

	/* Attempt to load file */
	if (!File_Exists(pszFileName))
	{
		Log_Printf(LOG_WARN, "The keymap file '%s' does not exist\n",
		           pszFileName);
		return;
	}
	in = fopen(pszFileName, "r");
	if (!in)
	{
		Log_Printf(LOG_ERROR, "Failed to open keymap file '%s'\n", pszFileName);
		return;
	}

	linenro = fails = 0;
	while (!feof(in))
	{
		char *line, *saveptr, buf[1024];
		const char *host, *guest;

		/* Read line from file */
		if (fgets(buf, sizeof(buf), in) == NULL)
			break;
		linenro++;

		/* Remove white-space from start of line */
		line = Str_Trim(buf);

		/* Ignore empty line and comments */
		if (strlen(line) == 0 || line[0] == ';' || line[0] == '#')
			continue;

		/* get the host (PC) key spec */
		host = Str_Trim(strtok_r(line, ",", &saveptr));
		if (!host)
		{
			Log_Printf(LOG_WARN, "Keymap line %d missing host field in '%s'\n",
				   linenro, pszFileName);
			fails++;
			continue;
		}

		/* Get the guest (ST) key spec */
		guest = Str_Trim(strtok_r(NULL, "\n", &saveptr));
		if (!guest || strlen(guest) == 0)
		{
			Log_Printf(LOG_WARN, "Keymap line %d missing guest field in '%s'\n",
				   linenro, pszFileName);
			fails++;
			continue;
		}

		if (strchr(host, '|') != NULL)
		{
			if (idx_ext >= (int)ARRAY_SIZE(LoadedKeyMap))
			{
				Log_Printf(LOG_WARN, "Too many extended mappings in '%s'\n", pszFileName);
				fails++;
				break;
			}
			memset(&LoadedKeyMap[idx_ext], 0, sizeof(LoadedKeyMap[idx_ext]));
			if (HostSpecToSDLKeysym(host, &LoadedKeyMap[idx_ext]) != -1
			    || !GuestSpecToSTScanCodes(guest, &LoadedKeyMap[idx_ext]))
			{
				Log_Printf(LOG_WARN, "Extended keymap parse failed line %d in '%s'\n",
					   linenro, pszFileName);
				fails++;
				memset(&LoadedKeyMap[idx_ext], 0, sizeof(LoadedKeyMap[idx_ext]));
				continue;
			}
			idx_ext++;
			LOG_TRACE(TRACE_KEYMAP, "extended host '%s' => guest '%s'\n", host, guest);
			continue;
		}

		/* Simple hex PC host + chained ST guest (e.g. 37,LSHIFT|0d) */
		if (strchr(guest, '|') != NULL)
		{
			int pc_scan;
			struct KeyMapping *m;

			if (idx_ext >= (int)ARRAY_SIZE(LoadedKeyMap))
			{
				Log_Printf(LOG_WARN, "Too many extended mappings in '%s'\n", pszFileName);
				fails++;
				break;
			}

			if (!Keymap_ParseHexUint(host, KEYS_DOWN_MAPPING_SIZE - 1, &pc_scan))
			{
				Log_Printf(LOG_WARN,
					   "Keymap line %d in '%s': guest contains '|' but host '%s' must be a hex PC scancode\n",
					   linenro, pszFileName, host);
				fails++;
				continue;
			}

			memset(&LoadedKeyMap[idx_ext], 0, sizeof(LoadedKeyMap[idx_ext]));
			m = &LoadedKeyMap[idx_ext];
			m->SdlKeysym.scancode = (SDL_Scancode)pc_scan;
			m->SdlKeysym.mod = 0;
			m->modmask = 0;

			if (!GuestSpecToSTScanCodes(guest, m))
			{
				Log_Printf(LOG_WARN,
					   "Chained guest parse failed line %d in '%s'\n",
					   linenro, pszFileName);
				fails++;
				memset(&LoadedKeyMap[idx_ext], 0, sizeof(LoadedKeyMap[idx_ext]));
				continue;
			}

			idx_ext++;
			LOG_TRACE(TRACE_KEYMAP, "hex host + chained guest '%s' => '%s'\n", host, guest);
			continue;
		}

		if (idx_simple >= (int)ARRAY_SIZE(LoadedKeymap))
		{
			Log_Printf(LOG_WARN, "Too many simple mappings in '%s'\n", pszFileName);
			fails++;
			break;
		}

		if (!Keymap_ParseHostSpec(host, &LoadedKeymap[idx_simple][0],
					  &LoadedKeymapHostIsScancode[idx_simple])
		    || !Keymap_ParseGuestSpec(guest, &LoadedKeymap[idx_simple][1]))
		{
			Log_Printf(LOG_WARN, "Simple keymap parse failed line %d in '%s'\n",
				   linenro, pszFileName);
			fails++;
			continue;
		}

		idx_simple++;
		LOG_TRACE(TRACE_KEYMAP, "key mapping from file: host %s => guest %s\n",
			  host, guest);
	}

	fclose(in);

	if (idx_simple > 0)
		simple_keymap_loaded = true;
	extended_mapping_count = idx_ext;

	if (fails)
		Log_AlertDlg(LOG_ERROR, "%d keymap file parsing failures\n(see console log for details)", fails);
}


/**
 * Returns false if SDL_Keycode is for modifier key that
 * won't be converted to ST scancode, true otherwise
 */
static bool IsKeyTranslatable(SDL_Keycode symkey)
{
	switch (symkey)
	{
	case SDLK_RALT:
	case SDLK_LGUI:
	case SDLK_RGUI:
	case SDLK_MODE:
	case SDLK_NUMLOCKCLEAR:
		return false;
	}
	return true;
}


/**
 * Is a shortcut modifier key pressed?
 */
static bool Keymap_IsShortCutMod(SDL_Keymod modkey)
{
	return (modkey & (KMOD_RALT|KMOD_LGUI|KMOD_RGUI|KMOD_MODE)) != 0;
}


static bool IsSTModifier(uint8_t scancode)
{
	switch (scancode)
	{
	case ST_LSHIFT:
	case ST_RSHIFT:
	case ST_ALTERNATE:
		return true;
	default:
		return false;
	}
}


/**
 * User pressed a key down
 */
void Keymap_KeyDown(SDL_Keymod modkey, SDL_Keycode symkey, SDL_Scancode scancode)
{
	struct KeyMapping *mapping;
	uint8_t *modifiers;
	unsigned ui_scancode;
	int i;

	LOG_TRACE(TRACE_KEYMAP, "key down: sym=%i scan=%i mod=0x%x name='%s'\n",
	          symkey, scancode, modkey, Keymap_GetKeyName(symkey));

	if (symkey != SDLK_UNKNOWN)
	{
		if (ShortCut_CheckKeys(symkey, Keymap_IsShortCutMod(modkey), true))
		{
			LOG_TRACE(TRACE_KEYMAP, "key down ignored: shortcut consumed\n");
			return;
		}
	}

	if (!(modkey & KMOD_SHIFT))
	{
		/* If using joystick emulation via keyboard, DON'T send keys to
		 * keyboard processor - some games use keyboard as pause! */
		if (Joy_KeyDown(symkey))
		{
			LOG_TRACE(TRACE_KEYMAP, "key down ignored: joystick emulation consumed\n");
			return;
		}
	}

	/* Ignore modifier keys that are not passed to the ST */
	if (!IsKeyTranslatable(symkey))
	{
		LOG_TRACE(TRACE_KEYMAP, "key down ignored: not translatable to ST\n");
		return;
	}

	ui_scancode = (unsigned)scancode;
	if (ui_scancode >= KEYS_DOWN_MAPPING_SIZE)
	{
		LOG_TRACE(TRACE_KEYMAP, "key down ignored: scancode %u outside mapping table\n", ui_scancode);
		return;
	}

	mapping = &KeysDownMapping[ui_scancode];
	Keymap_FillKeysDownMapping(modkey, symkey, scancode, mapping);

	if (mapping->STScanCodesLength == 0)
	{
		LOG_TRACE(TRACE_KEYMAP, "key down: no ST mapping produced\n");
		return;
	}

	modifiers = mapping->PressedModifiers;
	for (i = 0; i < mapping->STScanCodesLength; i++)
	{
		uint8_t sc = mapping->STScanCodes[i];

		Keyboard.KeyStates[sc]++;

		if (IsSTModifier(sc))
			*modifiers++ = sc;

		if (Keyboard.KeyStates[sc] == 1)
		{
			/* Set down */
			IKBD_PressSTKey(sc, true);
		}
	}
	*modifiers = 0;
}


/*-----------------------------------------------------------------------*/
/**
 * User released a key
 */
void Keymap_KeyUp(SDL_Keymod modkey, SDL_Keycode symkey, SDL_Scancode scancode)
{
	struct KeyMapping *mapping;
	uint8_t *modifier;
	unsigned ui_scancode;
	int i;

	LOG_TRACE(TRACE_KEYMAP, "key up: sym=%i scan=%i mod=0x%x name='%s'\n",
	          symkey, scancode, modkey, Keymap_GetKeyName(symkey));

	/* Ignore short-cut keys here */
	if (symkey != SDLK_UNKNOWN)
	{
		if (ShortCut_CheckKeys(symkey, Keymap_IsShortCutMod(modkey), false))
		{
			LOG_TRACE(TRACE_KEYMAP, "key up ignored: shortcut consumed\n");
			return;
		}
	}

	if (!(modkey & KMOD_SHIFT))
	{
		/* If using keyboard emulation, DON'T send keys to keyboard
		 * processor - some games use keyboard as pause! */
		if (Joy_KeyUp(symkey))
		{
			LOG_TRACE(TRACE_KEYMAP, "key up ignored: joystick emulation consumed\n");
			return;
		}
	}

	/* Ignore modifier keys that are not passed to the ST */
	if (!IsKeyTranslatable(symkey))
	{
		LOG_TRACE(TRACE_KEYMAP, "key up ignored: not translatable to ST\n");
		return;
	}

	ui_scancode = (unsigned)scancode;
	if (ui_scancode >= KEYS_DOWN_MAPPING_SIZE)
	{
		LOG_TRACE(TRACE_KEYMAP, "key up ignored: scancode %u outside mapping table\n", ui_scancode);
		return;
	}

	mapping = &KeysDownMapping[ui_scancode];
	if (mapping->STScanCodesLength == 0)
	{
		LOG_TRACE(TRACE_KEYMAP, "key up: no stored mapping for scancode %u\n", ui_scancode);
		return;
	}

	/* Release key (only if was pressed) */
	for (i = 0; i < mapping->STScanCodesLength; i++)
	{
		uint8_t sc = mapping->STScanCodes[i];

		if (IsSTModifier(sc) && mapping->STScanCodesLength > 1)
			continue;

		if (Keyboard.KeyStates[sc])
		{
			Keyboard.KeyStates[sc]--;
			IKBD_PressSTKey(sc, false);
		}
	}

	modifier = mapping->PressedModifiers;
	while (*modifier)
	{
		if (Keyboard.KeyStates[*modifier] != 0)
		{
			if (--Keyboard.KeyStates[*modifier] == 0)
				IKBD_PressSTKey(*modifier, false);
		}
		modifier++;
	}

	memset(mapping, 0, sizeof(*mapping));
}


/*-----------------------------------------------------------------------*/
/**
 * Simulate press or release of a key corresponding to given character
 */
void Keymap_SimulateCharacter(char asckey, bool press)
{
	SDL_Keycode symkey;
	SDL_Keymod modkey = KMOD_NONE;

	if (isupper((unsigned char)asckey))
	{
		if (press)
		{
			symkey = SDLK_LSHIFT;
			Keymap_KeyDown(modkey, symkey, 0);
		}
		symkey = tolower((unsigned char)asckey);
		modkey = KMOD_LSHIFT;
	}
	else
	{
		symkey = asckey;
	}
	if (press)
	{
		Keymap_KeyDown(modkey, symkey, 0);
	}
	else
	{
		Keymap_KeyUp(modkey, symkey, 0);
		if (isupper((unsigned char)asckey))
		{
			symkey = SDLK_LSHIFT;
			Keymap_KeyUp(modkey, symkey, 0);
		}
	}
}


/**
 * Maps a key name to its SDL keycode
 */
int Keymap_GetKeyFromName(const char *name)
{
	return SDL_GetKeyFromName(name);
}


/**
 * Maps an SDL keycode to a name
 */
const char *Keymap_GetKeyName(int keycode)
{
	if (!keycode)
		return "";

	return SDL_GetKeyName(keycode);
}


/**
 * Informs symbolic keymap of loaded TOS country.
 */
void Keymap_SetCountry(int countrycode)
{
	uint8_t (*func)(SDL_Keycode keysym);

	/* Prefer keyboard layout selected by user */
	if (ConfigureParams.Keyboard.nKbdLayout >= 0 &&
	    ConfigureParams.Keyboard.nKbdLayout <= 31)
	{
		countrycode = ConfigureParams.Keyboard.nKbdLayout;
	}
	else if (countrycode == TOS_LANG_ALL)
	{
		if (NvRam_Present())
		{
			countrycode = NvRam_GetKbdLayoutCode();
		}
		else if (ConfigureParams.Keyboard.nCountryCode >= 0 &&
		         ConfigureParams.Keyboard.nCountryCode <= 31)
		{
			countrycode = ConfigureParams.Keyboard.nCountryCode;
		}
	}

	switch (countrycode)
	{
	 case TOS_LANG_US:    func = Keymap_SymbolicToStScanCode_US; break;
	 case TOS_LANG_DE:    func = Keymap_SymbolicToStScanCode_DE; break;
	 case TOS_LANG_FR:    func = Keymap_SymbolicToStScanCode_FR; break;
	 case TOS_LANG_UK:    func = Keymap_SymbolicToStScanCode_UK; break;
	 case TOS_LANG_ES:    func = Keymap_SymbolicToStScanCode_ES; break;
	 case TOS_LANG_IT:    func = Keymap_SymbolicToStScanCode_IT; break;
	 case TOS_LANG_FI:    /* Finish seems to be the same as Swedish */
	 case TOS_LANG_SE:    func = Keymap_SymbolicToStScanCode_SE; break;
	 case TOS_LANG_CH_FR:
	 case TOS_LANG_CH_DE: func = Keymap_SymbolicToStScanCode_CH; break;
	 case TOS_LANG_NO:    func = Keymap_SymbolicToStScanCode_NO; break;
	 case TOS_LANG_DK:    func = Keymap_SymbolicToStScanCode_DK; break;
	 case TOS_LANG_NL:    func = Keymap_SymbolicToStScanCode_NL; break;
	 case TOS_LANG_CS:    func = Keymap_SymbolicToStScanCode_CZ; break;
	 default: func = Keymap_SymbolicToStScanCode_default; break;
	}

	Keymap_SymbolicToStScanCode = func;
}


/**
 * Check whether one of the shift keys is hold down
 */
bool Keymap_IsShiftPressed(void)
{
	return (SDL_GetModState() & KMOD_SHIFT) != 0;
}
