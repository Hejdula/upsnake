import sys
import socket
import threading
import time
from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                             QHBoxLayout, QLabel, QLineEdit, QPushButton, 
                             QStackedWidget, QListWidget, QMessageBox, QGridLayout, QFrame)
from PyQt6.QtCore import Qt, pyqtSignal, QObject, QTimer
from PyQt6.QtGui import QPainter, QColor, QBrush, QKeyEvent

# --- Constants ---
GRID_SIZE = 10
DIRECTION_MAP = {
    'U': (0, 1),
    'D': (0, -1),
    'L': (-1, 0),
    'R': (1, 0)
}
# Mapping from integer dir (server) to string char
INT_TO_DIR = ['U', 'D', 'L', 'R']

class GameState:
    def __init__(self):
        self.apple = (0, 0)
        self.players = {} # nick -> {'body': [(x,y), ...], 'color': QColor}
        self.my_nick = ""
        self.grid_size = GRID_SIZE
        self.colors = [QColor('red'), QColor('green'), QColor('blue'), 
                       QColor('yellow'), QColor('magenta'), QColor('cyan')]
        self.waiting_for = [] # List of nicks server is waiting for

    def get_color(self, nick):
        hash_val = sum(ord(c) for c in nick)
        return self.colors[hash_val % len(self.colors)]

class NetworkWorker(QObject):
    msg_received = pyqtSignal(str)
    disconnected = pyqtSignal()
    error_occurred = pyqtSignal(str)

    def __init__(self):
        super().__init__()
        self.socket = None
        self.running = False

    def connect_to_server(self, ip, port):
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((ip, port))
            self.running = True
            threading.Thread(target=self.receive_loop, daemon=True).start()
            return True
        except Exception as e:
            self.error_occurred.emit(str(e))
            return False

    def send(self, msg):
        if self.socket:
            try:
                self.socket.sendall((msg + "|").encode())
            except Exception as e:
                self.error_occurred.emit(str(e))
                self.disconnect()

    def disconnect(self):
        self.running = False
        if self.socket:
            try:
                self.socket.close()
            except:
                pass
            self.socket = None
        self.disconnected.emit()

    def receive_loop(self):
        buffer = ""
        while self.running and self.socket:
            try:
                data = self.socket.recv(4096)
                if not data:
                    self.disconnect()
                    break
                
                buffer += data.decode(errors='replace')
                
                while '|' in buffer:
                    message, buffer = buffer.split('|', 1)
                    if message:
                        self.msg_received.emit(message)
            except Exception as e:
                if self.running:
                    self.error_occurred.emit(str(e))
                    self.disconnect()
                break

class LoginWidget(QWidget):
    login_request = pyqtSignal(str, str, int) # nick, ip, port

    def __init__(self):
        super().__init__()
        layout = QVBoxLayout()
        
        self.nick_input = QLineEdit()
        self.nick_input.setPlaceholderText("Nickname")
        
        self.ip_input = QLineEdit("127.0.0.1")
        self.ip_input.setPlaceholderText("IP Address")
        
        self.port_input = QLineEdit("8888")
        self.port_input.setPlaceholderText("Port")
        
        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self.on_connect)
        
        layout.addWidget(QLabel("Snake Game Login"))
        layout.addWidget(self.nick_input)
        layout.addWidget(self.ip_input)
        layout.addWidget(self.port_input)
        layout.addWidget(self.connect_btn)
        layout.addStretch()
        
        self.setLayout(layout)

    def on_connect(self):
        nick = self.nick_input.text()
        ip = self.ip_input.text()
        try:
            port = int(self.port_input.text())
        except ValueError:
            QMessageBox.warning(self, "Error", "Port must be a number")
            return
            
        if not nick:
            QMessageBox.warning(self, "Error", "Nickname is required")
            return
            
        self.login_request.emit(nick, ip, port)

class RoomListWidget(QWidget):
    join_room = pyqtSignal(int)
    refresh_request = pyqtSignal()

    def __init__(self):
        super().__init__()
        layout = QVBoxLayout()
        
        self.list_widget = QListWidget()
        self.refresh_btn = QPushButton("Refresh")
        self.refresh_btn.clicked.connect(self.refresh_request.emit)
        
        self.join_btn = QPushButton("Join Selected Room")
        self.join_btn.clicked.connect(self.on_join)
        
        layout.addWidget(QLabel("Available Rooms"))
        layout.addWidget(self.list_widget)
        layout.addWidget(self.refresh_btn)
        layout.addWidget(self.join_btn)
        
        self.setLayout(layout)

    def update_rooms(self, sizes):
        self.list_widget.clear()
        for i, size in enumerate(sizes):
            self.list_widget.addItem(f"Room {i} - Players: {size}")

    def on_join(self):
        row = self.list_widget.currentRow()
        if row >= 0:
            self.join_room.emit(row)

