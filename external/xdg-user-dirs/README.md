# xdg-user-dirs on MINIX

`xdg-user-dirs` 0.18, cross-compiled for MINIX/amd64.  A small autotools program
that creates the XDG user directories (`~/Desktop`, `~/Downloads`, ...) at first
login and writes `~/.config/user-dirs.dirs`.  `lxqt-session` requires it at
configure time (`find_package(XdgUserDirs)`), and `startlxqt` sources the file it
writes.

Not vendored -- upstream needs no patching, only the right link flag.

## Build

    curl -LO https://user-dirs.freedesktop.org/releases/xdg-user-dirs-0.18.tar.gz
    tar xf xdg-user-dirs-0.18.tar.gz && cd xdg-user-dirs-0.18

    D=<destdir.amd64>
    T=<tooldir>/bin
    CC=$T/x86_64-elf64-minix-clang \
    CFLAGS="-O2 -D__minix=3 -D__minix__=3 -D__ELF__=1 -D_NETBSD_SOURCE --sysroot=$D" \
    ./configure --host=x86_64-elf64-minix --build=x86_64-linux-gnu \
        --prefix=/usr --disable-nls --disable-documentation
    make LIBS="-lintl"
    make DESTDIR=$D LIBS="-lintl" install

## The one thing that is not obvious

`--disable-nls` does not remove the `gettext`/`bindtextdomain` calls from the
source, and MINIX keeps those in a separate `libintl` rather than in libc.  So the
link needs `LIBS="-lintl"`, or it fails with a pile of undefined `gettext`
references.
