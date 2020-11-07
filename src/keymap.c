/*
  Hatari - keymap.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file is about being able to map SDL key events to scancodes to send
  to the IKBS as pressed/released keys.
  It is done in several ways, controlled by the configuration.
*/
const char Keymap_fileid[] = "Hatari keymap.c";

#include <ctype.h>
#include "main.h"
#include "keymap.h"
#include "configuration.h"
#include "file.h"
#include "ikbd.h"
#include "joy.h"
#include "shortcut.h"
#include "str.h"
#include "screen.h"
#include "debugui.h"
#include "log.h"

#define MAX_SDLK_SCANCODES 284+1

/* Scancode of ST keyboard */
#define ST_ESC		 0x01
#define ST_CTRL		 0x1d
#define ST_LSHIFT	 0x2a
#define ST_RSHIFT	 0x36
#define ST_ALTERNATE 0x38
#define ST_CAPSLOCK	 0x3a

#if !WITH_SDL2
/* This table is used to translate a symbolic keycode to the (SDL) scancode */
static Uint8 SdlSymToSdlScan[SDLK_LAST];
#endif


/* Key mappings: pair a SDL definition with a list of keys we have to press/release on the ST */
#define MAX_ST_SCANCODES 4 /* Max is alt-a-b-c, so 4 chars */
struct KeyMapping
{
	/* Input on PC keyboard */
	SDL_keysym SdlKeysym;
	SDL_Keymod modmask;
	/* Output on the ST's keyboard */
	uint8_t	   STScanCodesLength;
	uint8_t    STScanCodes[MAX_ST_SCANCODES];
	/* What modifiers we effectively had to press (and will have to release when the key is up.
	 * WARNING: this assumes the keyboard doesn't let you press a key that is already pressed. */
	uint8_t	   PressedModifiers[MAX_ST_SCANCODES];
};
static struct KeyMapping LoadedKeyMap[KBD_MAX_SCANCODE];
static struct KeyMapping KeysDownMapping[MAX_SDLK_SCANCODES]; /* Mappings associated with keys when they're down. The index is the SDL scancode. */

/* List of ST scan codes to NOT de-bounce when running in maximum speed */
static const char DebounceExtendedKeys[] =
{
	ST_CTRL,
	ST_LSHIFT,
	ST_ESC,
	ST_ALTERNATE,
	ST_RSHIFT,
	0  /* End of list */
};

/* Helper functions for parsing the keymap file */
static int HostSpecToSDLKeysym(const char *spec, struct KeyMapping* result);
static int GuestSpecToSTScanCodes(const char *spec, struct KeyMapping *result);
static SDL_Keymod SDLKeymodFromName(const char *name);

/*-----------------------------------------------------------------------*/
/**
 * Initialization.
 */
void Keymap_Init(void)
{
#if !WITH_SDL2
	/* Clear arrays */
	memset(SdlSymToSdlScan, 0, sizeof(SdlSymToSdlScan));
	memset(KeysDownMapping, 0, sizeof(KeysDown));
#endif
	Keymap_LoadRemapFile(ConfigureParams.Keyboard.szMappingFileName);
}

/**
 * Map SDL symbolic key to ST scan code.
 * This assumes a QWERTY ST keyboard.
 */
static uint8_t Keymap_SymbolicToStScanCode(const SDL_keysym* pKeySym)
{
	uint8_t code;

	switch (pKeySym->sym)
	{
	 case SDLK_BACKSPACE: code = 0x0E; break;
	 case SDLK_TAB: code = 0x0F; break;
	 case SDLK_CLEAR: code = 0x47; break;
	 case SDLK_RETURN: code = 0x1C; break;
	 case SDLK_ESCAPE: code = ST_ESC; break;
	 case SDLK_SPACE: code = 0x39; break;
	 case SDLK_EXCLAIM: code = 0x09; break;     /* on azerty? */
	 case SDLK_QUOTEDBL: code = 0x04; break;    /* on azerty? */
	 case SDLK_HASH: code = 0x29; break;
	 case SDLK_DOLLAR: code = 0x1b; break;      /* on azerty */
	 case SDLK_AMPERSAND: code = 0x02; break;   /* on azerty? */
	 case SDLK_QUOTE: code = 0x28; break;
	 case SDLK_LEFTPAREN: code = 0x63; break;
	 case SDLK_RIGHTPAREN: code = 0x64; break;
	 case SDLK_ASTERISK: code = 0x66; break;
	 case SDLK_PLUS: code = 0x1B; break;
	 case SDLK_COMMA: code = 0x33; break;
	 case SDLK_MINUS: code = 0x35; break;
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
	 case SDLK_LEFTBRACKET: code = 0x63; break;
	 case SDLK_BACKSLASH: code = 0x2B; break;     /* Might be 0x60 for UK keyboards */
	 case SDLK_RIGHTBRACKET: code = 0x64; break;
	 case SDLK_CARET: code = 0x2B; break;
	 case SDLK_UNDERSCORE: code = 0x0C; break;
	 case SDLK_BACKQUOTE: code = 0x52; break;
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
#if WITH_SDL2
	 case 180: code = 0x0D; break;
	 case 223: code = 0x0C; break;
	 case 228: code = 0x28; break;
	 case 246: code = 0x27; break;
	 case 252: code = 0x1A; break;
#else /* !WITH_SDL2 */
	 case SDLK_WORLD_0: code = 0x0d; break;
	 case SDLK_WORLD_1: code = 0x0c; break;
	 case SDLK_WORLD_2: code = 0x1a; break;
	 case SDLK_WORLD_3: code = 0x28; break;
	 case SDLK_WORLD_4: code = 0x27; break;
	 case SDLK_WORLD_20: code = 0x0D; break;
	 case SDLK_WORLD_63: code = 0x0C; break;
	 case SDLK_WORLD_68: code = 0x28; break;
	 case SDLK_WORLD_86: code = 0x27; break;
	 case SDLK_WORLD_92: code = 0x1A; break;
#endif /* !WITH_SDL2 */
	 /* Numeric keypad: */
	 case SDLK_KP0: code = 0x70; break;
	 case SDLK_KP1: code = 0x6D; break;
	 case SDLK_KP2: code = 0x6E; break;
	 case SDLK_KP3: code = 0x6F; break;
	 case SDLK_KP4: code = 0x6A; break;
	 case SDLK_KP5: code = 0x6B; break;
	 case SDLK_KP6: code = 0x6C; break;
	 case SDLK_KP7: code = 0x67; break;
	 case SDLK_KP8: code = 0x68; break;
	 case SDLK_KP9: code = 0x69; break;
	 case SDLK_KP_PERIOD: code = 0x71; break;
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
	 case SDLK_END: code = 0x61; break;
	 case SDLK_PAGEUP: code = 0x63; break;
	 case SDLK_PAGEDOWN: code = 0x64; break;
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
	 case SDLK_F11: code = 0x62; break;
	 case SDLK_F12: code = 0x61; break;
	 case SDLK_F13: code = 0x62; break;
	 /* Key state modifier keys */
	 case SDLK_CAPSLOCK: code = ST_CAPSLOCK; break;
	 case SDLK_SCROLLOCK: code = 0x61; break;
	 case SDLK_RSHIFT: code = ST_RSHIFT; break;
	 case SDLK_LSHIFT: code = ST_LSHIFT; break;
	 case SDLK_RCTRL: code = ST_CTRL; break;
	 case SDLK_LCTRL: code = ST_CTRL; break;
	 case SDLK_RALT: code = ST_ALTERNATE; break;
	 case SDLK_LALT: code = ST_ALTERNATE; break;
	 /* Miscellaneous function keys */
	 case SDLK_HELP: code = 0x62; break;
	 case SDLK_PRINT: code = 0x62; break;
	 case SDLK_UNDO: code = 0x61; break;
	 default: code = -1;
	}

	return code;
}


