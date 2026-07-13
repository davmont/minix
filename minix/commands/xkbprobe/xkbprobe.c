/*	xkbprobe - exercise libxkbcommon the way a Wayland client does
 *
 * On Wayland the compositor sends a keymap over wl_keyboard.keymap as XKB text
 * (in an fd), and every client compiles it with xkb_keymap_new_from_string()
 * and then tracks modifiers with xkb_state as key events arrive.  Nothing in
 * that path touches the xkeyboard-config data files, which is why MINIX can
 * ship libxkbcommon plus one precompiled keymap (/usr/share/xkb/us.xkb) and
 * leave xkeyboard-config alone.
 *
 * So that is what this probe does: compile the shipped keymap from a string,
 * translate keycodes to keysyms, and check that pressing shift changes the
 * answer.  Keycodes are evdev + 8, the offset XKB uses (X11 legacy).
 *
 * Exits 0 only if every check passes.
 */

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <xkbcommon/xkbcommon.h>

#define KEYMAP_PATH	"/usr/share/xkb/us.xkb"

/* evdev keycodes + 8 (the XKB offset). */
#define KEY_A_XKB	(30 + 8)
#define KEY_1_XKB	(2 + 8)
#define KEY_LSHIFT_XKB	(42 + 8)

static int failures;

static void
check(const char *what, int ok, const char *detail)
{
	if (ok) {
		printf("%-46s -> OK\n", what);
	} else {
		printf("%-46s -> FAIL (%s)\n", what, detail);
		failures++;
	}
	fflush(stdout);
}

static char *
slurp(const char *path)
{
	struct stat st;
	char *buf;
	FILE *f;

	if (stat(path, &st) != 0)
		return NULL;
	if ((f = fopen(path, "r")) == NULL)
		return NULL;
	if ((buf = malloc(st.st_size + 1)) == NULL) {
		fclose(f);
		return NULL;
	}
	if (fread(buf, 1, st.st_size, f) != (size_t)st.st_size) {
		free(buf);
		fclose(f);
		return NULL;
	}
	buf[st.st_size] = '\0';
	fclose(f);
	return buf;
}

/* Report the one keysym a key produces in the current state. */
static xkb_keysym_t
sym_of(struct xkb_state *state, xkb_keycode_t kc)
{
	return xkb_state_key_get_one_sym(state, kc);
}

int
main(void)
{
	struct xkb_context *ctx;
	struct xkb_keymap *keymap;
	struct xkb_state *state;
	char *text;
	char *dumped;
	char utf8[32];

	printf("xkbprobe: libxkbcommon keymap + state\n\n");

	ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	check("xkb_context_new", ctx != NULL, "returned NULL");
	if (ctx == NULL)
		return 1;

	text = slurp(KEYMAP_PATH);
	check("read " KEYMAP_PATH, text != NULL, strerror(errno));
	if (text == NULL)
		return 1;

	/*
	 * This is the call every Wayland client makes on wl_keyboard.keymap.
	 * If the vendored xkbcomp (bison parser, scanner, symbol/type/compat
	 * handling) is broken in any way, it shows up right here.
	 */
	keymap = xkb_keymap_new_from_string(ctx, text,
	    XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	check("xkb_keymap_new_from_string (wl_keyboard.keymap)",
	    keymap != NULL, "keymap failed to compile");
	if (keymap == NULL)
		return 1;

	state = xkb_state_new(keymap);
	check("xkb_state_new", state != NULL, "returned NULL");
	if (state == NULL)
		return 1;

	/* Unshifted: the 'A' key produces the keysym 'a'. */
	check("keycode 30 (evdev A) -> XKB_KEY_a",
	    sym_of(state, KEY_A_XKB) == XKB_KEY_a, "wrong keysym");

	memset(utf8, 0, sizeof(utf8));
	xkb_state_key_get_utf8(state, KEY_A_XKB, utf8, sizeof(utf8));
	check("  and UTF-8 \"a\"", strcmp(utf8, "a") == 0, utf8);

	/* Now hold shift and ask again: modifier state must change the answer. */
	xkb_state_update_key(state, KEY_LSHIFT_XKB, XKB_KEY_DOWN);

	check("shift is reported active",
	    xkb_state_mod_name_is_active(state, XKB_MOD_NAME_SHIFT,
	    XKB_STATE_MODS_EFFECTIVE) > 0, "shift not active");

	check("with shift: A -> XKB_KEY_A",
	    sym_of(state, KEY_A_XKB) == XKB_KEY_A, "shift did not apply");

	memset(utf8, 0, sizeof(utf8));
	xkb_state_key_get_utf8(state, KEY_A_XKB, utf8, sizeof(utf8));
	check("  and UTF-8 \"A\"", strcmp(utf8, "A") == 0, utf8);

	/* Shift+1 is '!' on a US layout -- a different level, not just case. */
	check("with shift: 1 -> XKB_KEY_exclam",
	    sym_of(state, KEY_1_XKB) == XKB_KEY_exclam, "wrong shifted level");

	xkb_state_update_key(state, KEY_LSHIFT_XKB, XKB_KEY_UP);
	check("shift released: A -> XKB_KEY_a again",
	    sym_of(state, KEY_A_XKB) == XKB_KEY_a, "state not restored");

	/* A compositor dumps a keymap back to text to send it on. */
	dumped = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
	check("xkb_keymap_get_as_string (what a compositor sends)",
	    dumped != NULL && strstr(dumped, "xkb_keymap") != NULL,
	    "dump failed");

	/* The dump must itself compile: that is the round trip a client sees. */
	if (dumped != NULL) {
		struct xkb_keymap *again;

		again = xkb_keymap_new_from_string(ctx, dumped,
		    XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
		check("re-compiling the dump round-trips", again != NULL,
		    "dumped keymap does not compile");
		if (again != NULL)
			xkb_keymap_unref(again);
		free(dumped);
	}

	xkb_state_unref(state);
	xkb_keymap_unref(keymap);
	xkb_context_unref(ctx);
	free(text);

	printf("\nxkbprobe: %s\n", failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
	return failures == 0 ? 0 : 1;
}
