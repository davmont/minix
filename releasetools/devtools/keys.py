import socket,json,sys,time
q=sys.argv[1]
def send(keys):
    s=socket.socket(socket.AF_UNIX); s.settimeout(5); s.connect(q); f=s.makefile("rw"); f.readline()
    for m in ['{"execute":"qmp_capabilities"}', json.dumps({"execute":"send-key","arguments":{"keys":[{"type":"qcode","data":k} for k in keys]}})]:
        f.write(m+"\n"); f.flush(); f.readline()
    f.close(); s.close()
for arg in sys.argv[2:]:
    send(arg.split("+")); time.sleep(1.5)
