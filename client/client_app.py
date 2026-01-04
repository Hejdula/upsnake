import sys
import socket
import threading
import time
from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, 
                             QHBoxLayout, QLabel, QLineEdit, QPushButton, 
                             QStackedWidget, QListWidget, QListWidgetItem, QMessageBox, QGridLayout, QFrame)
from PyQt6.QtCore import Qt, pyqtSignal, QObject, QTimer
from PyQt6.QtGui import QPainter, QColor, QBrush, QKeyEvent

SERVER_TIMEOUT_SEC = 5
DISCONNECT_TIMEOUT_SEC = 9
RECONNECT_ATTEMPTS = 3
RECONNECT_RETRY_DELAY = 2
TIMEOUT_CHECK_MILIS = 1000
GRID_SIZE = 10
DIRECTION_MAP = {
    'U': (0, -1),
    'D': (0, 1),
    'L': (-1, 0),
    'R': (1, 0)
}

class GameState:
    def __init__(self):
        self.apple = (0, 0)
        self.players = {} # nick -> {'body': [(x,y), ...], 'alive': bool}
        self.my_nick = ""
        self.grid_size = GRID_SIZE
        self.waiting_for = [] # List of nicks server is waiting for
        self.last_game_result = ""

class NetworkWorker(QObject):
    msg_received = pyqtSignal(str)
    disconnected = pyqtSignal()
    reconnect = pyqtSignal()
    error_occurred = pyqtSignal(str)
    connection_unstable = pyqtSignal()
    connection_recovered = pyqtSignal()

    def __init__(self):
        super().__init__()
        self.socket = None
        self.running = False
        self.should_reconnect = True
        self.last_msg_time = 0
        self.timeout_timer = QTimer()
        self.timeout_timer.timeout.connect(self.check_timeout)
        self.timeout_timer.start(1000)

    def connect_to_server(self, ip, port):
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((ip, port))
            self.running = True
            self.last_msg_time = time.time()
            threading.Thread(target=self.receive_loop, daemon=True).start()
            
            return True
        except Exception as e:
            self.error_occurred.emit(str(e))
            return False

    def check_timeout(self):
        if self.running:
            delta = time.time() - self.last_msg_time
            if delta > DISCONNECT_TIMEOUT_SEC:
                self.error_occurred.emit("Connection lost (Timeout)")
                self.disconnect()
            elif delta > SERVER_TIMEOUT_SEC:
                self.connection_unstable.emit()

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
        if self.should_reconnect:
            self.reconnect.emit()
        else:
            self.disconnected.emit()

    def receive_loop(self):
        buffer = ""
        while self.running and self.socket:
            try:
                data = self.socket.recv(4096)
                if not data:
                    self.disconnect()
                    break
                
                self.last_msg_time = time.time()
                self.connection_recovered.emit()
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

class InternetIssuesWidget(QWidget):
    def __init__(self):
        super().__init__()
        layout = QVBoxLayout()
        label = QLabel("Internet Issues...\nWaiting for server...")
        label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        font = label.font()
        font.setPointSize(16)
        label.setFont(font)
        layout.addWidget(label)
        self.setLayout(layout)

class ReconnectWidget(QWidget):
    cancel_reconnect = pyqtSignal()
    
    def __init__(self):
        super().__init__()
        layout = QVBoxLayout()
        self.status_label = QLabel("Connection lost.\nReconnecting...")
        self.status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        font = self.status_label.font()
        font.setPointSize(16)
        self.status_label.setFont(font)
        
        self.cancel_btn = QPushButton("Cancel")
        self.cancel_btn.clicked.connect(self.cancel_reconnect.emit)
        
        layout.addWidget(self.status_label)
        layout.addWidget(self.cancel_btn)
        self.setLayout(layout)
        
    def update_status(self, attempt, max_attempts):
        self.status_label.setText(f"Connection lost.\nReconnecting... ({attempt}/{max_attempts})")

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

        if not nick:
            QMessageBox.warning(self, "Error", "Nickname is required")
            return

        try:
            port = int(self.port_input.text())
            if not (1 <= port <= 65535):
                raise ValueError()
            socket.inet_aton(ip)

        except ValueError:
            QMessageBox.warning(self, "Error", "Port must be a number between 1 and 65535")
            return
        except socket.error:
            QMessageBox.warning(self, "Error", "Invalid IP address")
            return
            
        self.login_request.emit(nick, ip, port)

