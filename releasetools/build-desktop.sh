#!/usr/bin/env bash
#
# build-desktop.sh — build the LXQt/Qt6 desktop stack and install it into a
# MINIX DESTDIR, so `build.sh sets` (or the amd64_cdimage.sh overlay) can put it
# on an image.  This is an OPT-IN stage: a normal `build.sh release` does not run
# it, because the reachover world does not build Qt/glib/LXQt at all — they are
# out-of-tree CMake/meson/autotools packages fetched from upstream.
#
# The per-package rationale (patches, flags, why each define is there) lives in
# external/{glib,dbus,qt6,kf6,lxqt,xdg-user-dirs}/README.md.  This script is the
# executable form of those recipes; keep the two in sync.
#
# Sources are pinned in releasetools/desktop-sources.manifest (version + URL +
# sha256).  Fill the sha256 fields once with:  build-desktop.sh checksums
#
# Usage:
#   build-desktop.sh [build]        install the desktop into DESTDIR (default)
#   build-desktop.sh checksums      fetch each source, print sha256 lines
#   build-desktop.sh list           print the ordered component list
#
# Environment (with defaults):
#   DESTDIR       required — the MINIX sysroot to install into
#   TOOLDIR       required — the cross toolchain (…/tooldir.<host>)
#   QT_HOST_PATH  /usr      — host Qt of the SAME version as qtbase (moc/rcc/uic)
#   DESKTOP_WORK  ${DESTDIR%/*}/desktop-build   — download + build scratch area
#   JOBS          number of parallel jobs (default: nproc)
#
set -euo pipefail

SRCROOT=$(cd "$(dirname "$0")/.." && pwd)
MANIFEST="$SRCROOT/releasetools/desktop-sources.manifest"
EXT="$SRCROOT/external"
QT6DIR="$EXT/qt6"
TOOLCHAIN_FILE="$QT6DIR/minix-toolchain.cmake"
SHIM="$QT6DIR/cmake/MinixWaylandShim.cmake"

: "${QT_HOST_PATH:=/usr}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"

die() { echo "build-desktop: ERROR: $*" >&2; exit 1; }
log() { echo "==> $*" >&2; }

# The exact dependency order.  Each name has a build_<name> function below and a
# matching manifest record.  Native-only helpers (ECM, lxqt-build-tools) install
# host CMake modules into the sysroot; everything else is cross-compiled.
COMPONENTS="
pcre2 glib dbus qtbase qtsvg
extra-cmake-modules kwindowsystem layer-shell-qt
lxqt-build-tools libqtxdg liblxqt lxqt-globalkeys lxqt-menu-data qtxdg-tools
lxqt-panel lxqt-session qtermwidget qterminal xdg-user-dirs
"

# ---------------------------------------------------------------------------
# Manifest + fetch helpers
# ---------------------------------------------------------------------------
manifest_field() { # $1=id $2=col(2=version,3=url,4=sha256)
	awk -v id="$1" -v c="$2" '!/^#/ && $1==id {print $c; exit}' "$MANIFEST"
}

fetch() { # $1=id -> echoes path to the downloaded tarball
	local id="$1" url ver sha out
	url=$(manifest_field "$id" 3);  ver=$(manifest_field "$id" 2)
	sha=$(manifest_field "$id" 4)
	[ -n "$url" ] || die "no manifest entry for '$id'"
	out="$DL/$(basename "$url")"
	if [ ! -f "$out" ]; then
		log "fetch $id $ver"
		curl -fL --retry 3 -o "$out.tmp" "$url" || die "download failed: $url"
		mv "$out.tmp" "$out"
	fi
	if [ "$sha" != "TOFILL" ]; then
		echo "$sha  $out" | sha256sum -c - >/dev/null 2>&1 \
			|| die "sha256 mismatch for $id ($out) — refusing to build"
	elif [ "${ALLOW_TOFILL:-0}" != 1 ]; then
		die "sha256 for '$id' is TOFILL in the manifest — run 'build-desktop.sh checksums' and fill it in"
	fi
	echo "$out"
}

