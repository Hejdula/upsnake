import socket
import threading

def receive_messages(sock):
    buffer = ""
    while True:
        try:
            data = sock.recv(4096)
            if not data:
                print("Server closed connection.")
                break
            
            buffer += data.decode(errors='replace')
            
            while '|' in buffer:
                message, buffer = buffer.split('|', 1)
                if message == "PING":
                    print("Received PING, sending PONG")
                    sock.sendall("PONG|".encode())
                else:
                    print(f"Server: {message}")
                    
        except Exception as e:
            print(f"Receive error: {e}")
            break

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(("127.0.0.1", 8888))

print("Connected to server!")

# Start receiving thread
threading.Thread(target=receive_messages, args=(s,), daemon=True).start()

try:
	while True:
		msg = input()
		if msg.lower() == 'exit':
			break

		try:
			formatted = f"{msg.upper()}|"
			s.sendall(formatted.encode())
		except BrokenPipeError:
			print("Server disconnected (broken pipe)")
			break
		except Exception as e:
			print(f"Send error: {e}")
			break
finally:
	s.close()