#if WITH_SDL2

/**
 * Remap SDL scancode key to ST Scan code - this is the version for SDL2
 */
static uint8_t Keymap_PcToStScanCode(const SDL_keysym* pKeySym)
{
	switch (pKeySym->scancode)
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
	 case SDL_SCANCODE_ESCAPE: return 0x01;
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
	 case SDL_SCANCODE_CAPSLOCK: return 0x3a;
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
	 case SDL_SCANCODE_LCTRL: return ST_CTRL;
	 case SDL_SCANCODE_LSHIFT: return ST_LSHIFT;
	 case SDL_SCANCODE_LALT: return ST_ALTERNATE;
	 case SDL_SCANCODE_RCTRL: return ST_CTRL;
	 case SDL_SCANCODE_RSHIFT: return ST_RSHIFT;
	 default:
		if (!pKeySym->scancode && pKeySym->sym)
		{
			/* assume SimulateKey
			 * -> KeyUp/Down
			 *    -> Remap (with scancode mode configured)
			 *       -> PcToStScanCode
			 */
			return Keymap_SymbolicToStScanCode(pKeySym);
		}
		Log_Printf(LOG_WARN, "Unhandled scancode 0x%x!\n", pKeySym->scancode);
		return -1;
	}
}

#else /* !WITH_SDL2 */

/**
 * Heuristic analysis to find out the obscure scancode offset.
 * Some keys like 'z' can't be used for detection since they are on different
 * locations on "qwertz" and "azerty" keyboards.
 * This clever code has originally been taken from the emulator Aranym. (cheers!)
 */
static int Keymap_FindScanCodeOffset(const SDL_keysym* keysym)
{
	int offset = -1;    /* uninitialized scancode offset */
	int scanPC = keysym->scancode;

	if (scanPC == 0)
		return -1; /* Ignore illegal scancode */

	switch (keysym->sym)
	{
	 case SDLK_ESCAPE: offset = scanPC - 0x01; break;
	 case SDLK_1:  offset = scanPC - 0x02; break;
	 case SDLK_2:  offset = scanPC - 0x03; break;
	 case SDLK_3:  offset = scanPC - 0x04; break;
	 case SDLK_4:  offset = scanPC - 0x05; break;
	 case SDLK_5:  offset = scanPC - 0x06; break;
	 case SDLK_6:  offset = scanPC - 0x07; break;
	 case SDLK_7:  offset = scanPC - 0x08; break;
	 case SDLK_8:  offset = scanPC - 0x09; break;
	 case SDLK_9:  offset = scanPC - 0x0a; break;
	 case SDLK_0:  offset = scanPC - 0x0b; break;
	 case SDLK_BACKSPACE: offset = scanPC - 0x0e; break;
	 case SDLK_TAB:    offset = scanPC - 0x0f; break;
	 case SDLK_RETURN: offset = scanPC - 0x1c; break;
	 case SDLK_SPACE:  offset = scanPC - 0x39; break;
	 /*case SDLK_q:  offset = scanPC - 0x10; break;*/  /* different on azerty */
	 /*case SDLK_w:  offset = scanPC - 0x11; break;*/  /* different on azerty */
	 case SDLK_e:  offset = scanPC - 0x12; break;
	 case SDLK_r:  offset = scanPC - 0x13; break;
	 case SDLK_t:  offset = scanPC - 0x14; break;
	 /*case SDLK_y:  offset = scanPC - 0x15; break;*/  /* different on qwertz */
	 case SDLK_u:  offset = scanPC - 0x16; break;
	 case SDLK_i:  offset = scanPC - 0x17; break;
	 case SDLK_o:  offset = scanPC - 0x18; break;
	 case SDLK_p:  offset = scanPC - 0x19; break;
	 /*case SDLK_a:  offset = scanPC - 0x1e; break;*/  /* different on azerty */
	 case SDLK_s:  offset = scanPC - 0x1f; break;
	 case SDLK_d:  offset = scanPC - 0x20; break;
	 case SDLK_f:  offset = scanPC - 0x21; break;
	 case SDLK_g:  offset = scanPC - 0x22; break;
	 case SDLK_h:  offset = scanPC - 0x23; break;
	 case SDLK_j:  offset = scanPC - 0x24; break;
	 case SDLK_k:  offset = scanPC - 0x25; break;
	 case SDLK_l:  offset = scanPC - 0x26; break;
	 /*case SDLK_z:  offset = scanPC - 0x2c; break;*/  /* different on qwertz and azerty */
	 case SDLK_x:  offset = scanPC - 0x2d; break;
	 case SDLK_c:  offset = scanPC - 0x2e; break;
	 case SDLK_v:  offset = scanPC - 0x2f; break;
	 case SDLK_b:  offset = scanPC - 0x30; break;
	 case SDLK_n:  offset = scanPC - 0x31; break;
	 /*case SDLK_m:  offset = scanPC - 0x32; break;*/  /* different on azerty */
	 case SDLK_CAPSLOCK:  offset = scanPC - ST_CAPSLOCK; break;
	 case SDLK_LSHIFT: offset = scanPC - ST_LSHIFT; break;
	 case SDLK_LCTRL: offset = scanPC - ST_CTRL; break;
	 case SDLK_LALT: offset = scanPC - ST_ALTERNATE; break;
	 case SDLK_F1:  offset = scanPC - 0x3b; break;
	 case SDLK_F2:  offset = scanPC - 0x3c; break;
	 case SDLK_F3:  offset = scanPC - 0x3d; break;
	 case SDLK_F4:  offset = scanPC - 0x3e; break;
	 case SDLK_F5:  offset = scanPC - 0x3f; break;
	 case SDLK_F6:  offset = scanPC - 0x40; break;
	 case SDLK_F7:  offset = scanPC - 0x41; break;
	 case SDLK_F8:  offset = scanPC - 0x42; break;
	 case SDLK_F9:  offset = scanPC - 0x43; break;
	 case SDLK_F10: offset = scanPC - 0x44; break;
	 default:  break;
	}

	if (offset != -1)
	{
		Log_Printf(LOG_WARN, "Detected scancode offset = %d (key: '%s' with scancode $%02x)\n",
		        offset, SDL_GetKeyName(keysym->sym), scanPC);
	}

	return offset;
}