extract() { # $1=id -> echoes path to the extracted source tree (upstream dir name kept)
	local id="$1" tarball top dir
	tarball=$(fetch "$id")
	# The tarball's own top-level directory name is preserved, because the
	# per-package patches embed it (glib's are -p0 with a glib-2.80.5/ prefix,
	# etc.); renaming it would break patch application.
	top=$(tar -tf "$tarball" | head -1 | cut -d/ -f1)
	dir="$WORK/$top"
	if [ ! -d "$dir" ]; then
		log "extract $id -> $top"
		tar -xf "$tarball" -C "$WORK"
	fi
	echo "$dir"
}

# Apply a patch into the current directory, trying -p1 then -p0 (the desktop
# patches vary: qt6 is -p1, glib and lxqt 01-05 are -p0, lxqt 06-07 -p1).  Skips
# cleanly if already applied.  $1 = patch file.
apply_patch() {
	local p="$1" lvl
	for lvl in 1 0; do
		if patch -p"$lvl" --dry-run -N -r - < "$p" >/dev/null 2>&1; then
			patch -p"$lvl" -N -r - < "$p" >/dev/null; return 0
		fi
	done
	# already applied (reverse-applies clean) -> fine; otherwise a real failure
	patch -p1 -R --dry-run <"$p" >/dev/null 2>&1 || patch -p0 -R --dry-run <"$p" >/dev/null 2>&1 \
		|| die "patch does not apply: $p"
}

# Snapshot DESTDIR/usr file list, run a component build, then record everything
# it added into $INSTALL_LIST (set in cmd_build; used by amd64_cdimage.sh to
# overlay the desktop onto the ISO).
INSTALL_LIST=""
record_install() { # $1=id ; runs build_<id> between two snapshots
	local id="$1" before after
	before=$(mktemp); after=$(mktemp)
	( cd "$DESTDIR" && find usr etc var -type f -o -type l 2>/dev/null ) | sort > "$before" || true
	"build_$id"
	( cd "$DESTDIR" && find usr etc var -type f -o -type l 2>/dev/null ) | sort > "$after" || true
	comm -13 "$before" "$after" >> "$INSTALL_LIST"
	rm -f "$before" "$after"
}

# Common cross-CMake configure for the LXQt/KF6 consumers.  Callers add the
# component-specific -D flags as extra args.
cross_cmake() { # $1=srcdir ; $2..=extra -D flags
	local src="$1"; shift
	cmake "$src" -GNinja \
		-DCMAKE_TOOLCHAIN_FILE="$DESTDIR/usr/lib/cmake/Qt6/qt.toolchain.cmake" \
		-DQT_HOST_PATH="$QT_HOST_PATH" \
		-DMINIX_NO_STAGING=1 \
		-DCMAKE_INSTALL_PREFIX=/usr \
		-DCMAKE_PREFIX_PATH="$DESTDIR/usr;$DESTDIR/usr/share/ECM" \
		-DCMAKE_PROJECT_INCLUDE="$SHIM" \
		-DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF \
		"$@"
}

CXXDEF="-D__minix=3 -D__minix__=3 -D__ELF__=1 -D_NETBSD_SOURCE"

# ---------------------------------------------------------------------------
# Component builds — see external/<pkg>/README.md for the reasoning behind each.
# ---------------------------------------------------------------------------

build_pcre2() {
	# glib needs pcre2 in the sysroot; PIC so anything downstream can go in a .so.
	# TODO(verify): pcre2 has no external/pcre2 README — version 10.44 and this
	# flag set are inferred.  Confirm the version glib 2.80.5 was built against.
	local s; s=$(extract pcre2)
	rm -rf "$WORK/pcre2-b"; mkdir -p "$WORK/pcre2-b"
	( cd "$WORK/pcre2-b" && cross_cmake "$s" \
		-DCMAKE_POSITION_INDEPENDENT_CODE=ON \
		-DPCRE2_BUILD_TESTS=OFF -DPCRE2_BUILD_PCRE2GREP=OFF \
		&& ninja -j"$JOBS" && DESTDIR="$DESTDIR" ninja install )
}