class RoomListWidget(QWidget):
    join_room = pyqtSignal(int)
    refresh_request = pyqtSignal()
    quit = pyqtSignal()

    def __init__(self):
        super().__init__()
        layout = QVBoxLayout()
        
        self.list_widget = QListWidget()
        self.refresh_btn = QPushButton("Refresh")
        self.refresh_btn.clicked.connect(self.refresh_request.emit)
        
        self.join_btn = QPushButton("Join Selected Room")
        self.join_btn.clicked.connect(self.on_join)

        self.quit_btn = QPushButton("Disconnect")
        self.quit_btn.clicked.connect(self.quit.emit)
        
        layout.addWidget(QLabel("Available Rooms"))
        layout.addWidget(self.list_widget)
        layout.addWidget(self.join_btn)
        layout.addWidget(self.refresh_btn)
        layout.addWidget(self.quit_btn)
        
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

    def __init__(self, game_state):
        super().__init__()
        self.game_state = game_state
        layout = QVBoxLayout()
        
        self.players_list = QListWidget()
        
        self.start_btn = QPushButton("Start Game")
        self.start_btn.clicked.connect(self.start_game.emit)
        
        self.leave_btn = QPushButton("Leave Room")
        self.leave_btn.clicked.connect(self.leave_room.emit)
        
        layout.addWidget(QLabel("Lobby"))
        layout.addWidget(self.players_list)
        layout.addWidget(self.start_btn)
        layout.addWidget(self.leave_btn)
        
        self.setLayout(layout)

    def update_players(self, players):
        self.players_list.clear()
        for p in players:
            item = QListWidgetItem(p)
            if p == self.game_state.my_nick: 
                item.setForeground(QBrush(QColor("green")))
            self.players_list.addItem(item)

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
        
        painter.setBrush(QBrush(QColor('red')))
        painter.drawEllipse(int(ax * cell_w), int(ay * cell_h), int(cell_w), int(cell_h))
        
        # Draw Players
        for nick, data in self.game_state.players.items():
            color = QColor("green") if nick == self.game_state.my_nick else QColor("red")
            painter.setBrush(QBrush(color))
            
            if data['alive']:
                for i, (bx, by) in enumerate(data['body']):
                    # Head is slightly different
                    if i == 0:
                        painter.setBrush(QBrush(color.lighter(130)))
                    else:
                        painter.setBrush(QBrush(color))
                    
                    painter.drawRect(int(bx * cell_w), int(by * cell_h), int(cell_w), int(cell_h))
            else:
                painter.setBrush(QBrush(QColor("grey")))
                for i, (bx, by) in enumerate(data['body']):
                    painter.drawRect(int(bx * cell_w), int(by * cell_h), int(cell_w), int(cell_h))

        
        # last game result and waiting for label
        painter.setPen(QColor('yellow'))
        font = painter.font()
        font.setPointSize(24)
        font.setBold(True)
        painter.setFont(font)

        if self.game_state.last_game_result:
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, self.game_state.last_game_result)
        elif self.game_state.waiting_for:
            text = "Waiting for: " + ", ".join(self.game_state.waiting_for)
            painter.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, text)

