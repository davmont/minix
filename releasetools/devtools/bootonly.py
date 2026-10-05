#!/usr/bin/env python3
"""Start a guest with a QMP socket and leave it running (no login)."""
import sys, os, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import qemutest as q
iso, logdir = sys.argv[1], sys.argv[2]
os.makedirs(logdir, exist_ok=True)
class A: pass
a = A(); a.iso = iso; a.smp = 1; a.mem = 1024; a.drive = None
a.kvm = q.kvm_usable(); a.qemu = "qemu-system-x86_64"; a.boot_timeout = 300
a.boot_retries = 0; a.log_dir = logdir; a.suite = "quick"; a.tests = ""
g = q.Guest(a, open(os.path.join(logdir, "serial.log"), "wb"))
print("qmp %s" % g.qmp, flush=True)
time.sleep(int(sys.argv[3]) if len(sys.argv) > 3 else 600)
g.stop()
