# microgui — framebuffer micro-GUI proof of concept

`microgui.c` demonstrates that MINIX amd64 has a complete graphics
userland **without X**: it composites an interactive desktop directly
on `/dev/fb0` using the libraries vendored in the 2026-07 display
roadmap work.

## What it shows

- **Output**: the UEFI GOP linear framebuffer via `/dev/fb0`
  (`FBIOGET_VSCREENINFO` / `FBIOGET_FIXSCREENINFO`, then a per-frame blit
  respecting `line_length`).
- **Compositing**: [pixman](../../../external/mit/pixman) — an offscreen
  `x8r8g8b8` scene is built back-to-front (desktop, window rectangles,
  text, cursor) each frame.
- **Text**: [FreeType](../../../external/mit/freetype) — window titles
  and body lines are rendered with `FT_LOAD_RENDER` (anti-aliased) and
  composited through a pixman `a8` mask.
- **Input**: `/dev/mousemux` (`struct input_event`, non-blocking,
  `select()`ed with a frame timer). Left-button-down over a title bar
  raises and grabs the window; motion drags it; button-up drops it.

## Validated

Under QEMU q35 + OVMF (KVM), on the UEFI/GPT image (`minix_amd64_gpt.img`
— the framebuffer only exists under UEFI), with the fb service up
(`minix-service up /service/fb -dev /dev/fb0`): the scene renders (two
windows with anti-aliased titles, an arrow cursor), and QEMU-injected
mouse motion + button events drive a real title-bar drag — verified by
comparing screendumps before and after (the window visibly moves).

## Building

Not wired into the system build (it is an example and needs a TTF font,
which MINIX base does not ship). Cross-compile against the in-tree
libraries, e.g.:

```
CC=<objdir>/ext-tc/bin/x86_64-elf64-minix-clang
$CC -O1 -static -I<destdir>/usr/include/pixman-1 \
    -I<destdir>/usr/include/freetype2 \
    -o microgui microgui.c \
    <destdir>/usr/lib/libfreetype.a <destdir>/usr/lib/libpixman-1.a -lm
```

Ship it and a `.ttf` (e.g. DejaVu Sans Mono) to a UEFI-booted MINIX and
run it after bringing up `/service/fb`.

## Next steps (roadmap step 4)

A usable stack still needs: double-buffering / damage-rectangle redraw
(instead of full-frame blits), a compositor *process* that owns
`/dev/fb0` with a client protocol (the slot where a Wayland-ish layer
would fit), a widget/event toolkit, keyboard focus + text input (the
console owns the keyboard today), and a font package. See
`docs/THIRD_PARTY.md`.