class GameWidget(QWidget):
    move_request = pyqtSignal(str)
    leave_game = pyqtSignal()

    def __init__(self, game_state):
        super().__init__()
        self.game_state = game_state
        layout = QHBoxLayout()
        
        self.board = GameBoard(game_state)
        self.lobby = LobbyWidget(game_state)
        
        layout.addWidget(self.lobby, 1)
        layout.addWidget(self.board, 3)
        
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
        self.setWindowTitle("Snake")
        self.resize(600, 600)
        
        self.network = NetworkWorker()
        self.network.msg_received.connect(self.handle_message)
        self.network.disconnected.connect(self.handle_disconnect)
        self.network.reconnect.connect(self.attempt_reconnect)
        self.network.error_occurred.connect(self.handle_error)
        self.network.connection_unstable.connect(self.handle_unstable)
        self.network.connection_recovered.connect(self.handle_recovered)
        
        self.game_state = GameState()
        
        self.stack = QStackedWidget()
        
        self.login_widget = LoginWidget()
        self.login_widget.login_request.connect(self.connect_to_server)
        
        self.room_list_widget = RoomListWidget()
        self.room_list_widget.join_room.connect(self.join_room)
        self.room_list_widget.refresh_request.connect(self.refresh_rooms)
        self.room_list_widget.quit.connect(self.disconnect_from_server)
        
        self.game_widget = GameWidget(self.game_state)
        self.game_widget.lobby.start_game.connect(self.start_game)
        self.game_widget.lobby.leave_room.connect(self.leave_room)
        self.game_widget.move_request.connect(self.send_move)
        
        self.internet_issues_widget = InternetIssuesWidget()
        self.reconnect_widget = ReconnectWidget()
        self.reconnect_widget.cancel_reconnect.connect(self.cancel_reconnect)
        
        self.stack.addWidget(self.login_widget)
        self.stack.addWidget(self.room_list_widget)
        self.stack.addWidget(self.game_widget)
        self.stack.addWidget(self.internet_issues_widget)
        self.stack.addWidget(self.reconnect_widget)
        
        self.setCentralWidget(self.stack)

        self._last_move = None
        self.reconnect_attempt = 0
        self.last_active_widget = self.login_widget

    def connect_to_server(self, nick, ip, port):
        self.game_state.my_nick = nick
        if self.network.connect_to_server(ip, port):
            self.network.send(f"NICK {nick}")

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
    
    def send_move(self, direction):
        # if direction != self._last_move:
        self.network.send(f"MOVE {direction}")
            # self._last_move = direction

    def disconnect_from_server(self):
        self.network.should_reconnect = False
        self.network.disconnect()

    def handle_disconnect(self):
        self.stack.setCurrentWidget(self.login_widget)
        QMessageBox.information(self, "Disconnect", "Disconnected from server")

    def handle_unstable(self):
        if self.stack.currentWidget() != self.internet_issues_widget and self.stack.currentWidget() != self.reconnect_widget:
            self.last_active_widget = self.stack.currentWidget()
            self.stack.setCurrentWidget(self.internet_issues_widget)
            
    def handle_recovered(self):
        if self.stack.currentWidget() == self.internet_issues_widget:
            self.stack.setCurrentWidget(self.last_active_widget)

    def cancel_reconnect(self):
        self.network.should_reconnect = False
        self.network.disconnect()
        self.handle_disconnect()

    def attempt_reconnect(self):
        self.stack.setCurrentWidget(self.reconnect_widget)
        
        if self.reconnect_attempt >= RECONNECT_ATTEMPTS:
            self.network.should_reconnect = False
            self.handle_disconnect()
            QMessageBox.warning(self, "Error", "Could not reconnect to server.")
            self.reconnect_attempt = 0
            return

        self.reconnect_attempt += 1
        self.reconnect_widget.update_status(self.reconnect_attempt, RECONNECT_ATTEMPTS)
        
        nick = self.game_state.my_nick
        ip = self.login_widget.ip_input.text()
        try:
            port = int(self.login_widget.port_input.text())
        except:
            port = 8888
            
        if self.network.connect_to_server(ip, port):
            print("Reconnected!")
            self.network.send(f"NICK {nick}")
            self.reconnect_attempt = 0
            if self.last_active_widget == self.room_list_widget:
                self.network.send("LIST")
        else:
            print("Reconnect failed, retrying...")
            QTimer.singleShot(RECONNECT_RETRY_DELAY * 1000, self.attempt_reconnect)

    def handle_error(self, msg):
        if self.stack.currentWidget() != self.reconnect_widget and msg != "Connection lost (Timeout)":
            QMessageBox.warning(self, "Error", msg)

    def handle_message(self, msg):
        if self.stack.currentWidget() == self.internet_issues_widget:
             self.stack.setCurrentWidget(self.last_active_widget)

        print(f"IN: {msg}")
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
            self.stack.setCurrentWidget(self.room_list_widget)
                
        elif cmd == "LOBY":
            # LOBY <nick1> <nick2> ...
            players = tokens[1:]
            self.game_widget.lobby.update_players(players)
            self.stack.setCurrentWidget(self.game_widget)
                
        elif cmd == "FULL":
            QMessageBox.information(self, "Could not join", "Room is full!")
            
        elif cmd == "STRT":
            if tokens[1] == "FAIL":
                QMessageBox.information(self, "Could not start", "Game active or not enough players")
            pass
            
        elif cmd == "WAIT":
            # WAIT <nick1> <nick2> ...
            self.game_state.waiting_for = tokens[1:]
            self.game_widget.board.update()
            
        elif cmd == "LEFT":
            # We left the room
            pass
            
        elif cmd == "TICK":
            self.game_state.last_game_result = ""
            self.game_state.waiting_for = [] # Clear waiting status on new tick
            self.parse_game_state(tokens[1:])
            if self.stack.currentWidget() != self.game_widget:
                self.stack.setCurrentWidget(self.game_widget)
                self.game_widget.setFocus() # Ensure keyboard focus
            self.game_widget.board.update()
            # Send TACK to acknowledge receipt
            self.network.send("TACK")
        elif cmd == "WINS":
            self.game_state.last_game_result = "Player " + tokens[1] + " won!"
            self.game_widget.board.update()
        elif cmd == "DRAW":
            self.game_state.last_game_result = "Draw!"
            self.game_widget.board.update()
            pass

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
                
                for char in dirs_str[1:]: # Skip 'H'
                    dx, dy = DIRECTION_MAP[char]
                    
                    prev_x = curr[0] + dx
                    prev_y = curr[1] + dy
                    body.append((prev_x, prev_y))
                    curr = (prev_x, prev_y)

                self.game_state.players[nick] = {
                    'body': body, 
                    'alive': True if dirs_str[0] == 'H' else False
                    }
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
