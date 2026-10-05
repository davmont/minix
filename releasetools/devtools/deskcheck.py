#!/usr/bin/env python3
"""Desktop smoke test: boot a MKDESKTOP UEFI ISO, start LXQt, launch
qterminal, and report which desktop processes are alive, plus screendumps.

usage: deskcheck.py ISO OUTDIR
"""
import json, os, shutil, socket, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import qemutest  # Serial, PROMPT

OVMF = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "ovmf-working")
iso, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
tmp = tempfile.mkdtemp(prefix="deskcheck.")
vars_fd = os.path.join(tmp, "vars.fd")
shutil.copy(os.path.join(OVMF, "OVMF_VARS_4M.fd"), vars_fd)
ser_path = os.path.join(tmp, "serial.sock")
qmp_path = os.path.join(tmp, "qmp.sock")
cmd = ["qemu-system-x86_64", "-enable-kvm", "-cpu", "host", "-smp", "2",
       "-m", "2048",
       "-drive", "if=pflash,format=raw,readonly=on,file=%s"
       % os.path.join(OVMF, "OVMF_CODE_4M.fd"),
       "-drive", "if=pflash,format=raw,file=%s" % vars_fd,
       "-cdrom", iso, "-display", "none", "-monitor", "none",
       "-no-reboot",
       "-serial", "unix:%s,server,nowait" % ser_path,
       "-qmp", "unix:%s,server,nowait" % qmp_path]
qemu = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
log = open(os.path.join(out, "serial.log"), "wb")


def qmp(*cmds):
    s = socket.socket(socket.AF_UNIX)
    s.settimeout(30)
    s.connect(qmp_path)
    f = s.makefile("rw")
    f.readline()
    replies = []
    for c in ('{"execute":"qmp_capabilities"}',) + cmds:
        f.write(c + "\n")
        f.flush()
        replies.append(json.loads(f.readline()))
    s.close()
    return replies


def screendump(name):
    path = os.path.join(out, name)
    qmp('{"execute":"screendump","arguments":{"filename":"%s",'
        '"format":"png"}}' % path)
    return path


result = {}
try:
    ser = qemutest.Serial(ser_path, log)
    ser.expect(r"login: $", 300)
    ser.send("root\n")
    ser.expect(r"# $", 60)
    ser.send("PS1='Q''T#'\n")
    ser.expect(r"\n" + qemutest.PROMPT, 30)

    rc, o = ser.run("startlxqt > /tmp/startlxqt.log 2>&1 &", 30)
    time.sleep(45)
    screendump("1-session.png")
    rc, o = ser.run("ps -ax | grep -E 'lxqt|wlcompd|dbus' | grep -v grep",
                    60)
    result["after_startlxqt"] = o

    rc, o = ser.run("env XDG_RUNTIME_DIR=/tmp/runtime-0 "
                    "WAYLAND_DISPLAY=wayland-0 qterminal "
                    "> /tmp/qterminal.log 2>&1 &", 30)
    time.sleep(20)
    screendump("2-qterminal.png")
    rc, o = ser.run("ps -ax | grep -E 'qterminal' | grep -v grep", 60)
    result["qterminal"] = o
    rc, o = ser.run("tail -20 /tmp/startlxqt.log; echo ----; "
                    "tail -20 /tmp/qterminal.log", 60)
    result["logs"] = o
except qemutest.Timeout as e:
    result["timeout"] = str(e)
finally:
    qemu.kill()
    qemu.wait()
    shutil.rmtree(tmp, ignore_errors=True)

for k, v in result.items():
    print("=== %s\n%s" % (k, v.strip()))