/**
 * Map PC scancode to ST scancode.
 * This code was heavily inspired by the emulator Aranym. (cheers!)
 */
static uint8_t Keymap_PcToStScanCode(const SDL_keysym* keysym)
{
	static int offset = -1;    /* uninitialized scancode offset */

	/* We sometimes enter here with an illegal (=0) scancode, so we keep
	 * track of the right scancodes in a table and then use a value from there.
	 */
	if (keysym->scancode != 0)
	{
		SdlSymToSdlScan[keysym->sym] = keysym->scancode;
	}
	else
	{
		keysym->scancode = SdlSymToSdlScan[keysym->sym];
		if (keysym->scancode == 0)
			Log_Printf(LOG_WARN, "Key scancode is 0!\n");
	}

	switch (keysym->sym)
	{
	 /* Numeric Pad */
	 /* note that the numbers are handled in Keymap_GetKeyPadScanCode()! */
	 case SDLK_KP_DIVIDE:   return 0x65;  /* Numpad / */
	 case SDLK_KP_MULTIPLY: return 0x66;  /* NumPad * */
	 case SDLK_KP_MINUS:    return 0x4a;  /* NumPad - */
	 case SDLK_KP_PLUS:     return 0x4e;  /* NumPad + */
	 case SDLK_KP_PERIOD:   return 0x71;  /* NumPad . */
	 case SDLK_KP_ENTER:    return 0x72;  /* NumPad Enter */

	 /* Special Keys */
	 case SDLK_PRINT:    	return 0x62;  /* Help */
	 case SDLK_SCROLLOCK: 	return 0x61; /* Undo */
	 case SDLK_PAGEUP:   	return 0x63;  /* Keypad ( */
	 case SDLK_PAGEDOWN: 	return 0x64;  /* Keypad ) */
	 case SDLK_HOME:     	return 0x47;  /* Home */
	 case SDLK_END:      	return 0x60;  /* End => "<>" on German Atari kbd */
	 case SDLK_UP:       	return 0x48;  /* Arrow Up */
	 case SDLK_LEFT:    	return 0x4b;  /* Arrow Left */
	 case SDLK_RIGHT:    	return 0x4d;  /* Arrow Right */
	 case SDLK_DOWN:     	return 0x50;  /* Arrow Down */
	 case SDLK_INSERT:   	return 0x52;  /* Insert */
	 case SDLK_DELETE:   	return 0x53;  /* Delete */
	 case SDLK_LESS:     	return 0x60;  /* "<" */

	 /* Map Right Alt/Alt Gr/Control to the Atari keys */
	 case SDLK_RCTRL:  	return ST_CTRL;  /* Control */
	 case SDLK_RALT:   	return ST_ALTERNATE;  /* Alternate */

	 default:
		/* Process remaining keys: assume that it's PC101 keyboard
		 * and that it is compatible with Atari ST keyboard (basically
		 * same scancodes but on different platforms with different
		 * base offset (framebuffer = 0, X11 = 8).
		 * Try to detect the offset using a little bit of black magic.
		 * If offset is known then simply pass the scancode. */
		if (offset == -1)
		{
			offset = Keymap_FindScanCodeOffset(keysym);
		}

		if (offset >= 0)
		{
			/* offset is defined so pass the scancode directly */
			return (keysym->scancode - offset);
		}
		else
		{
			/* Failed to detect offset, so use default value 8 */
			Log_Printf(LOG_WARN, "Offset detection failed with "
			        "key '%s', scancode = 0x%02x, symcode =  0x%02x\n",
			        SDL_GetKeyName(keysym->sym), keysym->scancode, keysym->sym);
			return (keysym->scancode - 8);
		}
		break;
	}
}

#endif	/* !WITH_SDL2 */


/**
 * Remap a keypad key to ST scan code. We use a separate function for this
 * so that we can easily toggle between number and cursor mode with the
 * numlock key.
 */
static char Keymap_GetKeyPadScanCode(const SDL_keysym* pKeySym)
{
	if (SDL_GetModState() & KMOD_NUM)
	{
		switch (pKeySym->sym)
		{
		 case SDLK_KP1:  return 0x6d;  /* NumPad 1 */
		 case SDLK_KP2:  return 0x6e;  /* NumPad 2 */
		 case SDLK_KP3:  return 0x6f;  /* NumPad 3 */
		 case SDLK_KP4:  return 0x6a;  /* NumPad 4 */
		 case SDLK_KP5:  return 0x6b;  /* NumPad 5 */
		 case SDLK_KP6:  return 0x6c;  /* NumPad 6 */
		 case SDLK_KP7:  return 0x67;  /* NumPad 7 */
		 case SDLK_KP8:  return 0x68;  /* NumPad 8 */
		 case SDLK_KP9:  return 0x69;  /* NumPad 9 */
		 default:  break;
		}
	}
	else
	{
		switch (pKeySym->sym)
		{
		 case SDLK_KP1:  return 0x6d;  /* NumPad 1 */
		 case SDLK_KP2:  return 0x50;  /* Cursor down */
		 case SDLK_KP3:  return 0x6f;  /* NumPad 3 */
		 case SDLK_KP4:  return 0x4b;  /* Cursor left */
		 case SDLK_KP5:  return 0x50;  /* Cursor down (again?) */
		 case SDLK_KP6:  return 0x4d;  /* Cursor right */
		 case SDLK_KP7:  return 0x52;  /* Insert - good for Dungeon Master */
		 case SDLK_KP8:  return 0x48;  /* Cursor up */
		 case SDLK_KP9:  return 0x47;  /* Home - again for Dungeon Master */
		 default:  break;
		}
	}

	return -1;
}


