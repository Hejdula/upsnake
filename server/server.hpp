#ifndef SERVER_HPP
#define SERVER_HPP

#include <array>
#include <bits/types/error_t.h>
#include <chrono>
#include <deque>
#include <list>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <netinet/in.h>
#include <set>
#include <string>
#include <sys/epoll.h>
#include <unordered_map>
#include <utility>
#include <vector>

#define GRID_SIZE 10
#define INITIAL_SNAKE_LENGTH 3

enum err_t {
  INVALID_MESSAGE,
};

enum Direction {
  UP,
  DOWN,
  LEFT,
  RIGHT,
  DIRECTION_COUNT,
};

inline std::string dir_to_string(Direction dir) {
  switch (dir) {
  case UP:
    return "U";
  case DOWN:
    return "D";
  case LEFT:
    return "L";
  case RIGHT:
    return "R";
  default:
    return "?";
  }
}

struct Position {
  int x;
  int y;

  Position operator+(const Position &other) const {
    return {x + other.x, y + other.y};
  }
  Position operator-(const Position &other) const {
    return {x - other.x, y - other.y};
  }
  bool operator==(const Position &other) const {
    return x == other.x && y == other.y;
  }
};

class Player {
public:
  std::string nickname;
  Direction dir;
  Direction last_move_dir;
  bool alive;
  bool updated;
  int apples;
  int length;
  std::deque<Position> body;
  std::chrono::steady_clock::time_point last_active;
  Player(const std::string &nickname)
      : nickname(nickname), last_move_dir(DIRECTION_COUNT),
        length(INITIAL_SNAKE_LENGTH) {}
};

class Game {
  std::array<std::array<bool, GRID_SIZE>, GRID_SIZE> grid;
  std::array<Position, Direction::DIRECTION_COUNT> dir_to_pos;

public:
  std::list<Player *> players;
  bool active;
  bool waiting;
  Position apple;
  Game() : active(false) {
    grid.fill({});
    dir_to_pos = {
        Position{0, 1},  // UP
        Position{0, -1}, // DOWN
        Position{-1, 0}, // LEFT
        Position{1, 0},  // RIGHT
    };
  }

  bool is_empty(Position pos);
  void print();
  int hatch();
  bool slither();
  Position random_empty_tile();
  std::string current_move();
  std::string full_state();
};

class Connection {
public:
  Connection(int socket, sockaddr_in addr);
  std::string get_name();

  int socket;
  sockaddr_in addr;
  std::string buff;
  Player *player;
  std::chrono::steady_clock::time_point last_active;
};

class Server;
class Server {
public:
  Server(int port, const std::string &ip_address);
  int serve();
  int add_fd_to_epoll(int sock);
  int process_message(Connection &conn, std::string msg);
  void handle_timer();
  void handle_game_tick();
  void handle_socket_read(int sock_fd);
  void handle_new_connection();
  void setup();
  void close_connection(int sock_fd);
  void broadcast_game(Game &game, std::string msg);
  static int set_nonblocking(int sockfd);

  // Members
  int port;
  int server_socket;
  int epoll_fd;
  int global_timer_fd;
  int game_timer_fd;
  std::string ip_address;
  sockaddr_in server_addr;
  struct epoll_event event, events[10];
  std::vector<Game> rooms;
  std::vector<std::unique_ptr<Player>> players;
  std::chrono::steady_clock::time_point last_ping;
  std::unordered_map<int, std::unique_ptr<Connection>> connections;
};

#endif // SERVER_HPP
