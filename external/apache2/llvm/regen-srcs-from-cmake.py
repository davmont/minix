#!/usr/bin/env python3
"""Regenerate reachover Makefile SRCS (.cpp/.c lists) for LLVM 22 from a CMake
build.ninja. Maps each compiled source to the reachover lib whose .PATH is the
longest directory prefix. Leaves TABLEGEN_OUTPUT / .include / everything else
untouched; replaces only the contiguous SRCS+= .cpp block."""
import os, re, sys, glob

NINJA = "/tmp/llvm22_build/build.ninja"
SRCROOT = "/tmp/llvm22"          # build.ninja source paths live under here
LIBDIR = "external/apache2/llvm/lib"
# component dir under /tmp/llvm22 -> reachover srcdir var
COMP = {"llvm": "LLVM_SRCDIR", "clang": "clang"}  # clang uses CLANG_SRCDIR

# 1. compiled sources from build.ninja: full paths under /tmp/llvm22/{llvm,clang}/...
srcs = set()
pat = re.compile(r"CMakeFiles/[^ ]+\.dir/[^ ]*\.o: \S+ (" + re.escape(SRCROOT) + r"/(llvm|clang)/\S+\.(?:cpp|c))(?:\s|$|\|)")
with open(NINJA) as f:
    for line in f:
        if "CMakeFiles/" not in line or ".o: " not in line:
            continue
        m = pat.search(line)
        if m:
            srcs.add(m.group(1))   # /tmp/llvm22/llvm/lib/Support/APInt.cpp

# 2. reachover libs -> list of (component, reldir) from .PATH
#    .PATH: ${LLVM_SRCDIR}/lib/Support  -> ("llvm","lib/Support")
#    .PATH: ${CLANG_SRCDIR}/lib/Basic   -> ("clang","lib/Basic")
lib_paths = {}   # makefile path -> list[(comp, reldir)]
path_owner = []  # (comp, reldir, makefile) for longest-prefix match
ppat = re.compile(r"^\.PATH:\s*\$\{(LLVM_SRCDIR|CLANG_SRCDIR)\}/(\S+)")
for mk in glob.glob(f"{LIBDIR}/*/Makefile"):
    paths = []
    with open(mk) as f:
        for line in f:
            m = ppat.match(line)
            if m:
                comp = "llvm" if m.group(1) == "LLVM_SRCDIR" else "clang"
                paths.append((comp, m.group(2).rstrip("/")))
    if paths:
        lib_paths[mk] = paths
        for comp, d in paths:
            path_owner.append((comp, d, mk))

# 3. assign each source to the makefile with the longest matching .PATH prefix
assigned = {mk: [] for mk in lib_paths}   # mk -> list of SRCS entries (rel to its first matching .PATH)
for s in sorted(srcs):
    rel = s[len(SRCROOT) + 1:]            # llvm/lib/Support/APInt.cpp
    comp, relpath = rel.split("/", 1)     # comp=llvm, relpath=lib/Support/APInt.cpp
    sdir = os.path.dirname(relpath)
    best = None
    for (c, d, mk) in path_owner:
        if c != comp:
            continue
        if sdir == d or sdir.startswith(d + "/"):
            if best is None or len(d) > len(best[0]):
                best = (d, mk)
    if best is None:
        continue   # source not owned by any reachover lib (e.g. tools-only) -> skip
    d, mk = best
    entry = relpath[len(d) + 1:]          # APInt.cpp  OR  GISel/X86CallLowering.cpp
    assigned[mk].append(entry)

# 4. rewrite the SRCS+= .cpp/.c block in each Makefile
def is_src_line(t):
    t = t.strip().rstrip("\\").strip()
    return t.endswith(".cpp") or t.endswith(".c")

changed = 0
for mk, entries in assigned.items():
    if not entries:
        continue
    entries = sorted(set(entries))
    with open(mk) as f:
        lines = f.readlines()
    # find contiguous SRCS block: first line matching ^SRCS[+ ]*=, consume continuations
    start = None
    for i, l in enumerate(lines):
        if re.match(r"^SRCS\s*\+?=", l):
            start = i
            break
    if start is None:
        continue
    end = start
    while lines[end].rstrip("\n").endswith("\\"):
        end += 1
    # build new block
    block = ["SRCS+=\t" + entries[0] + (" \\\n" if len(entries) > 1 else "\n")]
    for e in entries[1:-1]:
        block.append("\t" + e + " \\\n")
    if len(entries) > 1:
        block.append("\t" + entries[-1] + "\n")
    lines[start:end + 1] = block
    with open(mk, "w") as f:
        f.writelines(lines)
    changed += 1

print(f"sources={len(srcs)} libs_with_paths={len(lib_paths)} rewritten={changed}")
# report libs that got zero sources (potential dead/renamed libs)
zero = [os.path.basename(os.path.dirname(mk)) for mk, e in assigned.items() if not e]
print(f"zero-source libs ({len(zero)}): {' '.join(sorted(zero))}")
