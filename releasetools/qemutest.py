#!/usr/bin/env python3
"""Boot a MINIX amd64 ISO under QEMU, log in over the serial console, run a
test suite, and exit non-zero on any failure.

This is the regression gate behind CI.  It needs only python3 and
qemu-system-x86_64 on the host; KVM is used when /dev/kvm is usable and
plain TCG (-cpu max) otherwise, so the same script runs on a developer box
and on a hosted CI runner.

    releasetools/qemutest.py --iso minix_amd64.iso --suite boot
    releasetools/qemutest.py --iso minix_amd64.iso --suite quick
    releasetools/qemutest.py --iso minix_amd64.iso --tests "1 2 3 45"

Suites:
    boot    boot to login:, log in, exit.  Proves the image is bootable.
    quick   the check-install subset of minix/tests (skips the very slow
            and the network tests).
    full    every test the run script knows about (hours).
    kyua    kyua over /usr/tests (only meaningful when the "tests" set is
            on the image).

How it works: the ISO gets a getty on tty00, so the serial line is a shell.
The CD root is read-only isofs and /tmp is a 128 KB ramdisk, so the suite
is copied onto a fresh MFS ramdisk (/dev/ram4 mounted on /mnt) before it
runs.  Commands are sent slowly, one character at a time (the MINIX tty
input buffer is 256 bytes) and every command is followed by an echo of a
marker that the shell prints only once the command has finished; the echo
is split-quoted so the command's own echo can never match it.

Tests run one at a time, each under its own timeout.  A test that hangs is
interrupted; if the guest no longer answers at all (some hangs take the
console down with them) the VM is thrown away, a fresh one is booted and
staged, and the run carries on with the next test.  The run script's -T
flag produces TAP, which is parsed per test.

Exit codes: 0 all tests passed, 1 some test failed or hung, 2 the image
did not boot or log in, 3 the host could not keep a guest running, 4 bad
usage or host problem.
"""

import argparse
import os
import re
import select
import shutil
import socket
import subprocess
import sys
import tempfile
import time

# The subset that minix/tests/check-install runs, minus the interactive prompt.
QUICK_TESTS = ("1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 "
               "21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 "
               "41 42 43 44 45 46 47 49 50 51 52 53 54 55 56 57 58 59 "
               "61 62 63 64 66 67 68 69 70 71 72 73 74 75 76 77 78 79 "
               "sh1 interp mfs isofs")

# Where the tests live on the image and how much ramdisk they get (KB).
TESTS_DIR = "/usr/tests/minix-posix"
RAMDISK_DEV = "/dev/ram4"
RAMDISK_KB = 24576

MARK = "QT_DONE"          # what the shell prints when a command is done
PROMPT = "QT#"            # a prompt no MINIX message can produce


class Timeout(Exception):
    pass


class Serial:
    """The guest's serial console over a QEMU unix socket."""

    def __init__(self, path, log):
        self.sock = None
        deadline = time.time() + 30
        while self.sock is None:
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(path)
                self.sock = s
            except OSError:
                if time.time() > deadline:
                    raise
                time.sleep(0.2)
        self.buf = bytearray()
        self.log = log

    def pump(self, timeout):
        """Read whatever arrived within `timeout` seconds; False on EOF."""
        r, _, _ = select.select([self.sock], [], [], timeout)
        if not r:
            return True
        d = self.sock.recv(65536)
        if not d:
            return False
        self.buf.extend(d)
        self.log.write(d)
        self.log.flush()
        return True

    def expect(self, pattern, timeout, start=0):
        """Wait until `pattern` matches the transcript after offset `start`.
        Returns the match; raises Timeout."""
        rx = re.compile(pattern.encode(), re.M)
        deadline = time.time() + timeout
        while True:
            m = rx.search(self.buf, start)
            if m:
                return m
            left = deadline - time.time()
            if left <= 0:
                raise Timeout(pattern)
            if not self.pump(min(left, 1.0)):
                raise Timeout(pattern + " (EOF)")

    def send(self, text):
        # One character at a time: the tty input buffer is 256 bytes and a
        # burst longer than that is silently truncated.
        for ch in text:
            self.sock.sendall(ch.encode())
            time.sleep(0.02)

    def run(self, cmd, timeout):
        """Run a shell command; return (rc, output) of just that command.
        On Timeout the command is still running in the guest."""
        start = len(self.buf)
        # Split quotes: the command echo shows QT_'DONE', the result QT_DONE.
        self.send("%s; echo QT_RC=$? QT_'DONE'\n" % cmd)
        m = self.expect(r"QT_RC=(\d+) " + MARK + r"\s*\r?\n", timeout, start)
        out = self.buf[start:m.start()].decode("utf-8", "replace")
        # Drop the echoed command line itself.
        out = out.split("\n", 1)[1] if "\n" in out else ""
        return int(m.group(1)), out

    def interrupt(self):
        """Kill the foreground command and get the prompt back.  Both
        interrupt characters are sent: ^C is the NetBSD default, DEL the
        classic MINIX one."""
        start = len(self.buf)
        for ch in ("\x03", "\x7f"):
            self.send(ch)
            time.sleep(1)
        self.send("\n")
        self.expect(r"\n" + PROMPT, 20, start)