build_glib() {
	# meson; see external/glib/README.md.  Patch is -p0 from external/glib.
	local s; s=$(extract glib)
	( cd "$s" && apply_patch "$EXT/glib/patches/01-glib-minix.patch" )
	export PKG_CONFIG_LIBDIR="$DESTDIR/usr/lib/pkgconfig"
	export PKG_CONFIG_SYSROOT_DIR="$DESTDIR"
	rm -rf "$WORK/glib-b"
	meson setup "$WORK/glib-b" "$s" --cross-file "$EXT/glib/minix-cross.ini" \
		--default-library=static \
		-Dtests=false -Dintrospection=disabled -Dman-pages=disabled \
		-Dnls=disabled -Dlibmount=disabled -Dselinux=disabled -Dxattr=false
	ninja -C "$WORK/glib-b" -j"$JOBS"
	DESTDIR="$DESTDIR" ninja -C "$WORK/glib-b" install
}

build_dbus() {
	# Two builds: (1) the daemon + shared libdbus-1.so, (2) a static libdbus-1.a
	# for linking into Qt (-dbus-linked).  See external/dbus/README.md.
	local s; s=$(extract dbus)
	local common="-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE -DMINIX_NO_STAGING=1
		-DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_SYSCONFDIR=/etc
		-DCMAKE_INSTALL_LOCALSTATEDIR=/var -DCMAKE_BUILD_TYPE=Release
		-DCMAKE_C_STANDARD_LIBRARIES=-lpthread\ -lexecinfo
		-DCMAKE_C_FLAGS=$CXXDEF\ -DHAVE_UNPCBID=1
		-DDBUS_BUILD_TESTS=OFF -DDBUS_ENABLE_XML_DOCS=OFF
		-DDBUS_ENABLE_DOXYGEN_DOCS=OFF -DDBUS_WITH_GLIB=OFF
		-DENABLE_SYSTEMD=OFF -DDBUS_SESSION_SOCKET_DIR=/tmp"
	rm -rf "$WORK/dbus-b"; mkdir -p "$WORK/dbus-b"
	( cd "$WORK/dbus-b" && cmake "$s" -GNinja $common \
		&& ninja -j"$JOBS" && DESTDIR="$DESTDIR" ninja install )
	# static archive
	( cd "$s" && apply_patch "$EXT/dbus/patches/01-dbus-static-lib.patch" )
	rm -rf "$WORK/dbus-static-b"; mkdir -p "$WORK/dbus-static-b"
	( cd "$WORK/dbus-static-b" && cmake "$s" -GNinja $common -DBUILD_SHARED_LIBS=OFF \
		&& ninja -j"$JOBS" dbus-1 && cp lib/libdbus-1.a "$DESTDIR/usr/lib/" )
}

