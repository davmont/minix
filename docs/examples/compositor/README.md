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
  COMMIT (damaged sub-rect), SET_SURFACE (replacement shmid after a
  resize), DESTROY. Server→client: CONFIGURE (new position and current
  size — a changed size asks the client to reallocate its surface),
  MOUSE (window-relative pointer + buttons), CLOSED, KEY (focused-window
  key: HID code + ASCII char + modifiers + press/release).
- **Compositor** (`fbcompd.c`): `select()`s over the listen socket, all
  client fds, `/dev/mousemux`, and `/dev/kbdmux`. Each event accumulates a
  **damage box** (the committed sub-rect, the old+new cursor, a moved
  window's swept area, ...) and recomposites *only* that box: the desktop
  fill, per-window title bar + FreeType text + shm surface, and the cursor
  are each clipped to it, and windows that do not touch it are skipped
  entirely. `libfbgui` then presents just those rows. So a client redraw or
  a cursor move no longer repaints - or blits - the whole multi-megabyte
  screen (measured: an average frame writes ~1.4% of a full-screen blit).
  Left-button-down on a title bar raises and drags that window; dragging
  the bottom-right grip resizes it; clicks inside a window forward MOUSE
  events to its client.
- **Client helper** (`fbclient.c/.h`): `fbc_connect`, `fbc_create_window`
  (shmget+shmat+CREATE_WINDOW → a pixel buffer to draw into),
  `fbc_commit`, `fbc_resize` (reallocate the surface on a resize
  CONFIGURE), `fbc_poll`.

## Validated

Under QEMU + OVMF (1280×800×32), with `/service/fb` up: `run.sh` starts
`fbcompd` and then `client_hello` and `client_clock` as **separate
processes**. Both windows appear on the framebuffer (7% non-background
content — the two windows' title bars and text), and injecting mouse
motion + a button drag over the compositor's title bar relocates one
client's window (≈3.8% of the screen changes between before/after
screendumps) while the other stays put. Cross-process shm attach
reports `SHM-OK`.

## Installed components

The compositor and its client library are now part of the system:

- **`/usr/bin/fbcompd`** — the compositor program (`minix/commands/fbcompd`).
- **`libfbclient`** — the client library (`minix/lib/libfbclient`),
  installed as `/usr/lib/libfbclient.*` with `<fbclient.h>` and
  `<fbcomp_proto.h>` in `/usr/include`.

Applications link `-lfbclient` and talk to `/usr/bin/fbcompd`. On a
UEFI-booted MINIX, bring up the framebuffer and run the compositor:

```
minix-service up /service/fb -dev /dev/fb0
/usr/bin/fbcompd &
```

`fbcompd` renders window titles with
`/usr/share/fonts/TTF/DejaVuSansMono.ttf` (installed by the
`external/bsd/dejavu` font package) by default; pass a different font
as `argv[1]` or via `$FBCOMPD_FONT`.  If no font is found it still
runs, just with titleless windows.

## The example clients

`client_hello.c` and `client_clock.c` here are tiny apps written
against the installed `libfbclient`. Cross-compile them and run against
the installed compositor:

```
CC=<objdir>/ext-tc/bin/x86_64-elf64-minix-clang
D=<destdir.amd64>
$CC -O1 -static -D__minix=3 -I$D/usr/include -o client_hello \
    client_hello.c $D/usr/lib/libfbclient.a
$CC -O1 -static -D__minix=3 -I$D/usr/include -o client_clock \
    client_clock.c $D/usr/lib/libfbclient.a
```

`run.sh` starts `/usr/bin/fbcompd` and the three clients as separate
processes.

## Window management

- **Click-to-raise / focus**: clicking any window raises it to the top
  and gives it focus (the topmost non-minimised window); the focused
  window's title bar is drawn brighter, and keystrokes route to it.
- **Title-bar boxes**: at the right of each title bar are three boxes —
  minimise (`−`), maximise (`□`) and close (`x`).
- **Close box**: clicking `x` sends `FBC_CLOSED` to the client and removes
  the window.
- **Maximise / restore**: clicking `□` toggles the window between its
  size and full-screen. Like an interactive resize it asks the client to
  reallocate (`FBC_CONFIGURE` → `fbc_resize` → `FBC_SET_SURFACE`); the
  pre-maximise geometry is saved and restored on the next click.
- **Minimise**: clicking `−` hides the window and adds a button for it to
  a **taskbar** along the bottom of the screen; clicking that button
  restores and raises the window. A minimised window keeps running (its
  buffer stays fresh) but is not composited and takes no input.
- **Tear-free commits**: the compositor composites from its own copy of
  each surface, refreshed only when the client `FBC_COMMIT`s — a client
  redrawing mid-frame cannot tear.
- **Robust to client crashes**: each client's messages are reassembled from
  a per-connection buffer (a stream socket may split or coalesce them), so a
  partial read never drops a client; when a client exits or crashes the
  compositor removes its window and `IPC_RMID`s the surface segment so SysV
  shm does not leak.
- **Drag**: left-drag on a title bar moves a window.
- **Resize**: each window has a small grip at its bottom-right corner;
  left-drag it to resize. The compositor sends the target size in an
  `FBC_CONFIGURE`; the client reallocates its shm surface via
  `fbc_resize()`, repaints, and replies with `FBC_SET_SURFACE` carrying
  the new shmid — the compositor adopts it (classic request/ack, no
  pixels on the socket). Both example clients repaint at the new size.
  A fast drag emits a burst of `FBC_CONFIGURE`s; the clients **coalesce**
  them by draining all pending events and reallocating only to the last
  size in each batch (the same way X11/Wayland toolkits collapse a
  configure burst). This keeps the surface from churning — one
  reallocation per redraw instead of one per pointer step — so the drag
  stays smooth and no stale surface hand-off can race. The compositor
  also tolerates a stale hand-off gracefully: if a superseded shmid can
  no longer be attached it logs and keeps the current surface.
- **Keyboard**: the compositor opens `/dev/kbdmux`. MINIX's input server
  delivers key events to whoever has that device open and forwards to TTY
  only when nobody does (`input.c`), so opening it cleanly **grabs** the
  keyboard from the console; closing it (on exit) hands it back — no
  kernel/TTY/input-server changes needed. The compositor decodes the raw
  USB-HID `struct input_event`s, tracks Shift/Ctrl/Alt, maps to ASCII with
  the US layout, and routes an `FBC_KEY` (code + char + modifiers +
  press/release) to the **focused** (topmost) window. Focus follows the
  same click-to-raise model, so clicking a window redirects the keyboard
  to it. `client_keys.c` demonstrates text entry. The serial console keeps
  working while the keyboard is grabbed, since serial input does not flow
  through the input server.

## Auto-start at boot

An `rc.d` script (`etc/rc.d/fbcompd`) can bring the compositor up at boot.
It is **opt-in**: the default `fbcompd=NO` (`etc/defaults/minix.rc.conf`)
keeps it off, because enabling it starts the fb driver and grabs the
keyboard, making the framebuffer the primary console. To enable it, add

    fbcompd=YES

to `/etc/rc.conf` and reboot (or run `/etc/rc.d/fbcompd start`). The script
brings up the fb driver (`minix-service up /service/fb -dev /dev/fb0`) and
launches `/usr/bin/fbcompd`; `fbcompd_flags` can pass a font path. The
serial console keeps working, so a headless/serial setup stays reachable.

## What a usable WM still needs (roadmap step 4 remainder)

- A resource/font story (base ships no TTF — a font package is needed).
- Auto-start currently launches only the bare compositor; a real session
  would also start a launcher/clients.
