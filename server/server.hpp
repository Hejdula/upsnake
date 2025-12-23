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

struct Position {
  int x;
  int y;

  Position operator+(const Position &other) const {
    return {x + other.x, y + other.y};
  }
  bool operator==(const Position &other) const {
    return x == other.x && y == other.y;
  }
};

class Player {
public:
  std::string nickname;
  Direction dir;
  bool alive;
  int apples;
  std::deque<Position> body;
  Player(const std::string &nickname) : nickname(nickname) {}
};

class Game {
  std::array<std::array<bool, GRID_SIZE>, GRID_SIZE>grid;
  std::array<Position, Direction::DIRECTION_COUNT> dir_to_pos;
public:
  std::list<Player *> players;
  bool active;
  Position apple;
  Game() : active(false) {
    grid.fill({});
    dir_to_pos = {
      Position{0, 1}, // UP
      Position{0, -1},  // DOWN
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
  Connection(int socket, int timer_fd, sockaddr_in addr);
  std::string get_name();

  int socket;
  sockaddr_in addr;
  std::string buff;
  Player *player;
  std::chrono::steady_clock::time_point last_active;
  int timer_fd;
};

class Server;
class Server {
public:
  Server(int port, const std::string &ip_address);
  int serve();
  int add_fd_to_epoll(int sock);
  int process_message(Connection &conn, std::string msg);
  void handle_timer();
  void handle_socket_read(int sock_fd);
  void handle_new_connection();
  void setup();
  void close_connection(int sock_fd);
  static int set_nonblocking(int sockfd);

  // Members
  int port;
  int server_socket;
  int epoll_fd;
  int global_timer_fd;
  std::string ip_address;
  sockaddr_in server_addr;
  struct epoll_event event, events[10];
  std::vector<Game> rooms;
  std::mutex rooms_mutex;
  std::vector<std::unique_ptr<Player>> players;
  std::mutex players_mutex;
  std::unordered_map<int, std::unique_ptr<Connection>> connections;
};

#endif // SERVER_HPP
