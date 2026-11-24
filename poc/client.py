#!/usr/bin/env python3

import socket,os,json


request = {"type":"query"}

with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as s:
    s.connect("/run/indexd.sock")
    s.sendall(json.dumps(request).encode())
    data = s.recv(1024)
    print(data.decode())
    s.close()