# fbcompd — a minimal multi-process compositor

This proof of concept turns the single-program framebuffer demo
(`docs/examples/microgui`) into a real **multi-process windowing
system**: a compositor process owns `/dev/fb0`, and independent client
processes each submit a window. It is the display-roadmap step-4c
proof that MINIX can run a Wayland-style compositor without X.

## Architecture

```
  client_hello ─┐   shm segment (pixels)      ┌─ owns /dev/fb0 (libfbgui)
  client_clock ─┤◄──────────────────────────►│   damage-rectangle present
                │   AF_UNIX socket (protocol) │   composites all windows
                └──────────────► fbcompd ◄─────┘   routes mouse input
```

- **Surfaces via SysV shared memory (MIT-SHM model).** A client calls
  `shmget(IPC_PRIVATE, ...)`, `shmat`s the segment, draws its pixels
  (x8r8g8b8), and sends the `shmid` — a plain integer — to the
  compositor in a `FBC_CREATE_WINDOW` message. The compositor
  `shmat`s the *same* segment read-only and composites it with pixman.
  No fd passing needed; `shmid` is shareable across processes (verified
  by `shmtest.c` → `SHM-OK`). MINIX ships the `ipc` server, so this
  works out of the box.
- **Protocol over an AF_UNIX socket** (`/tmp/fbcomp.sock`): fixed-size
  `struct fbc_msg` (`fbcomp_proto.h`). Client→server: CREATE_WINDOW,
  COMMIT (damaged sub-rect), SET_TITLE, DESTROY. Server→client:
  CONFIGURE (new position), MOUSE (window-relative pointer + buttons).
- **Compositor** (`fbcompd.c`): `select()`s over the listen socket, all
  client fds, and `/dev/mousemux`. Each frame it composites the desktop,
  every window (title bar + title text via FreeType, then the client's
  shm surface), and the cursor into the `libfbgui` back buffer, and
  presents with damage tracking so only changed rows hit the
  framebuffer. Left-button-down on a title bar raises and drags that
  window; clicks inside a window forward MOUSE events to its client.
- **Client helper** (`fbclient.c/.h`): `fbc_connect`, `fbc_create_window`
  (shmget+shmat+CREATE_WINDOW → a pixel buffer to draw into),
  `fbc_commit`, `fbc_poll`.

## Validated

Under QEMU + OVMF (1280×800×32), with `/service/fb` up: `run.sh` starts
`fbcompd` and then `client_hello` and `client_clock` as **separate
processes**. Both windows appear on the framebuffer (7% non-background
content — the two windows' title bars and text), and injecting mouse
motion + a button drag over the compositor's title bar relocates one
client's window (≈3.8% of the screen changes between before/after
screendumps) while the other stays put. Cross-process shm attach
reports `SHM-OK`.

## Building / running

Not wired into the system build (a PoC). Cross-compile each program
statically against the in-tree libraries:

```
CC=<objdir>/ext-tc/bin/x86_64-elf64-minix-clang
D=<destdir.amd64>
$CC -O1 -static -D__minix=3 -D_MINIX_SYSTEM=1 -I$D/usr/include/pixman-1 \
    -I$D/usr/include/freetype2 -I<minix>/minix/lib/libfbgui \
    -o fbcompd fbcompd.c $D/usr/lib/libfbgui.a $D/usr/lib/libfreetype.a \
    $D/usr/lib/libpixman-1.a -lm
# clients link fbclient.c (no fbgui/pixman needed if they plot pixels directly)
$CC -O1 -static -D__minix=3 -o client_hello client_hello.c fbclient.c
$CC -O1 -static -D__minix=3 -o client_clock client_clock.c fbclient.c ...
```

Ship the binaries + a `.ttf` font to a UEFI-booted MINIX, bring up
`/service/fb`, and run `run.sh`.

## What a usable WM still needs (roadmap step 4 remainder)

- Promote `fbcompd` to a real system service and `fbclient` to an
  installed `libfbclient` (this is examples-only for now).
- Keyboard input + focus routing (the text console still owns the
  keyboard).
- Window resize, close/minimise controls, z-order UI, proper decoration.
- Redraw throttling / frame callbacks (clients currently commit freely).
- Double-buffered surfaces to avoid tearing while a client redraws.
- A resource/font story (base ships no TTF — a font package is needed).