static int InputMatchesKeyMapping(const SDL_keysym* keySym, const struct KeyMapping *mapping)
{
	if (keySym->scancode == mapping->SdlKeysym.scancode)
		LOG_TRACE(TRACE_KEYMAP,"matching 0x%04x and 0x%04x\n", keySym->mod & mapping->modmask, mapping->SdlKeysym.mod);
	return keySym->scancode == mapping->SdlKeysym.scancode
		&& (keySym->mod & mapping->modmask) == mapping->SdlKeysym.mod;
}


/**
 * Remap SDL Key to ST Scan code
 * Receives the pressed key from SDL, and returns a matching key mapping.
 */
static struct KeyMapping* Keymap_RemapKeyToSTScanCodes(SDL_keysym* pKeySym, bool enableTrace)
{
	struct KeyMapping *keyDownMapping = &KeysDownMapping[pKeySym->scancode];

	/* Check for keypad first so we can handle numlock */
	if (ConfigureParams.Keyboard.nKeymapType != KEYMAP_LOADED)
	{
		if (pKeySym->sym >= SDLK_KP1 && pKeySym->sym <= SDLK_KP9)
		{
			keyDownMapping->STScanCodes[0] = Keymap_GetKeyPadScanCode(pKeySym);
			keyDownMapping->STScanCodesLength = 1;
			return keyDownMapping;
		}
	}

	/* Remap from PC scancodes? */
	if (ConfigureParams.Keyboard.nKeymapType == KEYMAP_SCANCODE)
	{
		keyDownMapping->STScanCodes[0] = Keymap_PcToStScanCode(pKeySym);
		keyDownMapping->STScanCodesLength = 1;
		return keyDownMapping;
	}

	/* Use loaded keymap? */
	if (ConfigureParams.Keyboard.nKeymapType == KEYMAP_LOADED)
	{
		int i,j;

		for (i = 0; i < KBD_MAX_SCANCODE; i++)
		{
			struct KeyMapping *mapping = &LoadedKeyMap[i];

			if (mapping->SdlKeysym.scancode == 0)
				break; /* End of table */

			if (!InputMatchesKeyMapping(pKeySym, mapping))
				continue;

			if (enableTrace)
			{
				/* This is pretty inefficient, that's why it's protected by a switch */
				LOG_TRACE(TRACE_KEYMAP,"  Mapping: ");
				for (j = 0; j < mapping->STScanCodesLength; j++)
					LOG_TRACE(TRACE_KEYMAP,"%02x ", mapping->STScanCodes[j]);
				LOG_TRACE(TRACE_KEYMAP,"(from keymap)\n");
			}

			*keyDownMapping = *mapping;
			return keyDownMapping;
		}
	}

	/* Fall back to symbolic mapping */
	keyDownMapping->STScanCodes[0] = Keymap_SymbolicToStScanCode(pKeySym);
	keyDownMapping->STScanCodesLength = 1;

	if (enableTrace)
		LOG_TRACE(TRACE_KEYMAP,"  Mapping: %02x (symbolic)\n",keyDownMapping->STScanCodes[0]);
	return keyDownMapping;
}


/*-----------------------------------------------------------------------*/
/**
 * Load keyboard remap file
 */
void Keymap_LoadRemapFile(const char *pszFileName)
{
	char mapLine[1024];
	int hostSpecIsOk, guestSpecIsOk;
	FILE *in;
	int idx = 0;
	int lineNumber = 0; /* For logging purposes            */
	char *token;
	char *saveptr;		/* For saving strtok_r's state, because host/guest analyses may also use strtok */
	char hostSpec[50]; 	/* Host (PC) keys specification    */
	char guestSpec[50];	/* Guest's (ST) keys specification */
	const char invalidSpecificationMessage[] = "Keymap_LoadRemapFile: '%s' not a valid specification at line %d\n";

	Log_Printf(LOG_DEBUG, "Keymap_LoadRemapFile: Loading '%s'\n", pszFileName);

	/* Initialize table with default values */
	memset(LoadedKeyMap, 0, sizeof(LoadedKeyMap));

	if (strlen(pszFileName) == 0)
		return;

	/* Attempt to load file */
	if (!File_Exists(pszFileName))
	{
		Log_Printf(LOG_DEBUG, "Keymap_LoadRemapFile: '%s' not a file\n", pszFileName);
		return;
	}
	in = fopen(pszFileName, "r");
	if (!in)
	{
		Log_Printf(LOG_ERROR, "Keymap_LoadRemapFile: failed to open keymap file '%s'\n", pszFileName);
		return;
	}

	while (!feof(in) && idx < KBD_MAX_SCANCODE)
	{
		/* Read line from file */
		if (fgets(mapLine, sizeof(mapLine), in) == NULL)
			break;

		++lineNumber;

		/* Remove white-space from start of line */
		Str_Trim(mapLine);

		/* Ignore empty line and comments */
		if (strlen(mapLine) == 0 || mapLine[0] == ';' || mapLine[0] == '#')
			continue;

		/* Cut out the values between host and guest parts */
		token = strtok_r(mapLine, ",", &saveptr);
		if (token == NULL)
			goto invalidSpecificationError;

		/* Get the host's key specification */
		strcpy(hostSpec,token);
		Str_Trim(hostSpec);
		if (strlen(hostSpec) == 0)
			goto invalidSpecificationError;
		hostSpecIsOk = HostSpecToSDLKeysym(hostSpec, &LoadedKeyMap[idx]);

		/* Get the guest (ST) specification */
		token = strtok_r(NULL, "\n", &saveptr);

		if (token == NULL)
			continue;
		strcpy(guestSpec,token);
		Str_Trim(guestSpec);
		if (strlen(guestSpec) == 0)
			goto invalidSpecificationError;
		guestSpecIsOk = GuestSpecToSTScanCodes(guestSpec, &LoadedKeyMap[idx]);

		/* Store into remap table, check both value within range */
		if (guestSpecIsOk && hostSpecIsOk)
		{
			LOG_TRACE(TRACE_KEYMAP,"keymap from file: host %s --> guest %s\n", hostSpec, guestSpec);
			idx += 1;
		}
		else
		{
			Log_Printf(LOG_WARN, "Could not parse keymap specification: %s\n", mapLine);
		}

		continue;

invalidSpecificationError:
			Log_Printf(LOG_ERROR, invalidSpecificationMessage, pszFileName, lineNumber);
	}

	fclose(in);
}

