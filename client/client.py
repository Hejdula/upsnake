import socket

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(("127.0.0.1", 8888))

print("Connected to server!")
try:
	while True:
		msg = input("Enter message (type 'exit' to quit): ")
		if msg.lower() == 'exit':
			break
		s.sendall(msg.encode())
finally:
	s.close()
