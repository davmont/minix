#!/usr/bin/env python3
"""Measure TCP throughput between two MINIX guests over a link with latency.

Boots two guests of the given ISO, each with an e1000 NIC on a QEMU UDP
socket; a relay here forwards the frames between them after a delay, which
gives the link a round-trip time.  Guest A sends MB megabytes to guest B with
tcpbench (minix/tests/tcpbench.c), and tcpdump on B shows the TCP options of
the connection's SYN packets.

    netbench.py ISO [--rtt-ms 20] [--mb 20] [--smp 1] [--log-dir DIR]
"""
import argparse, heapq, os, socket, sys, threading, time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                ".."))
import qemutest as q

BENCH = "/usr/tests/minix-posix/tcpbench"


class Relay:
    """Forward UDP datagrams between two QEMU socket netdevs, delayed."""

    def __init__(self, delay_s, port_a, port_b, peer_a, peer_b):
        self.delay = delay_s
        self.sa = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sa.bind(("127.0.0.1", port_a))
        self.sb = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sb.bind(("127.0.0.1", port_b))
        self.peer_a, self.peer_b = peer_a, peer_b
        self.queue, self.cv, self.seq = [], threading.Condition(), 0
        for args in ((self.sa, self.sb, peer_b), (self.sb, self.sa, peer_a)):
            threading.Thread(target=self.rx, args=args, daemon=True).start()
        threading.Thread(target=self.tx, daemon=True).start()

    def rx(self, src, dst, to):
        while True:
            data, _ = src.recvfrom(65536)
            with self.cv:
                self.seq += 1
                heapq.heappush(self.queue, (time.monotonic() + self.delay,
                                            self.seq, dst, to, data))
                self.cv.notify()

    def tx(self):
        while True:
            with self.cv:
                while not self.queue:
                    self.cv.wait()
                when, _, dst, to, data = self.queue[0]
                wait = when - time.monotonic()
                if wait > 0:
                    self.cv.wait(wait)
                    continue
                heapq.heappop(self.queue)
            dst.sendto(data, to)


def guest(a, name, logdir, mac, local, remote):
    a.qemu_extra = ["-netdev", "socket,id=n0,udp=127.0.0.1:%d,localaddr="
                    "127.0.0.1:%d" % (remote, local),
                    "-device", "e1000,netdev=n0,mac=%s" % mac]
    a.log_dir = os.path.join(logdir, name)
    os.makedirs(a.log_dir, exist_ok=True)
    return q.fresh_guest(a, open(os.path.join(a.log_dir, "serial.log"), "wb"))


def run(g, cmd, timeout=120):
    r, out = g.ser.run(cmd, timeout)
    return out.replace("\r", "").strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("iso")
    ap.add_argument("--rtt-ms", type=float, default=20)
    ap.add_argument("--mb", type=int, default=20)
    ap.add_argument("--smp", type=int, default=1)
    ap.add_argument("--log-dir", default=os.environ.get(
        "DEVTOOLS_OUT", "/tmp/minix-devtools") + "/netbench")
    opts = ap.parse_args()

    base = 40000 + os.getpid() % 10000
    la, lb, ra, rb = base, base + 1, base + 2, base + 3
    Relay(opts.rtt_ms / 2000.0, ra, rb, ("127.0.0.1", la),
          ("127.0.0.1", lb))

    class A: pass
    def args():
        a = A(); a.iso = opts.iso; a.smp = opts.smp; a.mem = 1024
        a.drive = None; a.kvm = q.kvm_usable(); a.qemu = "qemu-system-x86_64"
        a.boot_timeout = 300; a.boot_retries = 2; a.suite = "quick"
        a.tests = ""
        return a

    ga = guest(args(), "a", opts.log_dir, "52:54:00:12:34:0a", la, ra)
    gb = guest(args(), "b", opts.log_dir, "52:54:00:12:34:0b", lb, rb)
    try:
        run(ga, "ifconfig em0 inet 10.9.0.1 netmask 255.255.255.0 up")
        run(gb, "ifconfig em0 inet 10.9.0.2 netmask 255.255.255.0 up")
        run(gb, "(tcpdump -n -l -v -c 2 -i em0 'tcp[13] & 2 != 0' "
                ">/tmp/td 2>&1 &); (%s -l 5001 >/tmp/rx 2>&1 &)" % BENCH)
        time.sleep(3)
        print(run(ga, "ping -c 3 10.9.0.2 | tail -1"))
        print("A:", run(ga, "%s 10.9.0.2 5001 %d" % (BENCH, opts.mb), 900))
        time.sleep(2)
        print("B:", run(gb, "cat /tmp/rx"))
        print(run(gb, "cat /tmp/td"))
    finally:
        ga.stop()
        gb.stop()


if __name__ == "__main__":
    main()
