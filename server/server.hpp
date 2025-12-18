#ifndef SERVER_HPP
#define SERVER_HPP

#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <unordered_map>
#include <vector>

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
  int socket;
  sockaddr_in addr;
  std::string buff;
  Player *player;

  Connection(int socket, sockaddr_in addr)
      : socket(socket), addr(addr), buff(), player(nullptr) {}
};

class Server;
class Server {
public:
  Server(int port, const std::string &ip_address);
  int serve();
  void add_socket_to_pool(int sock);
  void handle_new_connection();
  void handle_socket_read(int sock_fd);
  void setup();
  static void set_nonblocking(int sockfd);

  // Members
  int port;
  int server_socket;
  std::string ip_address;
  sockaddr_in server_addr;
  int epoll_fd;
  struct epoll_event event, events[10];
  std::vector<Room> rooms;
  std::mutex rooms_mutex;
  std::vector<Player> players;
  std::mutex players_mutex;
  std::unordered_map<int, Connection> connections;
};

#endif // SERVER_HPP
