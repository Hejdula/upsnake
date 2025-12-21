import socket

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(("127.0.0.1", 8888))

print("Connected to server!")
try:
	while True:
		msg = input("Enter message (type 'exit' to quit): ")
		if msg.lower() == 'exit':
			break

		try:
			formatted = f"SNK {msg}|"
			s.sendall(formatted.encode())
		except BrokenPipeError:
			print("Server disconnected (broken pipe)")
			break
		except Exception as e:
			print(f"Send error: {e}")
			break

		# try:
		# 	data = s.recv(4096)
		# 	if not data:
		# 		print("Server closed connection.")
		# 		break
		# 	print("Server:", data.decode(errors='replace'))
		# except Exception as e:
		# 	print(f"Receive error: {e}")
		# 	break
finally:
	s.close()
