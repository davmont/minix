import socket,json,sys
s=socket.socket(socket.AF_UNIX);s.settimeout(10);s.connect(sys.argv[1]);f=s.makefile("rw");f.readline()
f.write('{"execute":"qmp_capabilities"}\n');f.flush();f.readline()
for cmd in sys.argv[2:]:
    f.write(json.dumps({"execute":"human-monitor-command","arguments":{"command-line":cmd}})+"\n");f.flush()
    print(json.loads(f.readline()).get("return",""))
