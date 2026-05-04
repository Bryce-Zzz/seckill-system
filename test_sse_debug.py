#!/usr/bin/env python3
import socket, select, time

HOST = "127.0.0.1"
PORT = 9001
USER = "debug_user"

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect((HOST, PORT))
print("connected")

req = f"GET /sse?user_id={USER} HTTP/1.1\r\nHost: {HOST}:{PORT}\r\nAccept: text/event-stream\r\nConnection: keep-alive\r\n\r\n"
s.sendall(req.encode())
print("request sent")

# 用 select 等数据
buf = b""
deadline = time.time() + 8
while time.time() < deadline:
    rdy = select.select([s], [], [], 1.0)
    print(f"select: ready={bool(rdy[0])}")
    if rdy[0]:
        chunk = s.recv(4096)
        print(f"recv: {len(chunk)} bytes")
        buf += chunk
        if b"\r\n\r\n" in buf:
            print("header end found!")
            print("Data so far:", repr(buf[:500]))
            break
    else:
        print("waiting...")

s.close()
