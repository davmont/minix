#!/usr/bin/env python3
"""Resolve merge conflicts in the test registration lists: number lists
(Makefile, run, qemutest.py) become the sorted union of both sides; set-list
lines become the union, sorted by test number.  Refuses anything else."""
import re, sys

def nums_key(tok):
    return (0, int(tok)) if tok.isdigit() else (1, tok)

def merge_line(a, b):
    # a and b: same prefix/suffix, a run of space-separated tokens between.
    ma = re.match(r'^(\s*"?)([0-9a-z ]*?)(\s*\\?"?\)?\s*)$', a)
    mb = re.match(r'^(\s*"?)([0-9a-z ]*?)(\s*\\?"?\)?\s*)$', b)
    if not (ma and mb) or ma.group(1) != mb.group(1) or ma.group(3) != mb.group(3):
        raise SystemExit("cannot merge:\n%s\n%s" % (a, b))
    ta, tb = ma.group(2).split(), mb.group(2).split()
    nums = sorted({t for t in ta + tb if t.isdigit()}, key=int)
    rest = [t for t in ta if not t.isdigit()]
    for t in tb:
        if not t.isdigit() and t not in rest:
            rest.append(t)
    return ma.group(1) + " ".join(nums + rest) + ma.group(3)

def resolve(path):
    lines = open(path).read().split("\n")
    out, i, n = [], 0, 0
    while i < len(lines):
        if lines[i].startswith("<<<<<<< "):
            j = lines.index("=======", i)
            k = next(x for x in range(j, len(lines)) if lines[x].startswith(">>>>>>> "))
            ours, theirs = lines[i+1:j], lines[j+1:k]
            if all("minix-tests" in l for l in ours + theirs):
                u = sorted(set(ours + theirs),
                           key=lambda l: int(re.search(r'test(\d+)', l).group(1)))
                out += u
            elif len(ours) == 1 and len(theirs) == 1:
                out.append(merge_line(ours[0], theirs[0]))
            else:
                raise SystemExit("unexpected conflict in %s" % path)
            i, n = k + 1, n + 1
        else:
            out.append(lines[i]); i += 1
    open(path, "w").write("\n".join(out))
    print("%s: %d hunks" % (path, n))

for p in sys.argv[1:]:
    resolve(p)
