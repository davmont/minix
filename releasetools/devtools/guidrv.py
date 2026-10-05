# GUI driver: serial (unix socket) for the shell + QMP (unix socket) for
# mouse injection and screenshots. Reusable helpers.
import os, socket, json, time, subprocess, shutil, select, re, sys

BASE=os.environ.get("DEVTOOLS_OUT", "/tmp/minix-devtools"); os.makedirs(BASE, exist_ok=True)
ROOT=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
ISO=os.environ.get("ISO", os.path.join(ROOT, "minix_amd64.iso"))
CODE=os.path.join(ROOT, "ovmf-working/OVMF_CODE_4M.fd"); VS=os.path.join(ROOT, "ovmf-working/OVMF_VARS_4M.fd")

class VM:
    def __init__(self, tag="g"):
        self.tag=tag
        self.ser=os.path.join(BASE,f"gser_{tag}.sock")
        self.qmp_path=os.path.join(BASE,f"gqmp_{tag}.sock")
        for p in (self.ser,self.qmp_path):
            try: os.unlink(p)
            except OSError: pass
        self.v=os.path.join(BASE,f"v_{tag}.bin"); shutil.copy(VS,self.v)
        self.serlog=open(os.path.join(BASE,f"gser_{tag}.log"),"wb")
        q=["qemu-system-x86_64","--enable-kvm","-cpu","host","-m","3072","-smp","4",
           "-drive",f"if=pflash,unit=0,format=raw,readonly=on,file={CODE}",
           "-drive",f"if=pflash,unit=1,format=raw,file={self.v}",
           "-cdrom",ISO,"-vga","std","-no-reboot",
           "-serial",f"unix:{self.ser},server,nowait",
           "-qmp",f"unix:{self.qmp_path},server,nowait"]
        self.p=subprocess.Popen(q,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        # connect sockets (retry until QEMU creates them)
        self.sc=self._conn(self.ser); self.qc=self._conn(self.qmp_path)
        self.qf=self.qc.makefile("rwb",buffering=0)
        self._qmp_hello()
        self.sbuf=bytearray()
    def _conn(self,path):
        for _ in range(200):
            try:
                s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM); s.connect(path); return s
            except OSError: time.sleep(0.1)
        raise RuntimeError("connect "+path)
    def _qmp_hello(self):
        self.qf.readline()  # greeting
        self.qf.write(b'{"execute":"qmp_capabilities"}\n'); self.qf.readline()
    def qmp(self,cmd,**args):
        m={"execute":cmd}
        if args: m["arguments"]=args
        self.qf.write((json.dumps(m)+"\n").encode())
        # read until a return/ error (skip events)
        while True:
            line=self.qf.readline()
            if not line: return None
            o=json.loads(line)
            if "return" in o or "error" in o: return o
    # ---- serial ----
    def spump(self,sec,until=None):
        e=time.time()+sec
        while time.time()<e:
            r,_,_=select.select([self.sc],[],[],0.2)
            if r:
                d=self.sc.recv(8192)
                if not d: return False
                self.sbuf.extend(d); self.serlog.write(d); self.serlog.flush()
                if until and re.search(until,bytes(self.sbuf[-4000:])): return True
        return False
    def sslow(self,t):
        for ch in t: self.sc.send(ch.encode()); time.sleep(0.04)
    # ---- mouse ----
    def mrel(self,dx,dy):
        self.qmp("input-send-event",events=[
            {"type":"rel","data":{"axis":"x","value":int(dx)}},
            {"type":"rel","data":{"axis":"y","value":int(dy)}}])
    def home(self):  # push cursor to top-left corner
        self.mrel(-4000,-4000); time.sleep(0.2)
    def moveto(self,x,y):  # absolute-ish via home + relative (calibrate signs later)
        self.home(); self.mrel(x,y); time.sleep(0.2)
    def click(self,btn="left"):
        self.qmp("input-send-event",events=[{"type":"btn","data":{"button":btn,"down":True}}])
        time.sleep(0.08)
        self.qmp("input-send-event",events=[{"type":"btn","data":{"button":btn,"down":False}}])
    def shot(self,name):
        ppm=os.path.join(BASE,f"shot_{name}.ppm"); png=os.path.join(BASE,f"shot_{name}.png")
        try: os.unlink(ppm)
        except OSError: pass
        self.qmp("screendump",filename=ppm)
        for _ in range(30):
            if os.path.exists(ppm) and os.path.getsize(ppm)>0: break
            time.sleep(0.1)
        subprocess.run(["convert",ppm,png],check=False)
        return png
    def kill(self):
        try: self.p.terminate()
        except: pass
        time.sleep(1)
        try: self.p.kill()
        except: pass


    def home2(self):
        for _ in range(250): self.mrel(-6,-6); time.sleep(0.002)
        time.sleep(0.15)
    _cx=None; _cy=None
    def goto(self,x,y,step=6,d=0.003):
        self.home2(); self._cx=0; self._cy=0  # re-home each click (max mouse IPC)
        dx=int(x)-self._cx; dy=int(y)-self._cy
        sx=step if dx>=0 else -step
        for _ in range(abs(dx)//step): self.mrel(sx,0); time.sleep(d)
        sy=step if dy>=0 else -step
        for _ in range(abs(dy)//step): self.mrel(0,sy); time.sleep(d)
        self._cx=int(x); self._cy=int(y); time.sleep(0.2)
    def clickat(self,x,y):
        self.goto(x,y); time.sleep(0.1); self.click(); time.sleep(0.3)

if __name__=="__main__":
    vm=VM("poc")
    print("login:", vm.spump(200, rb"login:"))
    vm.sslow("root\n"); vm.spump(12, rb"# ")
    vm.sslow("startlxqt >/tmp/sl.log 2>&1 &\n")
    vm.spump(50)
    print("desktop screenshot:", vm.shot("poc_desktop"))
    # calibrate mouse: home, then move to center-ish
    vm.home(); print("home shot:", vm.shot("poc_home"))
    vm.mrel(640,400); time.sleep(0.3); print("moved shot:", vm.shot("poc_moved"))
    print("== DONE ==")
    vm.kill()