class LobbyWidget(QWidget):
    start_game = pyqtSignal()
    leave_room = pyqtSignal()

    def __init__(self):
        super().__init__()
        layout = QVBoxLayout()
        
        self.players_list = QListWidget()
        
        self.start_btn = QPushButton("Start Game")
        self.start_btn.clicked.connect(self.start_game.emit)
        
        self.leave_btn = QPushButton("Leave Room")
        self.leave_btn.clicked.connect(self.leave_room.emit)
        
        layout.addWidget(QLabel("Lobby - Waiting for players"))
        layout.addWidget(self.players_list)
        layout.addWidget(self.start_btn)
        layout.addWidget(self.leave_btn)
        
        self.setLayout(layout)

    def update_players(self, players):
        self.players_list.clear()
        for p in players:
            self.players_list.addItem(p)

class GameBoard(QWidget):
    def __init__(self, game_state):
        super().__init__()
        self.game_state = game_state
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        
        w = self.width()
        h = self.height()
        cell_w = w / self.game_state.grid_size
        cell_h = h / self.game_state.grid_size
        
        # Draw grid background
        painter.fillRect(0, 0, w, h, QColor(30, 30, 30))

        # Draw grid lines
        painter.setPen(QColor(60, 60, 60))
        for i in range(self.game_state.grid_size + 1):
            # Vertical lines
            x = int(i * cell_w)
            painter.drawLine(x, 0, x, h)
            # Horizontal lines
            y = int(i * cell_h)
            painter.drawLine(0, y, w, y)
        painter.setPen(Qt.PenStyle.NoPen)
        
        # Draw Apple
        ax, ay = self.game_state.apple
        ay = -ay
        # Server coords: (0,0) bottom-left? Or top-left?
        # Assuming standard matrix: (0,0) top-left for now.
        # If server uses (0,0) as bottom-left, I need to flip Y.
        # Let's try standard top-left first.
        
        painter.setBrush(QBrush(QColor('red')))
        painter.drawEllipse(int(ax * cell_w), int(ay * cell_h), int(cell_w), int(cell_h))
        
        # Draw Players
        for nick, data in self.game_state.players.items():
            color = self.game_state.get_color(nick)
            painter.setBrush(QBrush(color))
            
            for i, (bx, by) in enumerate(data['body']):
                # Head is slightly different or just same color
                if i == 0:
                    painter.setBrush(QBrush(color.lighter(130)))
                    # Draw Nickname above head
                    painter.setPen(QColor('white'))
                    painter.drawText(int(bx * cell_w), int(by * cell_h) - 5, nick)
                    painter.setPen(Qt.PenStyle.NoPen)
                else:
                    painter.setBrush(QBrush(color))
                
                painter.drawRect(int(bx * cell_w), int(by * cell_h), int(cell_w), int(cell_h))

        # Draw Waiting Status
        if self.game_state.waiting_for:
            painter.setPen(QColor('yellow'))
            text = "Waiting for: " + ", ".join(self.game_state.waiting_for)
            painter.drawText(10, h - 10, text)

