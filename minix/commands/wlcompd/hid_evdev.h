/*	hid_evdev.h - USB HID usage codes to Linux evdev keycodes.
 *
 * This table is not an optional nicety; without it every keystroke lands on the
 * wrong key.  MINIX's input server delivers raw USB HID usages (see
 * <minix/input.h>, INPUT_KEY_A == 0x04).  Wayland's wl_keyboard.key carries an
 * *evdev* keycode, because that is what the XKB world is built on: a client adds
 * the customary 8 and looks the result up in the keymap the compositor sent it,
 * and the keymap we ship is compiled from xkeyboard-config's "evdev" rules.
 *
 * So the compositor has to translate.  Zero means "no evdev equivalent"; such a
 * key is dropped rather than sent as keycode 0, which a client would try to look
 * up and get nonsense from.
 */

#ifndef WLCOMPD_HID_EVDEV_H
#define WLCOMPD_HID_EVDEV_H

#include <stdint.h>

/* Indexed by HID usage (Keyboard/Keypad page, 0x00..0x67). */
static const uint16_t hid_to_evdev[0x68] = {
	/* 0x00-0x03: reserved / errors */
	0, 0, 0, 0,
	/* 0x04-0x1D: a..z, in HID's alphabetical order -- evdev's is the
	 * physical QWERTY order, which is exactly why this cannot be a
	 * simple offset. */
	30,	/* a */	48,	/* b */	46,	/* c */	32,	/* d */
	18,	/* e */	33,	/* f */	34,	/* g */	35,	/* h */
	23,	/* i */	36,	/* j */	37,	/* k */	38,	/* l */
	50,	/* m */	49,	/* n */	24,	/* o */	25,	/* p */
	16,	/* q */	19,	/* r */	31,	/* s */	20,	/* t */
	22,	/* u */	47,	/* v */	17,	/* w */	45,	/* x */
	21,	/* y */	44,	/* z */
	/* 0x1E-0x27: 1..9, 0 */
	2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
	/* 0x28-0x2C: enter, esc, backspace, tab, space */
	28, 1, 14, 15, 57,
	/* 0x2D-0x38: - = [ ] \ europe1 ; ' ` , . / */
	12, 13, 26, 27, 43, 43, 39, 40, 41, 51, 52, 53,
	/* 0x39: caps lock */
	58,
	/* 0x3A-0x45: F1..F12 */
	59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 87, 88,
	/* 0x46-0x48: print screen, scroll lock, pause */
	99, 70, 119,
	/* 0x49-0x4E: insert, home, page up, delete, end, page down */
	110, 102, 104, 111, 107, 109,
	/* 0x4F-0x52: right, left, down, up */
	106, 105, 108, 103,
	/* 0x53: num lock */
	69,
	/* 0x54-0x58: keypad / * - + enter */
	98, 55, 74, 78, 96,
	/* 0x59-0x62: keypad 1..9, 0 */
	79, 80, 81, 75, 76, 77, 71, 72, 73, 82,
	/* 0x63: keypad . */
	83,
	/* 0x64: europe 2 */
	86,
	/* 0x65: application / menu */
	127,
	/* 0x66: power */
	116,
	/* 0x67: keypad = */
	117,
};

/* Modifiers live at 0xE0..0xE7, well past the table above. */
static inline uint16_t
hid_key_to_evdev(uint16_t hid)
{
	switch (hid) {
	case 0xE0: return 29;		/* left ctrl */
	case 0xE1: return 42;		/* left shift */
	case 0xE2: return 56;		/* left alt */
	case 0xE3: return 125;		/* left meta */
	case 0xE4: return 97;		/* right ctrl */
	case 0xE5: return 54;		/* right shift */
	case 0xE6: return 100;		/* right alt */
	case 0xE7: return 126;		/* right meta */
	default:
		if (hid < sizeof(hid_to_evdev) / sizeof(hid_to_evdev[0]))
			return hid_to_evdev[hid];
		return 0;
	}
}

#endif /* WLCOMPD_HID_EVDEV_H */