static int HostSpecToSDLKeysym(const char *spec, struct KeyMapping* result)
{
	/* Analyses the host (PC) specification from the table file and populate the keymapping */
	char buf[100];
	char *token;
	SDL_Scancode scancode;

	/* Prepare buffer */
	strcpy(buf, spec);

	/* Prepare for early returns */
	result->SdlKeysym.mod = 0;
	result->modmask = 0;

	/* Scancode part */
	token = strtok(buf,"|");
	if (token != NULL && (scancode = strtol(token, NULL, 16)))
		result->SdlKeysym.scancode = scancode;
	else
		return 0;

	/* Modifier part */
	token = strtok(NULL,"|");
	if (token != NULL)
	{
		/* We have a modifier specified */
		result->SdlKeysym.mod = SDLKeymodFromName(token);

		/* "modifier mask" part */
		token = strtok(NULL,"|");
		if (token != NULL)
			result->modmask = SDLKeymodFromName(token);
	}

	return -1; /* Success */
}


static int GuestSpecToSTScanCodes(const char *spec, struct KeyMapping* mapping)
{
	/* Analyses the guest (Atari ST keyboard) specification from the table file */
	char buf[100];
	char *start;
	char *separator;
	uint8_t *scancodes = mapping->STScanCodes; /* Alias for readability */
	uint8_t scancode;
	int i = 0;

	strcpy(buf, spec);
	separator = buf-1;
	do
	{
		start = separator+1;
		separator = strchr(start, '|');
		if (separator != NULL)
			*separator = '\0';

		if (strlen(start) <= 2)
		{
			scancode = strtol(start, NULL, 16);
		}
		else
		{
			/* Scancode may be expressed as LSHIFT,RSHIFT,ALTERNATE for user convenience */
			if (strcmp(start, "LSHIFT") == 0)
				scancode = ST_LSHIFT;
			else if (strcmp(start, "RSHIFT") == 0)
				scancode = ST_RSHIFT;
			else if (strcmp(start, "ALTERNATE") == 0)
				scancode = ST_ALTERNATE;
			else
			{
				Log_Printf(LOG_ERROR, "GuestSpecToSTScanCodes: Cannot understand scancode '%s'\n", start);
				i = 0; /* Error out */
				break;
			}
		}

		scancodes[i++] = scancode;
	} while (separator != NULL);

	mapping->STScanCodesLength = i;

	return i > 0;
}


/*-----------------------------------------------------------------------*/
/**
 * Scan list of keys to NOT de-bounce when running in maximum speed, eg ALT,SHIFT,CTRL etc...
 * @return true if key requires de-bouncing
 */
