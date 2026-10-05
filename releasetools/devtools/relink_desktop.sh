#!/bin/sh
# Relink the desktop's 10 static programs against the libc.a in $LIBCDIR
# and install them into $DEST (a destdir).  The desktop's own libraries
# still come from build.amd64's destdir (the link commands' --sysroot);
# -L$LIBCDIR comes first on the command line, so the implicit -lc (and
# libminc-free libc pieces) resolve to $LIBCDIR's copies.
set -e
LIBCDIR=${LIBCDIR:?}
DEST=${DEST:?}
OBJ=${OBJ:-$(cd "$(dirname "$0")/../../.." && pwd)/build.amd64}
W=$OBJ/desktop-build
LOGS=${LOGS:-/tmp/minix-devtools}; mkdir -p "$LOGS"
cd "$W"

relink_ninja() {
	bd=$1 tgt=$2 dest=$3
	cmd=$(ninja -C "$bd" -t commands "$tgt" | tail -1)
	cmd=${cmd#: && }
	cmd=${cmd% && :}
	cmd=$(printf "%s" "$cmd" | sed "s|^\([^ ]*\) |\1 -L$LIBCDIR |")
	(cd "$bd" && sh -c "$cmd") > "$LOGS/relinkab_$(basename "$tgt").log" 2>&1
	install -m 755 "$bd/$tgt" "$DEST/$dest"
	echo "ok $dest"
}

relink_ninja lxqt-about-b lxqt-about usr/bin/lxqt-about
relink_ninja lxqt-config-b src/lxqt-config usr/bin/lxqt-config
relink_ninja lxqt-config-b lxqt-config-file-associations/lxqt-config-file-associations usr/bin/lxqt-config-file-associations
relink_ninja lxqt-config-b lxqt-config-locale/lxqt-config-locale usr/bin/lxqt-config-locale
relink_ninja lxqt-session-b lxqt-leave/lxqt-leave usr/bin/lxqt-leave
relink_ninja lxqt-session-b lxqt-session/lxqt-session usr/bin/lxqt-session
relink_ninja lxqt-panel-b panel/lxqt-panel usr/bin/lxqt-panel
relink_ninja qterminal-b qterminal usr/bin/qterminal
relink_ninja qtxdg-tools-b src/mat/qtxdg-mat usr/bin/qtxdg-mat

cd xdg-user-dirs-0.18
$OBJ/tooldir.Linux-x86_64/bin/x86_64-elf64-minix-clang \
    -O2 -D__minix=3 -D__minix__=3 -D__ELF__=1 -D_NETBSD_SOURCE \
    --sysroot=$OBJ/destdir.amd64 -static -fuse-ld=lld \
    -L"$LIBCDIR" -L$OBJ/destdir.amd64/usr/lib \
    -Wl,-z,noseparate-code -Wl,-z,norelro -Wl,--no-rosegment \
    -o xdg-user-dirs-update xdg-user-dirs-update.o -lintl
install -m 755 xdg-user-dirs-update "$DEST/usr/bin/xdg-user-dirs-update"
echo "ok usr/bin/xdg-user-dirs-update"
