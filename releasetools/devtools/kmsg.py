#!/usr/bin/env python3
"""Dump the MINIX kernel message ring of a running guest via QMP memsave."""
import socket, json, sys, subprocess, os, tempfile
qmp, kernel = sys.argv[1], sys.argv[2]
addr = int([l.split()[0] for l in subprocess.run(["nm", kernel], capture_output=True, text=True).stdout.splitlines() if l.endswith(" kmessages")][0], 16)
size = 4 + 4 + 10000
out = os.path.join(tempfile.mkdtemp(), "km.bin")
s = socket.socket(socket.AF_UNIX); s.settimeout(10); s.connect(qmp); f = s.makefile("rw"); f.readline()
for c in [{"execute": "qmp_capabilities"}, {"execute": "memsave", "arguments": {"val": addr, "size": 20000, "filename": out, "cpu-index": 0}}]:
    f.write(json.dumps(c) + "\n"); f.flush(); print(f.readline().strip()[:100], file=sys.stderr)
b = open(out, "rb").read()
nxt, size_ = int.from_bytes(b[0:4], "little"), int.from_bytes(b[4:8], "little")
buf = b[8:]
print("km_next=%d km_size=%d" % (nxt, size_), file=sys.stderr)
ring = buf[nxt:] + buf[:nxt] if size_ else buf
sys.stdout.write(ring.replace(b"\0", b"").decode("latin1")[-6000:])