static bool Keymap_DebounceSTKey(char STScanCode)
{
	int i=0;

	/* Are we in fast forward, and have disabled key repeat? */
	if ((ConfigureParams.System.bFastForward == true)
	    && (ConfigureParams.Keyboard.bDisableKeyRepeat))
	{
		/* We should de-bounce all non extended keys,
		 * e.g. leave ALT, SHIFT, CTRL etc... held */
		while (DebounceExtendedKeys[i])
		{
			if (STScanCode == DebounceExtendedKeys[i])
				return false;
			i++;
		}

		/* De-bounce key */
		return true;
	}

	/* Do not de-bounce key */
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Debounce any PC key held down if running with key repeat disabled.
 * This is called each ST frame, so keys get held down for one VBL which
 * is enough for 68000 code to scan.
 */
void Keymap_DebounceAllKeys(void)
{
	uint8_t nScanCode;

	/* Return if we aren't in fast forward or have not disabled key repeat */
	if ((ConfigureParams.System.bFastForward == false)
	        || (!ConfigureParams.Keyboard.bDisableKeyRepeat))
	{
		return;
	}

	/* Now run through each key looking for ones held down */
	for (nScanCode = 1; nScanCode <= KBD_MAX_SCANCODE; nScanCode++)
	{
		/* Is key held? */
		if (Keyboard.KeyStates[nScanCode])
		{
			/* Does this require de-bouncing? */
			if (Keymap_DebounceSTKey(nScanCode))
			{
				IKBD_PressSTKey(nScanCode, false);
				Keyboard.KeyStates[nScanCode] = false;
			}
		}
	}

}


static bool IsKeyTranslatable(SDL_Keycode symkey)
{
	/* Ignore modifier keys that are not passed to the ST */
	switch (symkey)
	{
		case SDLK_RALT:
	 	case SDLK_LMETA:
	 	case SDLK_RMETA:
	 	case SDLK_MODE:
	 	case SDLK_NUMLOCK:
			return false;
	}
	return true;
}


/*-----------------------------------------------------------------------*/
/* Returns true if the scancode is for a key that allows to a different character
 * from the same key.
 */
static bool IsSTModifier(uint8_t scancode)
{
	switch (scancode)
	{
		case ST_LSHIFT:
		case ST_RSHIFT:
		case ST_ALTERNATE:
			return true;
	}
	return false;
}

/*-----------------------------------------------------------------------*/
/**
 * User pressed a key down
 */
void Keymap_KeyDown(SDL_keysym *sdlkey)
{
	struct KeyMapping* mapping;
	uint8_t* modifiers;
	int i;
	/* Convenience */
	int symkey = sdlkey->sym;
	int modkey = sdlkey->mod;

	LOG_TRACE(TRACE_KEYMAP, "Keymap_KeyDown: sym=%i scancode=0x%02x mod=0x%02x name='%s'\n",
	          symkey, sdlkey->scancode, modkey, Keymap_GetKeyName(symkey));

	if (ShortCut_CheckKeys(modkey, symkey, true))
		return;

	/* If using joystick emulation via keyboard, DON'T send keys to keyboard processor!!!
	 * Some games use keyboard as pause! */
	if (Joy_KeyDown(symkey, modkey))
		return;

	if (!IsKeyTranslatable(symkey))
		return;

	mapping = Keymap_RemapKeyToSTScanCodes(sdlkey, true);
	if (mapping == NULL)
		return;

	int modcount = 0;

	modifiers = mapping->PressedModifiers;
	for (i = 0; i < mapping->STScanCodesLength ; i++)
	{
		uint8_t scancode = mapping->STScanCodes[i];

		Keyboard.KeyStates[scancode]++;

		/* If it's a modifier that we're pressing, remember to release it later.
		 * In case you're wondering why we don't release the modifiers immediately after
		 * pressing the key: the reason is that if the user keeps the key down to make it
		 * repeat, the modifiers need to be there, otherwise it is the "unshifter/unalted"
		 * character that will repeat instead.
		 * TODO: Unfortunately this causes a bug as the modifiers will accumulate if you
		 * press multiple modified keys at once. For example, if on a French keyboard you
		 * press ALT-5 (to get a [), the ALTERNATE modifier will be retained. Holding that
		 * ALT down and pressing the key 6 at the same time (to get |), the | only requires
		 * SHIFT to be pressed. But ALTERNATE also being emulated, you will get ~.
		 * To maybe fix that we would have to manage a stack of modifiers, so we could
		 * release ALT while 7 is pressed (as it only requires SHIFT) then press it again
		 * when 7 is released. Is it really worth the effort... */
		if (IsSTModifier(scancode))
		{
			*modifiers++ = scancode;
			modcount++;
		}

		/* If that key was not already pressed, press it */
		if (Keyboard.KeyStates[scancode] == 1)
			IKBD_PressSTKey(scancode, true);
	}
	*modifiers = 0; /* End the list of modifiers */

	/*
	LOG_TRACE(TRACE_KEYMAP,"  %d modifiers to release: %02x %02x\n",modcount,mapping->PressedModifiers[0],mapping->PressedModifiers[1]);
	LOG_TRACE(TRACE_KEYMAP,"  LS:%d LR:%d CTRL:%d ALT:%d\n",Keyboard.KeyStates[ST_LSHIFT],Keyboard.KeyStates[ST_RSHIFT],Keyboard.KeyStates[ST_CTRL],Keyboard.KeyStates[ST_ALTERNATE]);
	*/
}


/*-----------------------------------------------------------------------*/
/**
 * User released a key
 */
void Keymap_KeyUp(SDL_keysym *sdlkey)
{
	struct KeyMapping *mapping;
	uint8_t *modifier;
	int i;
	int symkey = sdlkey->sym;
	int modkey = sdlkey->mod;

	 LOG_TRACE(TRACE_KEYMAP, "Keymap_KeyUp: sym=%i scancode=0x%02x mod=0x%02x name='%s'\n",
	          symkey, sdlkey->scancode, modkey, Keymap_GetKeyName(symkey));

	/* Ignore short-cut keys here */
	if (ShortCut_CheckKeys(modkey, symkey, false))
		return;

	/* If using keyboard emulation, DON'T send keys to keyboard processor!!!
	 * Some games use keyboard as pause! */
	if (Joy_KeyUp(symkey, modkey))
		return;

	/* Handle special keys */
	if (!IsKeyTranslatable(symkey))
	{
		LOG_TRACE(TRACE_KEYMAP, "   Key not translatable to ST keyboard.\n");
		return;
	}

	mapping = &KeysDownMapping[sdlkey->scancode];

	if (mapping == NULL)
	{
		Log_Printf(LOG_ERROR, "  No key mapping found !\n");
		return;
	}

	/* Release key (only if was pressed) */
	for (i = 0; i < mapping->STScanCodesLength ; i++)
	{
		uint8_t scancode = mapping->STScanCodes[i];

		if (IsSTModifier(scancode) && mapping->STScanCodesLength > 1)
			continue; /* We release emulated modifiers last */

		/* Set up */
		if (Keyboard.KeyStates[scancode])
		{
			LOG_TRACE(TRACE_KEYMAP,"  %02x was down, will be up'ed\n",scancode);

			Keyboard.KeyStates[scancode]--;
			IKBD_PressSTKey(scancode, false);
		}
		else
			LOG_TRACE(TRACE_KEYMAP,"  %02x will be kept down (presses: %d)\n",scancode,Keyboard.KeyStates[scancode]);

	}

	/* Release modifiers that were pressed to emulate the key*/
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

	/* Trace state of modifiers */
	LOG_TRACE(TRACE_KEYMAP,"  LS:%d LR:%d CTRL:%d ALT:%d\n",Keyboard.KeyStates[ST_LSHIFT],Keyboard.KeyStates[ST_RSHIFT],Keyboard.KeyStates[ST_CTRL],Keyboard.KeyStates[ST_ALTERNATE]);
}


/*-----------------------------------------------------------------------*/
/**
 * Simulate press or release of a key corresponding to given character
 */
void Keymap_SimulateCharacter(char asckey, bool press)
{
	SDL_keysym sdlkey;
	sdlkey.mod = KMOD_NONE;
	sdlkey.scancode = 0;

	if (isupper((unsigned char)asckey))
	{
		if (press)
		{
			sdlkey.sym = SDLK_LSHIFT;
			Keymap_KeyDown(&sdlkey);
		}
		sdlkey.sym = tolower((unsigned char)asckey);
		sdlkey.mod = KMOD_LSHIFT;
	}
	else
	{
		sdlkey.sym = asckey;
	}

	if (press)
	{
		Keymap_KeyDown(&sdlkey);
	}
	else
	{
		Keymap_KeyUp(&sdlkey);
		if (isupper((unsigned char)asckey))
		{
			sdlkey.sym = SDLK_LSHIFT;
			Keymap_KeyUp(&sdlkey);
		}
	}
}


#if WITH_SDL2

SDL_Keycode Keymap_GetKeyFromName(const char *name)
{
	return SDL_GetKeyFromName(name);
}

const char *Keymap_GetKeyName(SDL_Keycode keycode)
{
	if (!keycode)
		return "";

	return SDL_GetKeyName(keycode);
}

static SDL_Keymod SDLKeymodFromName(const char *name) {
	struct {
		SDL_Keymod mod;
		const char *name;
	} const keymodNames[] = {
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
		{ 0/*whatever*/, NULL }};

	int i;
	for (i = 0; keymodNames[i].name != NULL; i++)
	{
		if (strcmp(name, keymodNames[i].name) == 0)
			return keymodNames[i].mod;
	};

	LOG_TRACE(TRACE_KEYMAP, "SDLKeymodFromName: Didn't find SDL_Keymod \"%s\", defaulting to KMOD_NONE.\n", name);

	return KMOD_NONE;
}

#else	/* !WITH_SDL2 */

static struct {
	SDL_Keycode code;
	const char *name;
} const sdl_keytab[] = {
	{ SDLK_BACKSPACE, "Backspace" },
	{ SDLK_TAB, "Tab" },
	{ SDLK_CLEAR, "Clear" },
	{ SDLK_RETURN, "Return" },
	{ SDLK_PAUSE, "Pause" },
	{ SDLK_ESCAPE, "Escape" },
	{ SDLK_SPACE, "Space" },
	{ SDLK_EXCLAIM, "!" },
	{ SDLK_QUOTEDBL, "\"" },
	{ SDLK_HASH, "#" },
	{ SDLK_DOLLAR, "$" },
	{ SDLK_AMPERSAND, "&" },
	{ SDLK_QUOTE, "'" },
	{ SDLK_LEFTPAREN, "(" },
	{ SDLK_RIGHTPAREN, ")" },
	{ SDLK_ASTERISK, "*" },
	{ SDLK_PLUS, "+" },
	{ SDLK_COMMA, "," },
	{ SDLK_MINUS, "-" },
	{ SDLK_PERIOD, "." },
	{ SDLK_SLASH, "/" },
	{ SDLK_0, "0" },
	{ SDLK_1, "1" },
	{ SDLK_2, "2" },
	{ SDLK_3, "3" },
	{ SDLK_4, "4" },
	{ SDLK_5, "5" },
	{ SDLK_6, "6" },
	{ SDLK_7, "7" },
	{ SDLK_8, "8" },
	{ SDLK_9, "9" },
	{ SDLK_COLON, ":" },
	{ SDLK_SEMICOLON, ";" },
	{ SDLK_LESS, "<" },
	{ SDLK_EQUALS, "=" },
	{ SDLK_GREATER, ">" },
	{ SDLK_QUESTION, "?" },
	{ SDLK_AT, "@" },
	{ SDLK_LEFTBRACKET, "[" },
	{ SDLK_BACKSLASH, "\\" },
	{ SDLK_RIGHTBRACKET, "]" },
	{ SDLK_CARET, "^" },
	{ SDLK_UNDERSCORE, "_" },
	{ SDLK_BACKQUOTE, "`" },
	{ SDLK_a, "A" },
	{ SDLK_b, "B" },
	{ SDLK_c, "C" },
	{ SDLK_d, "D" },
	{ SDLK_e, "E" },
	{ SDLK_f, "F" },
	{ SDLK_g, "G" },
	{ SDLK_h, "H" },
	{ SDLK_i, "I" },
	{ SDLK_j, "J" },
	{ SDLK_k, "K" },
	{ SDLK_l, "L" },
	{ SDLK_m, "M" },
	{ SDLK_n, "N" },
	{ SDLK_o, "O" },
	{ SDLK_p, "P" },
	{ SDLK_q, "Q" },
	{ SDLK_r, "R" },
	{ SDLK_s, "S" },
	{ SDLK_t, "T" },
	{ SDLK_u, "U" },
	{ SDLK_v, "V" },
	{ SDLK_w, "W" },
	{ SDLK_x, "X" },
	{ SDLK_y, "Y" },
	{ SDLK_z, "Z" },
	{ SDLK_DELETE, "Delete" },
	{ SDLK_KP0, "Keypad 0" },
	{ SDLK_KP1, "Keypad 1" },
	{ SDLK_KP2, "Keypad 2" },
	{ SDLK_KP3, "Keypad 3" },
	{ SDLK_KP4, "Keypad 4" },
	{ SDLK_KP5, "Keypad 5" },
	{ SDLK_KP6, "Keypad 6" },
	{ SDLK_KP7, "Keypad 7" },
	{ SDLK_KP8, "Keypad 8" },
	{ SDLK_KP9, "Keypad 9" },
	{ SDLK_KP_PERIOD, "Keypad ." },
	{ SDLK_KP_DIVIDE, "Keypad /" },
	{ SDLK_KP_MULTIPLY, "Keypad *" },
	{ SDLK_KP_MINUS, "Keypad -" },
	{ SDLK_KP_PLUS, "Keypad +" },
	{ SDLK_KP_ENTER, "Keypad Enter" },
	{ SDLK_KP_EQUALS, "Keypad =" },
	{ SDLK_UP, "Up" },
	{ SDLK_DOWN, "Down" },
	{ SDLK_RIGHT, "Right" },
	{ SDLK_LEFT, "Left" },
	{ SDLK_INSERT, "Insert" },
	{ SDLK_HOME, "Home" },
	{ SDLK_END, "End" },
	{ SDLK_PAGEUP, "PageUp" },
	{ SDLK_PAGEDOWN, "PageDown" },
	{ SDLK_F1, "F1" },
	{ SDLK_F2, "F2" },
	{ SDLK_F3, "F3" },
	{ SDLK_F4, "F4" },
	{ SDLK_F5, "F5" },
	{ SDLK_F6, "F6" },
	{ SDLK_F7, "F7" },
	{ SDLK_F8, "F8" },
	{ SDLK_F9, "F9" },
	{ SDLK_F10, "F10" },
	{ SDLK_F11, "F11" },
	{ SDLK_F12, "F12" },
	{ SDLK_F13, "F13" },
	{ SDLK_F14, "F14" },
	{ SDLK_F15, "F15" },
	{ SDLK_NUMLOCK, "Numlock" },
	{ SDLK_CAPSLOCK, "CapsLock" },
	{ SDLK_SCROLLOCK, "ScrollLock" },
	{ SDLK_RSHIFT, "Right Shift" },
	{ SDLK_LSHIFT, "Left Shift" },
	{ SDLK_RCTRL, "Right Ctrl" },
	{ SDLK_LCTRL, "Left Ctrl" },
	{ SDLK_RALT, "Right Alt" },
	{ SDLK_LALT, "Left Alt" },
	{ SDLK_RMETA, "Right GUI" },
	{ SDLK_LMETA, "Left GUI" },
	{ SDLK_MODE, "ModeSwitch" },
	{ SDLK_HELP, "Help" },
	{ SDLK_PRINT, "PrintScreen" },
	{ SDLK_SYSREQ, "SysReq" },
	{ SDLK_BREAK, "Cancel" },
	{ SDLK_MENU, "Menu" },
	{ SDLK_POWER, "Power" },
	{ SDLK_UNDO, "Undo" },

	{ SDLK_WORLD_1, "¡" },	/* 161 */
	{ SDLK_WORLD_2, "¢" },	/* 162 */
	{ SDLK_WORLD_3, "£" },	/* 163 */
	{ SDLK_WORLD_4, "¤" },	/* 164 */
	{ SDLK_WORLD_5, "¥" },	/* 165 */
	{ SDLK_WORLD_6, "¦" },	/* 166 */
	{ SDLK_WORLD_7, "§" },	/* 167 */
	{ SDLK_WORLD_8, "¨" },	/* 168 */
	{ SDLK_WORLD_9, "©" },	/* 169 */
	{ SDLK_WORLD_10, "ª" },	/* 170 */
	{ SDLK_WORLD_11, "«" },	/* 171 */
	{ SDLK_WORLD_12, "¬" },	/* 172 */
	{ SDLK_WORLD_14, "®" },	/* 174 */
	{ SDLK_WORLD_15, "¯" },	/* 175 */
	{ SDLK_WORLD_16, "°" },	/* 176 */
	{ SDLK_WORLD_17, "±" },	/* 177 */
	{ SDLK_WORLD_18, "²" },	/* 178 */
	{ SDLK_WORLD_19, "³" },	/* 179 */
	{ SDLK_WORLD_20, "´" },	/* 180 */
	{ SDLK_WORLD_21, "µ" },	/* 181 */
	{ SDLK_WORLD_22, "¶" },	/* 182 */
	{ SDLK_WORLD_23, "·" },	/* 183 */
	{ SDLK_WORLD_24, "¸" },	/* 184 */
	{ SDLK_WORLD_25, "¹" },	/* 185 */
	{ SDLK_WORLD_26, "º" },	/* 186 */
	{ SDLK_WORLD_27, "»" },	/* 187 */
	{ SDLK_WORLD_28, "¼" },	/* 188 */
	{ SDLK_WORLD_29, "½" },	/* 189 */
	{ SDLK_WORLD_30, "¾" },	/* 190 */
	{ SDLK_WORLD_31, "¿" },	/* 191 */
	{ SDLK_WORLD_32, "À" },	/* 192 */
	{ SDLK_WORLD_33, "Á" },	/* 193 */
	{ SDLK_WORLD_34, "Â" },	/* 194 */
	{ SDLK_WORLD_35, "Ã" },	/* 195 */
	{ SDLK_WORLD_36, "Ä" },	/* 196 */
	{ SDLK_WORLD_37, "Å" },	/* 197 */
	{ SDLK_WORLD_38, "Æ" },	/* 198 */
	{ SDLK_WORLD_39, "Ç" },	/* 199 */
	{ SDLK_WORLD_40, "È" },	/* 200 */
	{ SDLK_WORLD_41, "É" },	/* 201 */
	{ SDLK_WORLD_42, "Ê" },	/* 202 */
	{ SDLK_WORLD_43, "Ë" },	/* 203 */
	{ SDLK_WORLD_44, "Ì" },	/* 204 */
	{ SDLK_WORLD_45, "Í" },	/* 205 */
	{ SDLK_WORLD_46, "Î" },	/* 206 */
	{ SDLK_WORLD_47, "Ï" },	/* 207 */
	{ SDLK_WORLD_48, "Ð" },	/* 208 */
	{ SDLK_WORLD_49, "Ñ" },	/* 209 */
	{ SDLK_WORLD_50, "Ò" },	/* 210 */
	{ SDLK_WORLD_51, "Ó" },	/* 211 */
	{ SDLK_WORLD_52, "Ô" },	/* 212 */
	{ SDLK_WORLD_53, "Õ" },	/* 213 */
	{ SDLK_WORLD_54, "Ö" },	/* 214 */
	{ SDLK_WORLD_55, "×" },	/* 215 */
	{ SDLK_WORLD_56, "Ø" },	/* 216 */
	{ SDLK_WORLD_57, "Ù" },	/* 217 */
	{ SDLK_WORLD_58, "Ú" },	/* 218 */
	{ SDLK_WORLD_59, "Û" },	/* 219 */
	{ SDLK_WORLD_60, "Ü" },	/* 220 */
	{ SDLK_WORLD_61, "Ý" },	/* 221 */
	{ SDLK_WORLD_62, "Þ" },	/* 222 */
	{ SDLK_WORLD_63, "ß" },	/* 223 */
	{ SDLK_WORLD_64, "à" },	/* 224 */
	{ SDLK_WORLD_65, "á" },	/* 225 */
	{ SDLK_WORLD_66, "â" },	/* 226 */
	{ SDLK_WORLD_67, "ã" },	/* 227 */
	{ SDLK_WORLD_68, "ä" },	/* 228 */
	{ SDLK_WORLD_69, "å" },	/* 229 */
	{ SDLK_WORLD_70, "æ" },	/* 230 */
	{ SDLK_WORLD_71, "ç" },	/* 231 */
	{ SDLK_WORLD_72, "è" },	/* 232 */
	{ SDLK_WORLD_73, "é" },	/* 233 */
	{ SDLK_WORLD_74, "ê" },	/* 234 */
	{ SDLK_WORLD_75, "ë" },	/* 235 */
	{ SDLK_WORLD_76, "ì" },	/* 236 */
	{ SDLK_WORLD_77, "í" },	/* 237 */
	{ SDLK_WORLD_78, "î" },	/* 238 */
	{ SDLK_WORLD_79, "ï" },	/* 239 */
	{ SDLK_WORLD_80, "ð" },	/* 240 */
	{ SDLK_WORLD_81, "ñ" },	/* 241 */
	{ SDLK_WORLD_82, "ò" },	/* 242 */
	{ SDLK_WORLD_83, "ó" },	/* 243 */
	{ SDLK_WORLD_84, "ô" },	/* 244 */
	{ SDLK_WORLD_85, "õ" },	/* 245 */
	{ SDLK_WORLD_86, "ö" },	/* 246 */
	{ SDLK_WORLD_87, "÷" },	/* 247 */
	{ SDLK_WORLD_88, "ø" },	/* 248 */
	{ SDLK_WORLD_89, "ù" },	/* 249 */
	{ SDLK_WORLD_90, "ú" },	/* 250 */
	{ SDLK_WORLD_91, "û" },	/* 251 */
	{ SDLK_WORLD_92, "ü" },	/* 252 */
	{ SDLK_WORLD_93, "ý" },	/* 253 */
	{ SDLK_WORLD_94, "þ" },	/* 254 */
	{ SDLK_WORLD_95, "ÿ" },	/* 255 */

	{ -1, NULL }
};

SDL_Keycode Keymap_GetKeyFromName(const char *name)
{
	int i;

	if (!name[0])
		return 0;

	for (i = 0; sdl_keytab[i].name != NULL; i++)
	{
		if (strcasecmp(name, sdl_keytab[i].name) == 0)
			return sdl_keytab[i].code;
	}

	return 0;
}

const char *Keymap_GetKeyName(SDL_Keycode keycode)
{
	int i;

	if (keycode == 0)
		return "";

	for (i = 0; sdl_keytab[i].name != NULL; i++)
	{
		if (keycode == sdl_keytab[i].code)
			return sdl_keytab[i].name;
	}

	return "";
}

#endif /* !WITH_SDL2 */
