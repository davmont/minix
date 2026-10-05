#!/usr/bin/env python3
"""Boot an ISO, stage the tests, run a command, then hold the guest and run
commands dropped into <logdir>/cmd (output appended to cmd.out); 'quit' ends."""
import sys, os, time, json, socket, subprocess
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import qemutest as q
_P = subprocess.Popen
def _popen(cmd, *a, **k):
    if cmd and "qemu-system" in cmd[0]:
        cmd = list(cmd) + ["-action", "shutdown=pause"]
    return _P(cmd, *a, **k)
q.subprocess.Popen = _popen
iso, logdir, smp, first = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
os.makedirs(logdir, exist_ok=True)
class A: pass
a = A(); a.iso = iso; a.smp = smp; a.mem = 1024; a.drive = None
a.kvm = q.kvm_usable(); a.qemu = "qemu-system-x86_64"; a.boot_timeout = 300
a.boot_retries = 2; a.log_dir = logdir; a.suite = "quick"; a.tests = "100"
g = q.fresh_guest(a, open(os.path.join(logdir, "serial.log"), "wb"))
open(os.path.join(logdir, "qmp"), "w").write(g.qmp)
r, out = g.ser.run(first, 900)
open(os.path.join(logdir, "cmd.out"), "a").write("$ %s\n%s\n" % (first, out))
print("holding; qmp %s" % g.qmp, flush=True)
cf = os.path.join(logdir, "cmd")
while True:
    if os.path.exists(cf):
        c = open(cf).read().strip(); os.unlink(cf)
        if c == "quit": break
        try:
            if c.startswith("key "):
                so = socket.socket(socket.AF_UNIX); so.settimeout(5); so.connect(g.qmp)
                f = so.makefile("rw"); f.readline()
                for m in ['{"execute":"qmp_capabilities"}', json.dumps({"execute": "send-key",
                          "arguments": {"keys": [{"type": "qcode", "data": k} for k in c.split()[1:]]}})]:
                    f.write(m + "\n"); f.flush(); f.readline()
                so.close(); out = "sent"
            else:
                r, out = g.ser.run(c, 120)
        except q.Timeout:
            out = "TIMEOUT"
        open(cf + ".out", "a").write("$ %s\n%s\n" % (c, out))
    time.sleep(1)
g.stop()
