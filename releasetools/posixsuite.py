#!/usr/bin/env python3
"""Build the Open POSIX Test Suite (external/gpl2/open_posix_testsuite) for
MINIX amd64 on the build host, and pack it onto a disk image for
releasetools/qemutest.py --suite posix --drive IMAGE.

Every conformance test is cross-compiled against a destdir with the tooldir's
clang and lld, dynamically linked.  A test that does not compile is recorded,
not fatal: it is usually the interface under test that is missing, and the
suite then reports it as "nobuild".

  releasetools/posixsuite.py --destdir ../obj/destdir.amd64 \\
      --tooldir ../obj/tooldir.Linux-x86_64 --out posix.img

The image is a ustar archive (the guest unpacks it with tar xf /dev/c0d1)
holding posix/MANIFEST, one line per test: "ID KIND STATUS", where ID is the
test's path under conformance/ without its suffix, KIND is run, sh or
buildonly, and STATUS is ok or nobuild.  The compiler errors of the tests
that did not build go to OUT.log.
"""
import argparse
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(TOP, "external/gpl2/open_posix_testsuite/dist")

# What upstream's include/mk/config.mk.in compiles with.
CFLAGS = ["-std=c99", "-D_POSIX_C_SOURCE=200809L", "-D_XOPEN_SOURCE=700",
          "-w"]
# What share/mk/bsd.own.mk adds for every amd64 program: the cross clang
# (target x86_64-elf64-minix) predefines no OS macros, and auto-vectorized
# stores trip KVM's instruction emulator.
TARGET_FLAGS = ["-D__minix=3", "-D__minix__=3", "-D__ELF__=1",
                "-fno-slp-vectorize", "-fno-vectorize"]
# And share/mk/bsd.sys.mk's link flags: MINIX's exec takes a text and a data
# segment, which lld gives only when told, at GNU ld's base address.
LINK_FLAGS = ["-Wl,-z,noexecstack", "-Wl,-z,norelro",
              "-Wl,-z,noseparate-code", "-Wl,--no-rosegment",
              "-Wl,--image-base=0x400000"]

RUN_RE = re.compile(r"^\d+-\d+\.c$")
BUILDONLY_RE = re.compile(r"^\d+-buildonly\.c$")
SH_RE = re.compile(r"^\d+-\d+\.sh$")

# Run by qemutest in the guest, from the top of the unpacked suite: run one
# test and print its result.  t0 kills a test that outlives POSIX_TIMEOUT.
RUN1 = r"""#!/bin/sh
# run1.sh ID: run one Open POSIX test; print "POSIXTS rc=N" or
# "POSIXTS nobuild".
top=$(pwd)
# The CD's /tmp is 128 KB; the tests take TMPDIR (most of them).
mkdir -p "$top/tmp" && chmod 1777 "$top/tmp"
TMPDIR=$top/tmp; export TMPDIR
set -- $(grep "^$1 " MANIFEST)
if [ $# -ne 3 ]; then echo "POSIXTS unknown"; exit 0; fi
if [ "$3" != ok ]; then echo "POSIXTS nobuild"; exit 0; fi
if [ "$2" = buildonly ]; then echo "POSIXTS rc=0"; exit 0; fi
cd "$top/$(dirname "$1")" || exit 1
t=./$(basename "$1")
if [ "$2" = sh ]; then t="sh $t.sh"; fi
$top/t0 ${POSIX_TIMEOUT:-60} $t < /dev/null
echo "POSIXTS rc=$?"
"""


def find_tests():
    """(id, kind, dir, file) for every test under conformance/."""
    tests = []
    base = os.path.join(DIST, "conformance")
    for d, _dirs, files in sorted(os.walk(base)):
        for f in sorted(files):
            if RUN_RE.match(f):
                kind = "run"
            elif BUILDONLY_RE.match(f):
                kind = "buildonly"
            elif SH_RE.match(f):
                kind = "sh"
            else:
                continue
            rel = os.path.relpath(os.path.join(d, f), base)
            tests.append((os.path.splitext(rel)[0], kind, d, f))
    return tests


