#!/usr/bin/env python3
"""Boot an ISO, log in, stage the tests, run one shell command, print its output."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import qemutest as q
iso, logdir, cmd = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(logdir, exist_ok=True)
class A: pass
a = A(); a.iso = iso; a.smp = 1; a.mem = 1024; a.drive = None
a.kvm = q.kvm_usable(); a.qemu = "qemu-system-x86_64"; a.boot_timeout = 300
a.boot_retries = 2; a.log_dir = logdir; a.suite = "quick"; a.tests = ""
g = q.fresh_guest(a, open(os.path.join(logdir, "serial.log"), "wb"))
try:
    r, out = g.ser.run(cmd, 60)
    print(out.replace("\r", ""))
except q.Timeout:
    print("TIMEOUT")
g.stop()
