# Developer tools for testing MINIX images under QEMU

Helpers used while developing and debugging, on top of `../qemutest.py`
(the test harness, which they import).  They boot an ISO from the tree's
`releasetools/amd64_cdimage.sh`, talk to it over the serial console and
QEMU's QMP socket, and leave their output under `$DEVTOOLS_OUT` (default
`/tmp/minix-devtools`) or a log directory given on the command line.

| Tool | What it does |
|------|--------------|
| `runcmd.py ISO LOGDIR CMD` | boot, log in, stage the POSIX tests on a ramdisk, run one shell command, print its output |
| `hold.py ISO LOGDIR SMP CMD` | as runcmd, then keep the guest up: commands written to `LOGDIR/cmd` run in it (output in `cmd.out`); `quit` ends it; `key ...` sends keys |
| `bootonly.py ISO LOGDIR [SECS]` | boot without logging in and keep QEMU up (prints the QMP socket), e.g. to read a console that never reaches login |
| `kmsg.py QMPSOCK KERNEL` | dump the kernel message ring (`kmessages`) of a running guest via QMP memsave; KERNEL is the unstripped `obj/minix/kernel/kernel` |
| `keys.py QMPSOCK KEY[+KEY] ...` | send keystrokes (e.g. `f1` for the IS process dump, `shift+f3` for VFS) |
| `qmpcmd.py QMPSOCK CMD` | run one QMP/HMP command |
| `bootloop.py` | boot an image repeatedly to catch rare boot hangs (register samples on a stall) |
| `stress.py --iso ISO --minutes N --log-dir D` | long fork/exec/IPC stress run with hang detection |
| `powercut.py ISO LOGDIR HOW TAG` | durability check: write with O_SYNC/O_DSYNC/plain, cut QEMU's power, look for the data in the disk image |
| `guidrv.py` | desktop GUI driver: UEFI boot of a MKDESKTOP image, serial shell + QMP mouse/keyboard injection and screendumps |
| `deskcheck.py ISO OUTDIR` | desktop smoke test: start LXQt, launch qterminal, list desktop processes, screendumps |
| `relink_desktop.sh` | relink the statically linked desktop programs against a new libc (needed after a libc ABI change); `LIBCDIR`, `DEST`, optional `OBJ`, `LOGS` |
| `resolve_lists.py FILE...` | resolve merge conflicts in the test lists (Makefile/run/set list/qemutest.py) by taking the union |

Never use `pkill -f` to stop these: it matches the command line of the
shell that runs it.  Stop guests by PID.