def ldlibs(d, libdir):
    """The directory's LDLIBS, less libraries the destdir does not have
    (MINIX has no librt: what NetBSD keeps there is in libc and
    libpthread)."""
    path = os.path.join(d, "LDLIBS")
    if not os.path.exists(path):
        return []
    libs = []
    for w in open(path).read().split():
        if w.startswith("-l") and not any(
                os.path.exists(os.path.join(libdir, "lib%s%s" % (w[2:], ext)))
                for ext in (".so", ".a")):
            continue
        libs.append(w)
    return libs


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--destdir", required=True)
    ap.add_argument("--tooldir", required=True)
    ap.add_argument("--out", required=True, help="disk image to write")
    ap.add_argument("-j", type=int, default=8)
    args = ap.parse_args()

    sysroot = os.path.abspath(args.destdir)
    libdir = os.path.join(sysroot, "usr/lib")
    bindir = os.path.join(os.path.abspath(args.tooldir), "bin")
    cc = [os.path.join(bindir, "x86_64-elf64-minix-clang"),
          "--sysroot=" + sysroot, "-B" + libdir, "-L" + libdir,
          "-fuse-ld=" + os.path.join(bindir, "x86_64-elf64-minix-ld.lld")
          ] + TARGET_FLAGS + LINK_FLAGS
    if not os.path.exists(cc[0]):
        sys.exit("posixsuite: no %s" % cc[0])

    stage = tempfile.mkdtemp(prefix="posixsuite.")
    top = os.path.join(stage, "posix")
    try:
        os.makedirs(top)

        def build(t):
            tid, kind, d, f = t
            out = os.path.join(top, tid)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            if kind == "sh":
                shutil.copy(os.path.join(d, f), out + ".sh")
                return t, True, ""
            cmd = cc + CFLAGS + ["-I" + os.path.join(DIST, "include"),
                                 "-I" + d]
            if kind == "buildonly":
                cmd += ["-c", "-o", os.devnull, os.path.join(d, f)]
            else:
                # Upstream assumes a C library with the threads in it, as
                # glibc's is now: tests without an LDLIBS use them too.
                cmd += ["-o", out, os.path.join(d, f)] + ldlibs(d, libdir) + [
                    "-lpthread", "-lm"]
            p = subprocess.run(cmd, capture_output=True, text=True)
            return t, p.returncode == 0, p.stderr

        tests = find_tests()
        results = {}
        with concurrent.futures.ThreadPoolExecutor(args.j) as ex:
            for t, ok, err in ex.map(build, tests):
                results[t[0]] = (t, ok, err)

        # Helper scripts and data the tests use from their own directory.
        for d in sorted({t[2] for t in tests}):
            rel = os.path.relpath(d, os.path.join(DIST, "conformance"))
            for f in os.listdir(d):
                src = os.path.join(d, f)
                if (os.path.isfile(src) and f.endswith(".sh")
                        and not SH_RE.match(f)):
                    shutil.copy(src, os.path.join(top, rel, f))

        p = subprocess.run(cc + ["-w", "-o", os.path.join(top, "t0"),
                                 os.path.join(DIST, "tools/t0.c")],
                           capture_output=True, text=True)
        if p.returncode != 0:
            sys.exit("posixsuite: t0 did not build:\n" + p.stderr)
        with open(os.path.join(top, "run1.sh"), "w") as f:
            f.write(RUN1)

        nobuild = 0
        with open(os.path.join(top, "MANIFEST"), "w") as man, \
                open(args.out + ".log", "w") as log:
            for tid, kind, _d, _f in tests:
                ok, err = results[tid][1], results[tid][2]
                man.write("%s %s %s\n" % (tid, kind, "ok" if ok else "nobuild"))
                if not ok:
                    nobuild += 1
                    errs = [l for l in err.splitlines() if "error:" in l]
                    log.write("%s: %s\n" % (tid, "; ".join(errs[:3])
                                            or err.strip()[-300:]))

        with tarfile.open(args.out, "w", format=tarfile.USTAR_FORMAT) as tf:
            tf.add(top, arcname="posix")
        size = os.path.getsize(args.out)
        with open(args.out, "ab") as f:      # whole MBs, for the disk
            f.truncate((size + (1 << 20) - 1) & ~((1 << 20) - 1))
        print("posixsuite: %d tests, %d did not build (see %s.log); %s, %d KB"
              % (len(tests), nobuild, args.out, args.out,
                 os.path.getsize(args.out) // 1024))
    finally:
        shutil.rmtree(stage, ignore_errors=True)


if __name__ == "__main__":
    main()
