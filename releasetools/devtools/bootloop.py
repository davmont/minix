#!/usr/bin/env python3
"""Boot an ISO on N CPUs repeatedly; on a stall before login, dump lock state."""
import sys, os, time, subprocess, re, json, socket
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import qemutest as q
iso, kernel, n, logdir = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
smp = int(sys.argv[5]) if len(sys.argv) > 5 else 2
os.makedirs(logdir, exist_ok=True)
_P = subprocess.Popen
def _popen(cmd, *a, **k):
    if cmd and "qemu-system" in cmd[0]:
        cmd = list(cmd) + ["-action", "shutdown=pause"]
    return _P(cmd, *a, **k)
q.subprocess.Popen = _popen
syms = {}
for l in subprocess.run(["nm", "-n", kernel], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 3: syms[p[2]] = int(p[0], 16)
addrs = sorted((v, k) for k, v in syms.items())
def sym(a):
    import bisect
    i = bisect.bisect_right([x[0] for x in addrs], a) - 1
    return "%s+0x%x" % (addrs[i][1], a - addrs[i][0]) if i >= 0 else hex(a)
def hmp(g, *cmds):
    s = socket.socket(socket.AF_UNIX); s.settimeout(10); s.connect(g.qmp)
    f = s.makefile("rw"); f.readline()
    f.write('{"execute":"qmp_capabilities"}\n'); f.flush(); f.readline()
    out = []
    for c in cmds:
        f.write(json.dumps({"execute": "human-monitor-command", "arguments": {"command-line": c}}) + "\n"); f.flush()
        out.append(json.loads(f.readline()).get("return", ""))
    return out
class A: pass
args = A(); args.iso = iso; args.smp = smp; args.mem = 1024; args.drive = None
args.kvm = q.kvm_usable(); args.qemu = "qemu-system-x86_64"
for i in range(1, n + 1):
    log = open(os.path.join(logdir, "boot%d.log" % i), "wb")
    g = q.Guest(args, log)
    try:
        g.ser.expect(r"login: $", 90)
        print("boot %d ok" % i, flush=True); g.stop(); continue
    except q.Timeout:
        pass
    rep = []
    regs = hmp(g, "info registers -a")[0]
    rep.append(regs)
    for lk in ("big_kernel_lock", "boot_lock", "runqueue_lock", "bkl_held_by_cpu", "ap_cpus_booted", "ap_cpu_ready"):
        if lk in syms:
            rep.append("%s: %s" % (lk, hmp(g, "x /2wx 0x%x" % syms[lk])[0].strip()))
    for cpu, rsp in re.findall(r"CPU#(\d+).*?RSP=([0-9a-f]+)", regs, re.S):
        mem = hmp(g, "x /64gx 0x%s" % rsp)[0]
        ra = [int(w, 16) for w in re.findall(r"0x(ffffffff80[0-9a-f]{6})", mem)]
        rep.append("cpu%s stack: %s" % (cpu, " ".join(sym(a) for a in ra if a < syms.get("_etext", 1 << 64))))
    for cpu, rip in re.findall(r"CPU#(\d+).*?RIP=([0-9a-f]+)", regs, re.S):
        rep.append("cpu%s rip %s" % (cpu, sym(int(rip, 16))))
    open(os.path.join(logdir, "stall%d.txt" % i), "w").write("\n".join(rep))
    last = g.ser.buf.decode("latin1").strip().rsplit("\n", 1)[-1]
    print("boot %d STALL after %r" % (i, last), flush=True)
    print("\n".join(r for r in rep[1:]), flush=True)
    g.stop()