class GameWidget(QWidget):
    move_request = pyqtSignal(str)
    leave_game = pyqtSignal()

    def __init__(self, game_state):
        super().__init__()
        self.game_state = game_state
        layout = QVBoxLayout()
        
        self.board = GameBoard(game_state)
        self.info_label = QLabel("Game Started")
        
        self.leave_btn = QPushButton("Leave Game")
        self.leave_btn.clicked.connect(self.leave_game.emit)
        self.leave_btn.setFocusPolicy(Qt.FocusPolicy.NoFocus) # Keep focus on board
        
        layout.addWidget(self.info_label)
        layout.addWidget(self.board, 1)
        layout.addWidget(self.leave_btn)
        
        self.setLayout(layout)

    def keyPressEvent(self, event):
        key = event.key()
        if key == Qt.Key.Key_Up:
            self.move_request.emit('U')
        elif key == Qt.Key.Key_Down:
            self.move_request.emit('D')
        elif key == Qt.Key.Key_Left:
            self.move_request.emit('L')
        elif key == Qt.Key.Key_Right:
            self.move_request.emit('R')

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Snake Client")
        self.resize(600, 600)
        
        self.network = NetworkWorker()
        self.network.msg_received.connect(self.handle_message)
        self.network.disconnected.connect(self.handle_disconnect)
        self.network.error_occurred.connect(self.handle_error)
        
        self.game_state = GameState()
        
        self.stack = QStackedWidget()
        
        self.login_widget = LoginWidget()
        self.login_widget.login_request.connect(self.connect_to_server)
        
        self.room_list_widget = RoomListWidget()
        self.room_list_widget.join_room.connect(self.join_room)
        self.room_list_widget.refresh_request.connect(self.refresh_rooms)
        
        self.lobby_widget = LobbyWidget()
        self.lobby_widget.start_game.connect(self.start_game)
        self.lobby_widget.leave_room.connect(self.leave_room)
        
        self.game_widget = GameWidget(self.game_state)
        self.game_widget.move_request.connect(self.send_move)
        self.game_widget.leave_game.connect(self.leave_game)
        
        self.stack.addWidget(self.login_widget)
        self.stack.addWidget(self.room_list_widget)
        self.stack.addWidget(self.lobby_widget)
        self.stack.addWidget(self.game_widget)
        
        self.setCentralWidget(self.stack)

    def connect_to_server(self, nick, ip, port):
        self.game_state.my_nick = nick
        if self.network.connect_to_server(ip, port):
            self.network.send(f"NICK {nick}")
            self.network.send("LIST")
            # We don't switch view yet, wait for ROOM response

    def refresh_rooms(self):
        self.network.send("LIST")

    def join_room(self, room_id):
        self.network.send(f"JOIN {room_id}")

    def start_game(self):
        self.network.send("STRT")

    def leave_room(self):
        self.network.send("LEAV")
        self.network.send("LIST")
        self.stack.setCurrentWidget(self.room_list_widget)

    def leave_game(self):
        self.network.send("LEAV")
        self.network.send("LIST")
        self.stack.setCurrentWidget(self.room_list_widget)

    def send_move(self, direction):
        self.network.send(f"MOVE {direction}")

    def handle_disconnect(self):
        QMessageBox.critical(self, "Disconnected", "Connection lost.")
        self.stack.setCurrentWidget(self.login_widget)

    def handle_error(self, msg):
        print(f"Error: {msg}")

    def handle_message(self, msg):
        print(f"RX: {msg}")
        tokens = msg.split()
        if not tokens:
            return
            
        cmd = tokens[0]
        
        if cmd == "PING":
            self.network.send("PONG")
            
        elif cmd == "ROOM":
            # ROOM <size1> <size2> ...
            sizes = tokens[1:]
            self.room_list_widget.update_rooms(sizes)
            if self.stack.currentWidget() == self.login_widget:
                self.stack.setCurrentWidget(self.room_list_widget)
                
        elif cmd == "LOBY":
            # LOBY <nick1> <nick2> ...
            players = tokens[1:]
            self.lobby_widget.update_players(players)
            if self.stack.currentWidget() != self.lobby_widget:
                self.stack.setCurrentWidget(self.lobby_widget)
                
        elif cmd == "FULL":
            QMessageBox.warning(self, "Full", "Room is full!")
            
        elif cmd == "STRT":
            # STRT OK
            # Game starts, but we wait for state update to switch?
            # Actually server sends STRT OK then broadcasts state.
            pass
            
        elif cmd == "WAIT":
            # Game paused/waiting
            # WAIT <nick1> <nick2> ...
            self.game_state.waiting_for = tokens[1:]
            self.game_widget.board.update()
            
        elif cmd == "LEFT":
            # We left the room
            pass
            
        # Game State Handling
        # Check if it's a game state message (starts with TICK)
        elif cmd == "TICK":
            self.game_state.waiting_for = [] # Clear waiting status on new tick
            self.parse_game_state(tokens[1:]) # Skip TICK token
            if self.stack.currentWidget() != self.game_widget:
                self.stack.setCurrentWidget(self.game_widget)
                self.game_widget.setFocus() # Ensure keyboard focus
            self.game_widget.board.update()
            # Send TACK to acknowledge receipt
            self.network.send("TACK")

    def parse_game_state(self, tokens):
        try:
            # Format: apple_x apple_y ...
            if len(tokens) < 2: return
            
            ax = int(tokens[0])
            ay = int(tokens[1])
            
            self.game_state.apple = (ax, ay)
            
            idx = 2
            updated_players = set()
            
            while idx < len(tokens):
                nick = tokens[idx]
                idx += 1
                if idx >= len(tokens): break
                
                val1 = tokens[idx]
                idx += 1
                
                # Always Full State: Nick X Y H...
                hx = int(val1)
                hy = int(tokens[idx])
                idx += 1 # consumed Y
                
                dirs_str = tokens[idx]
                idx += 1 # consumed H...
                
                # Reconstruct body
                body = [(hx, hy)]
                curr = (hx, hy)
                
                # Directions in H... are from TAIL to HEAD (based on server logic)
                # So to go backwards from HEAD, we subtract the direction
                for char in dirs_str[1:]: # Skip 'H'
                    dx, dy = 0, 0
                    if char == 'U': dx, dy = 0, 1
                    elif char == 'D': dx, dy = 0, -1
                    elif char == 'L': dx, dy = -1, 0
                    elif char == 'R': dx, dy = 1, 0
                    
                    prev_x = curr[0] - dx
                    prev_y = curr[1] - dy
                    body.append((prev_x, prev_y))
                    curr = (prev_x, prev_y)
                    
                self.game_state.players[nick] = {'body': body}
                updated_players.add(nick)

            # Remove stale players
            current_nicks = list(self.game_state.players.keys())
            for n in current_nicks:
                if n not in updated_players:
                    del self.game_state.players[n]
                    
        except Exception as e:
            print(f"Error parsing game state: {e}")

if __name__ == '__main__':
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())
