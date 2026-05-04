import socket, time

s = socket.socket()
s.settimeout(5)
s.connect(('127.0.0.1', 9001))
s.sendall(b'GET /sse?user_id=pytest HTTP/1.1\r\nHost: 127.0.0.1:9001\r\nAccept: text/event-stream\r\nConnection: keep-alive\r\n\r\n')
time.sleep(0.3)
data = s.recv(4096)
print("GOT:", repr(data[:300]))
s.close()