class Guest:
    """One QEMU instance running the ISO, logged in and ready for commands."""

    def __init__(self, args, log):
        self.args = args
        self.log = log
        self.tmp = tempfile.mkdtemp(prefix="qemutest.")
        sock = os.path.join(self.tmp, "serial.sock")
        cmd = [args.qemu]
        if args.kvm:
            cmd += ["-enable-kvm", "-cpu", "host"]
        else:
            # TCG: "max" exposes FSGSBASE, which MINIX amd64 needs for TLS.
            cmd += ["-cpu", "max"]
        cmd += ["-smp", str(args.smp), "-m", str(args.mem),
                "-cdrom", args.iso, "-boot", "d",
                "-display", "none", "-monitor", "none", "-no-reboot",
                "-serial", "unix:%s,server,nowait" % sock]
        self.qemu = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.PIPE)
        self.ser = Serial(sock, log)

    def login(self):
        """Boot to a root shell with a unique prompt.  Raises Timeout."""
        t0 = time.time()
        self.ser.expect(r"login: $", self.args.boot_timeout)
        print("qemutest: login prompt after %.0fs" % (time.time() - t0),
              flush=True)
        self.ser.send("root\n")
        self.ser.expect(r"# $", 60)
        # A prompt nothing else prints; split-quoted so its own echo differs.
        self.ser.send("PS1='Q''T#'\n")
        self.ser.expect(r"\n" + PROMPT, 30)

    def stage_tests(self):
        """Copy the POSIX suite onto a writable ramdisk and cd there.
        Returns an error message, or None."""
        r, out = self.ser.run("test -x %s/run" % TESTS_DIR, 30)
        if r != 0:
            return ("%s/run not on the image (build the ISO with the "
                    "minix-tests set)" % TESTS_DIR)
        setup = ("ramdisk %d %s >/dev/null && mkfs.mfs %s >/dev/null && "
                 "mount %s /mnt >/dev/null && (cd %s && pax -rw . /mnt) && "
                 "cd /mnt" % (RAMDISK_KB, RAMDISK_DEV, RAMDISK_DEV,
                              RAMDISK_DEV, TESTS_DIR))
        r, out = self.ser.run(setup, 300)
        if r != 0:
            return "could not stage the tests on a ramdisk:\n" + out
        return None

    def stop(self):
        self.qemu.terminate()
        try:
            self.qemu.wait(10)
        except subprocess.TimeoutExpired:
            self.qemu.kill()
        err = self.qemu.stderr.read().decode("utf-8", "replace").strip()
        if err and "terminating on signal" not in err:
            print("qemu: " + err)
        shutil.rmtree(self.tmp, ignore_errors=True)


def kvm_usable():
    return os.access("/dev/kvm", os.R_OK | os.W_OK)


def tap_verdict(name, out):
    """What `run -T -t name` said about that one test."""
    if re.search(r"^not ok test %s\b" % re.escape(name), out, re.M):
        return "fail"
    if re.search(r"^ok test %s\b" % re.escape(name), out, re.M):
        return "ok"
    if re.search(r"warning: skipping test%s\b" % re.escape(name), out):
        return "skip"
    return "fail"