build_qtbase() {
	# The big one; see external/qt6/README.md.  Static, dbus-linked, and
	# concurrent ON (lxqt-panel's wlroots backend needs it — the README notes
	# the base recipe had it off).
	local s; s=$(extract qtbase)
	( cd "$s" && for p in "$QT6DIR"/patches/*.patch; do apply_patch "$p"; done
	  cp -r "$QT6DIR/mkspecs/minix-clang" mkspecs/ )
	rm -rf "$WORK/qtbase-b"
	cmake -S "$s" -B "$WORK/qtbase-b" -GNinja \
		-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
		-DQT_HOST_PATH="$QT_HOST_PATH" \
		-DQT_QMAKE_TARGET_MKSPEC=minix-clang \
		-DCMAKE_INSTALL_PREFIX="$DESTDIR/usr" \
		-DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
		-DINPUT_opengl=no -DINPUT_egl=no -DINPUT_xcb=no -DINPUT_dbus=no \
		-DINPUT_glib=no -DINPUT_icu=no -DINPUT_fontconfig=no \
		-DINPUT_harfbuzz=qt -DINPUT_pcre=qt -DINPUT_libpng=qt -DINPUT_libjpeg=qt \
		-DINPUT_zlib=system -DINPUT_freetype=system \
		-DFEATURE_libudev=OFF -DFEATURE_evdev=OFF -DFEATURE_libinput=OFF \
		-DFEATURE_sql=OFF -DFEATURE_testlib=OFF -DFEATURE_network=OFF \
		-DFEATURE_printsupport=OFF \
		-DFEATURE_dbus_linked=ON -DDBus1_LIBRARY="$DESTDIR/usr/lib/libdbus-1.a" \
		-DFEATURE_concurrent=ON \
		-DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
	ninja -C "$WORK/qtbase-b" -j"$JOBS"
	ninja -C "$WORK/qtbase-b" install
}

build_qtsvg() {
	# Qt6Svg module (libqtxdg needs it).  Same cross toolchain as qtbase.
	# TODO(verify): flag set inferred — a Qt submodule usually just needs the
	# toolchain file + host path + install prefix.
	local s; s=$(extract qtsvg)
	rm -rf "$WORK/qtsvg-b"
	cmake -S "$s" -B "$WORK/qtsvg-b" -GNinja \
		-DCMAKE_TOOLCHAIN_FILE="$DESTDIR/usr/lib/cmake/Qt6/qt.toolchain.cmake" \
		-DQT_HOST_PATH="$QT_HOST_PATH" \
		-DCMAKE_INSTALL_PREFIX="$DESTDIR/usr" \
		-DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
		-DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
	ninja -C "$WORK/qtsvg-b" -j"$JOBS"; ninja -C "$WORK/qtsvg-b" install
}

build_extra-cmake-modules() {
	# Native — ships only CMake modules.  See external/kf6/README.md.
	local s; s=$(extract extra-cmake-modules)
	rm -rf "$WORK/ecm-b"; mkdir -p "$WORK/ecm-b"
	( cd "$WORK/ecm-b" && cmake "$s" -GNinja -DCMAKE_INSTALL_PREFIX=/usr \
		-DBUILD_TESTING=OFF -DBUILD_HTML_DOCS=OFF -DBUILD_MAN_DOCS=OFF \
		&& DESTDIR="$DESTDIR" ninja install )
}

build_kwindowsystem() {
	local s; s=$(extract kwindowsystem)
	rm -rf "$WORK/kws-b"; mkdir -p "$WORK/kws-b"
	( cd "$WORK/kws-b" && cross_cmake "$s" \
		-DKF_IGNORE_PLATFORM_CHECK=TRUE \
		-DKWINDOWSYSTEM_X11=OFF -DKWINDOWSYSTEM_WAYLAND=OFF -DKWINDOWSYSTEM_QML=OFF \
		&& ninja -j"$JOBS" && DESTDIR="$DESTDIR" ninja install )
}

build_layer-shell-qt() {
	local s; s=$(extract layer-shell-qt)
	( cd "$s" && for p in "$EXT/kf6"/patches/*.patch; do apply_patch "$p"; done )
	rm -rf "$WORK/lsq-b"; mkdir -p "$WORK/lsq-b"
	( cd "$WORK/lsq-b" && cross_cmake "$s" -DKF_IGNORE_PLATFORM_CHECK=TRUE \
		&& ninja -j"$JOBS" && DESTDIR="$DESTDIR" ninja install )
}

build_lxqt-build-tools() {
	# Native — CMake modules only.  See external/lxqt/README.md.
	local s; s=$(extract lxqt-build-tools)
	rm -rf "$WORK/lbt-b"; mkdir -p "$WORK/lbt-b"
	( cd "$WORK/lbt-b" && cmake "$s" -GNinja -DCMAKE_INSTALL_PREFIX=/usr \
		-DLXQT_ETC_XDG_DIR=/etc/xdg \
		&& DESTDIR="$DESTDIR" ninja install )
}

# libqtxdg / liblxqt and the other LXQt consumers share this shape.
lxqt_consumer() { # $1=id ; $2..=extra -D flags
	local id="$1"; shift
	local s; s=$(extract "$id")
	( cd "$s" && for p in "$EXT/lxqt"/patches/*"$id"-minix.patch; do
		[ -f "$p" ] && apply_patch "$p"; done )
	rm -rf "$WORK/$id-b"; mkdir -p "$WORK/$id-b"
	( cd "$WORK/$id-b" && cross_cmake "$s" \
		-DLXQT_ETC_XDG_DIR=/etc/xdg -DBUILD_DEV_UTILS=OFF \
		"$@" && ninja -j"$JOBS" && DESTDIR="$DESTDIR" ninja install )
}

build_libqtxdg() {
	lxqt_consumer libqtxdg \
		-DMINIX_EXTRA_STANDARD_LIBRARIES="-lgio-2.0 -lgobject-2.0 -lgmodule-2.0 -lglib-2.0 -lintl -lpcre2-8 -lz" \
		-DQTXDGX_ICONENGINEPLUGIN_INSTALL_PATH=/usr/lib/qt6/plugins/iconengines
}
build_liblxqt() {
	lxqt_consumer liblxqt \
		-DBUILD_BACKLIGHT_LINUX_BACKEND=OFF \
		-DCMAKE_CXX_FLAGS="$CXXDEF -DLXQT_NO_KWINDOWSYSTEM"
}
# TODO(verify): the READMEs do not give explicit flag sets for these four; they
# are plain LXQt/Qt6 consumers, so the shared lxqt_consumer shape is used.
# Confirm against your working build (esp. any -D…=OFF the panel/session need).
build_lxqt-globalkeys() { lxqt_consumer lxqt-globalkeys -DCMAKE_CXX_FLAGS="$CXXDEF"; }
build_lxqt-menu-data()  { lxqt_consumer lxqt-menu-data  -DCMAKE_CXX_FLAGS="$CXXDEF"; }
build_qtxdg-tools()     { lxqt_consumer qtxdg-tools     -DCMAKE_CXX_FLAGS="$CXXDEF"; }
build_lxqt-panel()      { lxqt_consumer lxqt-panel      -DCMAKE_CXX_FLAGS="$CXXDEF"; }
build_lxqt-session()    { lxqt_consumer lxqt-session    -DCMAKE_CXX_FLAGS="$CXXDEF"; }

build_qtermwidget() {
	local s; s=$(extract qtermwidget)
	( cd "$s" && apply_patch "$EXT/lxqt/patches/06-qtermwidget-minix.patch" )
	rm -rf "$WORK/qtermwidget-b"; mkdir -p "$WORK/qtermwidget-b"
	( cd "$WORK/qtermwidget-b" && cross_cmake "$s" \
		-DMINIX_EXTRA_STANDARD_LIBRARIES="-lutil -lexecinfo -lelf" \
		-DCMAKE_CXX_FLAGS="$CXXDEF" \
		-DQTERMWIDGET_USE_UTEMPTER=OFF -DUSE_UTF8PROC=OFF \
		&& ninja -j"$JOBS" && DESTDIR="$DESTDIR" ninja install )
}
build_qterminal() {
	local s; s=$(extract qterminal)
	( cd "$s" && apply_patch "$EXT/lxqt/patches/07-qterminal-minix.patch" )
	rm -rf "$WORK/qterminal-b"; mkdir -p "$WORK/qterminal-b"
	( cd "$WORK/qterminal-b" && cross_cmake "$s" \
		-DCMAKE_PREFIX_PATH="$DESTDIR/usr" \
		-DMINIX_EXTRA_STANDARD_LIBRARIES="-lutil -lexecinfo -lelf" \
		-DCMAKE_CXX_FLAGS="$CXXDEF" \
		&& ninja -j"$JOBS" && DESTDIR="$DESTDIR" ninja install )
}

build_xdg-user-dirs() {
	# autotools; see external/xdg-user-dirs/README.md.
	local s; s=$(extract xdg-user-dirs)
	local T="$TOOLDIR/bin"
	( cd "$s" && \
	  CC="$T/x86_64-elf64-minix-clang" \
	  CFLAGS="-O2 $CXXDEF --sysroot=$DESTDIR" \
	  ./configure --host=x86_64-elf64-minix --build=x86_64-linux-gnu \
		--prefix=/usr --disable-nls --disable-documentation && \
	  make -j"$JOBS" LIBS="-lintl" && \
	  make DESTDIR="$DESTDIR" LIBS="-lintl" install )
}

# ---------------------------------------------------------------------------
# Prereqs + subcommands
# ---------------------------------------------------------------------------
check_prereqs() {
	local miss=0 t
	for t in curl tar sha256sum cmake ninja meson patch pkg-config; do
		command -v "$t" >/dev/null || { echo "missing host tool: $t" >&2; miss=1; }
	done
	# host Qt of the exact qtbase version, for moc/rcc/uic via QT_HOST_PATH.
	local want; want=$(manifest_field qtbase 2)
	local moc; moc=$(command -v moc-qt6 || command -v moc || echo "$QT_HOST_PATH/lib/qt6/libexec/moc")
	if [ -x "$moc" ]; then
		"$moc" --version 2>/dev/null | grep -q "$want" \
			|| echo "WARNING: host moc is not Qt $want (QT_HOST_PATH=$QT_HOST_PATH) — cross build needs a version-exact host Qt" >&2
	else
		echo "missing host Qt $want (moc/rcc/uic) — set QT_HOST_PATH to a Qt $want install" >&2; miss=1
	fi
	[ "$miss" = 0 ] || die "install the missing host prerequisites and retry"
}

cmd_checksums() {
	ALLOW_TOFILL=1
	local id p
	for id in $COMPONENTS; do
		p=$(fetch "$id")
		printf '%-20s %s\n' "$id" "$(sha256sum "$p" | cut -d' ' -f1)"
	done
	echo "# paste the sha256 column into releasetools/desktop-sources.manifest" >&2
}

cmd_list() { for c in $COMPONENTS; do echo "$c $(manifest_field "$c" 2)"; done; }

cmd_build() {
	[ -n "${DESTDIR:-}" ] || die "DESTDIR is required"
	[ -n "${TOOLDIR:-}" ] || die "TOOLDIR is required"
	[ -f "$TOOLCHAIN_FILE" ] || die "missing $TOOLCHAIN_FILE (external/qt6 present?)"
	check_prereqs
	INSTALL_LIST="$WORK/install.list"; : > "$INSTALL_LIST"
	local c
	for c in $COMPONENTS; do
		log "build component: $c"
		record_install "$c"
	done
	sort -u "$INSTALL_LIST" -o "$INSTALL_LIST"
	log "done — $(wc -l < "$INSTALL_LIST") files installed into $DESTDIR"
	log "install list: $INSTALL_LIST"
}

main() {
	: "${DESKTOP_WORK:=${DESTDIR%/*}/desktop-build}"
	WORK="$DESKTOP_WORK"; DL="$WORK/downloads"
	mkdir -p "$WORK" "$DL"
	case "${1:-build}" in
		build)     cmd_build ;;
		checksums) cmd_checksums ;;
		list)      cmd_list ;;
		*) die "unknown command '${1:-}'; use: build | checksums | list" ;;
	esac
}
main "$@"
