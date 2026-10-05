#!/usr/bin/env python3
"""Boot with a blank second disk, make an MFS on it, write a pattern with
test117 w ..., then cut the power at once (QMP quit) and report whether the
pattern is in the disk image."""
import sys, os, subprocess
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import qemutest as q
iso, logdir, how, tag = sys.argv[1:5]
os.makedirs(logdir, exist_ok=True)
img = os.path.join(logdir, "disk.img")
subprocess.run(["truncate", "-s", "0", img]); subprocess.run(["truncate", "-s", "64M", img])
class A: pass
a = A(); a.iso = iso; a.smp = 1; a.mem = 1024; a.drive = img
a.kvm = q.kvm_usable(); a.qemu = "qemu-system-x86_64"; a.boot_timeout = 300
a.boot_retries = 2; a.log_dir = logdir; a.suite = "quick"; a.tests = "117"
g = q.fresh_guest(a, open(os.path.join(logdir, "serial.log"), "wb"))
r, out = g.ser.run("mkfs.mfs /dev/c0d1 >/dev/null && mkdir -p /tmp/m2 && mount /dev/c0d1 /tmp/m2 >/dev/null && sync; echo ok", 120)
r, out = g.ser.run("./test117 w /tmp/m2/f %s %s" % (how, tag), 60)
g.stop()          # power cut: no shutdown, no sync
data = open(img, "rb").read()
print("%s: %s -> pattern %s" % (how, out.strip().replace("\r","").replace("\n"," | ")[-60:],
      "FOUND" if ("test117-%s-" % tag).encode() in data else "absent"))
