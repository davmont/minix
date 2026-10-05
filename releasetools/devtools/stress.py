#!/usr/bin/env python3
"""Stress pjdfstest in background loops on an SMP guest; when a loop stops
progressing, capture ps/wchan while the foreground shell is still free."""
import sys, os, time, argparse, re
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import qemutest as q

ap = argparse.ArgumentParser()
ap.add_argument("--iso", required=True)
ap.add_argument("--smp", type=int, default=2)
ap.add_argument("--workers", type=int, default=3)
ap.add_argument("--minutes", type=int, default=60)
ap.add_argument("--stall", type=int, default=240)
ap.add_argument("--log-dir", required=True)
ap.add_argument("--cpu", default=None)
a = ap.parse_args()
os.makedirs(a.log_dir, exist_ok=True)

class A: pass
args = A()
args.iso = a.iso; args.smp = a.smp; args.mem = 1024; args.drive = None
args.kvm = q.kvm_usable(); args.qemu = "qemu-system-x86_64"
args.boot_timeout = 300; args.boot_retries = 2; args.log_dir = a.log_dir
args.suite = "pjdfstest"; args.tests = None
log = open(os.path.join(a.log_dir, "serial.log"), "wb")
g = q.fresh_guest(args, log)
if g is None:
    sys.exit(2)
s = g.ser
W = ("i=0; while :; do for t in $(find . -name '*.t'); do i=$((i+1)); "
     "echo \"$i $t\" > /mnt/w$W.last; sh $t >/dev/null 2>&1; done; done")
for w in range(a.workers):
    s.run("(W=%d; %s) >/dev/null 2>&1 & :" % (w, W), 30)
print("stress: %d workers started" % a.workers, flush=True)
last = {}; since = {}
t_end = time.time() + a.minutes * 60
found = False
while time.time() < t_end:
    time.sleep(30)
    try:
        r, out = s.run("cat /mnt/w*.last", 60)
    except q.Timeout:
        print("stress: foreground shell hung too", flush=True)
        regs = g.registers()
        open(os.path.join(a.log_dir, "regs.txt"), "w").write(regs)
        found = True
        break
    now = time.time()
    lines = [l.strip() for l in out.splitlines() if re.match(r"^\d+ ", l.strip())]
    for w, l in enumerate(lines):
        if last.get(w) != l:
            last[w] = l; since[w] = now
        elif now - since[w] > a.stall:
            print("stress: worker %d stuck at '%s' for %.0fs" % (w, l, now - since[w]), flush=True)
            found = True
    print("stress: %s" % " | ".join(lines), flush=True)
    if found:
        for cmd in ("ps -alxww", "ps -axo pid,ppid,stat,wchan,command"):
            try:
                r, out = s.run(cmd, 60)
                open(os.path.join(a.log_dir, "ps.txt"), "a").write("$ %s\n%s\n" % (cmd, out))
            except q.Timeout:
                pass
        break
print("stress: %s" % ("STALL" if found else "clean"), flush=True)
g.stop()
