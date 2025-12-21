#ifndef SERVER_HPP
#define SERVER_HPP

#include <bits/types/error_t.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <set>
#include <string>
#include <sys/epoll.h>
#include <unordered_map>
#include <vector>
#include <list>

enum err_t {
  INVALID_MESSAGE,
};

class Player {
public:
  std::string nickname;
  Player(const std::string &nickname) : nickname(nickname) {}
};

class Game {
public:
  std::vector<Player *> players;

  int tick();
};

class Room {
public:
  std::vector<Player *> players;
  Game game;
  bool active;
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
  int add_socket_to_pool(int sock);
  int process_message(Connection& conn, std::string msg);
  void handle_timer(int fd);
  void handle_socket_read(int sock_fd);
  void handle_new_connection();
  void setup();
  void close_connection(int sock_fd);
  static int set_nonblocking(int sockfd);

  // Members
  int port;
  int server_socket;
  std::string ip_address;
  sockaddr_in server_addr;
  int epoll_fd;
  struct epoll_event event, events[10];
  std::vector<Room> rooms;
  std::mutex rooms_mutex;
  std::vector<std::unique_ptr<Player>> players;
  std::mutex players_mutex;
  std::unordered_map<int, std::unique_ptr<Connection>> connections;
  std::unordered_map<int, int> timer_to_conn;
};

#endif // SERVER_HPP