def fresh_guest(args, log):
    """Boot, log in and stage the tests, or return None with a message."""
    g = Guest(args, log)
    try:
        g.login()
    except Timeout:
        g.stop()
        print("qemutest: the image did not boot to a root shell")
        return None
    if args.suite != "boot" and args.suite != "kyua":
        msg = g.stage_tests()
        if msg:
            g.stop()
            print("qemutest: " + msg)
            return None
    return g


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--iso", required=True)
    ap.add_argument("--suite", choices=["boot", "quick", "full", "kyua"],
                    default="boot")
    ap.add_argument("--tests", help="explicit test list, e.g. '1 2 3 sh1'")
    ap.add_argument("--test-timeout", type=int, default=0,
                    help="seconds per test (default: 600 KVM, 1200 TCG)")
    ap.add_argument("--boot-timeout", type=int, default=0,
                    help="seconds to reach login: (default: 300 KVM, 600 TCG)")
    ap.add_argument("--max-reboots", type=int, default=5,
                    help="give up after this many guest wedges")
    ap.add_argument("--mem", type=int, default=1024)
    ap.add_argument("--smp", type=int, default=2)
    ap.add_argument("--no-kvm", action="store_true")
    ap.add_argument("--qemu", default="qemu-system-x86_64")
    ap.add_argument("--log-dir", default="qemutest-logs")
    args = ap.parse_args()

    if not os.path.exists(args.iso):
        print("no such image: %s" % args.iso, file=sys.stderr)
        return 4
    if shutil.which(args.qemu) is None:
        print("%s not found" % args.qemu, file=sys.stderr)
        return 4
    args.kvm = kvm_usable() and not args.no_kvm
    slow = 1 if args.kvm else 2          # TCG boots in ~45s vs ~30s on KVM
    args.boot_timeout = args.boot_timeout or 300 * slow
    test_timeout = args.test_timeout or 600 * slow

    os.makedirs(args.log_dir, exist_ok=True)
    transcript = os.path.join(args.log_dir, "serial.log")
    tapfile = os.path.join(args.log_dir, "results.tap")
    print("qemutest: %s, %s, suite=%s" % (
        args.iso, "KVM" if args.kvm else "TCG (no KVM)", args.suite),
        flush=True)

    log = open(transcript, "wb")
    guest = fresh_guest(args, log)
    if guest is None:
        return 2
    try:
        if args.suite == "boot":
            print("qemutest: boot OK")
            return 0

        ser = guest.ser
        if args.suite == "kyua":
            r, out = ser.run("test -x /usr/bin/kyua -a -f /usr/tests/Kyuafile",
                             30)
            if r != 0:
                print("qemutest: kyua or /usr/tests/Kyuafile missing on image")
                return 2
            r, out = ser.run("cd /usr/tests && kyua test >/dev/null 2>&1; "
                             "kyua report-tap --results-filter="
                             "passed,skipped,xfail,broken,failed",
                             6 * test_timeout)
            good = len(re.findall(r"^ok ", out, re.M))
            bad = len(re.findall(r"^not ok ", out, re.M))
            open(tapfile, "w").write(out)
            print("qemutest: kyua ok=%d not-ok=%d" % (good, bad))
            return 1 if bad else 0

        if args.tests:
            tests = args.tests.split()
        elif args.suite == "quick":
            tests = QUICK_TESTS.split()
        else:
            r, out = ser.run("./run -l", 30)
            tests = out.split()

        results = {}
        tap = ["1..%d" % len(tests)]
        reboots = 0
        t_suite = time.time()
        for n, t in enumerate(tests, 1):
            t0 = time.time()
            out = ""
            try:
                r, out = ser.run("./run -T -t '%s'" % t, test_timeout)
                verdict = tap_verdict(t, out)
            except Timeout:
                verdict = "hang"
                try:
                    ser.interrupt()
                except Timeout:
                    # The console is gone with the test.  Start over on a
                    # fresh guest so the remaining tests still run.
                    verdict = "wedge"
                    reboots += 1
                    guest.stop()
                    if reboots > args.max_reboots:
                        print("qemutest: guest wedged %d times, giving up"
                              % reboots)
                        results[t] = verdict
                        break
                    print("qemutest: test %s took the guest down; "
                          "rebooting (%d/%d)"
                          % (t, reboots, args.max_reboots), flush=True)
                    guest = fresh_guest(args, log)
                    if guest is None:
                        results[t] = verdict
                        break
                    ser = guest.ser
            results[t] = verdict
            print("qemutest: [%3d/%d %5.0fs] %-5s test %s"
                  % (n, len(tests), time.time() - t0, verdict, t), flush=True)
            if verdict == "ok":
                tap.append("ok %d - test %s" % (n, t))
            elif verdict == "skip":
                tap.append("ok %d - test %s # SKIP not built for this arch"
                           % (n, t))
            else:
                tap.append("not ok %d - test %s%s" % (
                    n, t, "" if verdict == "fail" else " # " + verdict.upper()))
                for line in out.splitlines():
                    if line.startswith("#"):
                        tap.append("  " + line.rstrip("\r"))
                        print("    " + line.rstrip("\r"))
        open(tapfile, "w").write("\n".join(tap) + "\n")

        ok = [t for t, v in results.items() if v == "ok"]
        skipped = [t for t, v in results.items() if v == "skip"]
        bad = [t for t, v in results.items() if v not in ("ok", "skip")]
        unrun = [t for t in tests if t not in results]
        print("qemutest: %d passed, %d failed, %d skipped, %d not run in %.0fs"
              % (len(ok), len(bad), len(skipped), len(unrun),
                 time.time() - t_suite))
        if bad:
            print("qemutest: FAILED: %s" % " ".join(
                "%s(%s)" % (t, results[t]) for t in bad))
        if unrun:
            print("qemutest: not run: %s" % " ".join(unrun))
            return 3
        return 1 if bad else 0
    except Timeout as e:
        print("qemutest: timed out waiting for %s" % e)
        return 3
    finally:
        if guest is not None:
            guest.stop()
        log.close()
        print("qemutest: transcript in %s" % transcript)


if __name__ == "__main__":
    sys.exit(main())
