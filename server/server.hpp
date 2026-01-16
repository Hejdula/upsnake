#ifndef SERVER_HPP
#define SERVER_HPP

#include "game.hpp"
#include "connection.hpp"
#include <chrono>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <unordered_map>
#include <vector>

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
