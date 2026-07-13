#	MINIX: the keymap a compositor hands to its clients.
#
# xkeyboard-config is not vendored, so xkb_keymap_new_from_names() cannot
# compile a keymap on the target.  It does not need to: on Wayland the
# compositor sends a ready-made keymap over wl_keyboard.keymap and every client
# compiles it with xkb_keymap_new_from_string().  This is that keymap --
# rules "evdev", model "pc105", layout "us", in XKB_KEYMAP_FORMAT_TEXT_V1,
# compiled from xkeyboard-config with the vendored xkbcomp.

FILES=		us.xkb
FILESDIR=	/usr/share/xkb

.include <bsd.files.mk>
.include <bsd.prog.mk>
