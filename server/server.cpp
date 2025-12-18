#include <algorithm>
#include <arpa/inet.h>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>
using std::mutex;
using std::string;
using std::vector;

#define NUMBER_OF_ROOMS 4
#define MAX_EVENTS 10

vector<int> clients;

class Player {
public:
  string nickname;
  Player(const string &nickname) : nickname(nickname) {}
};

class Game {
public:
  vector<Player *> players;

  int tick();
};

class Room {
public:
  vector<Player *> players;
  Game game;
  bool active;
};

class Connection {
public:
  // void close_and_cleanup(Server* server) {
  //   epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, socket, nullptr);
  //   close(socket);
  //   server->connections.erase(socket);
  // }
  int socket;
  sockaddr_in addr;
  string buff;
  Player *player;

  Connection(int socket, sockaddr_in addr)
      : socket(socket), addr(addr), buff(), player(nullptr) {}
};

class Server {
  // net
  int port;
  int server_socket;
  string ip_address;
  sockaddr_in server_addr;

  // pooling
  int epoll_fd;
  struct epoll_event event = {}, events[MAX_EVENTS];

  // state
  vector<Room> rooms;
  mutex rooms_mutex;
  vector<Player> players;
  mutex players_mutex;
  std::unordered_map<int, Connection> connections;

public:
  Server(int port, const string &ip_address)
      : port(port), ip_address(ip_address) {}

  int serve() {
    try {
      Server::setup();

      Server::add_socket_to_pool(server_socket);

      while (true) {
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < event_count; i++) {
          if (events[i].data.fd == server_socket) {
            handle_new_connection();
          } else {
            handle_socket_read(events[i].data.fd);
            // char buffer[1024];
            // ssize_t bytes_received =
            //     recv(events[i].data.fd, buffer, sizeof(buffer) - 1, 0);
            // if (bytes_received <= 0) {
            //   close(events[i].data.fd);
            // } else {
            //   buffer[bytes_received] = '\0';
            //   printf("Received: %s\n", buffer);
            // }
          }
        }
      }
    } catch (const std::exception &e) {
      std::cerr << "Server error: " << e.what() << std::endl;
      return 1;
    }
    return 0;
  }

private:
  void handle_socket_read(int sock_fd) {
    auto it = connections.find(sock_fd);
    if (it == connections.end()) {
      printf(
          "socket file descriptor not in connections, this should not happen");
      epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_fd, nullptr);
      close(sock_fd);
    }

    Connection &conn = it->second;
    string buff(1024, '\0');
    ssize_t bytes_received = recv(sock_fd, buff.data(), buff.size() - 1, 0);

    if (bytes_received <= 0) {
      epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_fd, nullptr);
      close(sock_fd);
      connections.erase(sock_fd);
    }

    std::cout << "recieved " << bytes_received << " bytes from " << conn.socket
              << std::endl;

    conn.buff.append(buff.data(), bytes_received);

    std::cout << "Current buffer: " << conn.buff << std::endl;

    size_t separator = conn.buff.find('|');
    if (separator != std::string::npos) {
      string msg = conn.buff.substr(0, separator);
      conn.buff.erase(0, separator + 1);
      std::cout << "found a whole message:" << msg << std::endl;
    }
  }

  void add_socket_to_pool(int sock) {
    set_nonblocking(sock);
    event.events = EPOLLIN;
    event.data.fd = sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &event)) {
      throw std::runtime_error("addding socket to pool");
    }
  }

  void handle_new_connection() {
    sockaddr_in client_addr = {};
    socklen_t addrlen = sizeof(client_addr);
    int client_socket =
        accept(server_socket, (sockaddr *)&client_addr, &addrlen);
    if (client_socket == -1) {
      perror("accept");
      return;
    }

    auto res = connections.emplace(client_socket,
                                   Connection(client_socket, client_addr));
    if (!res.second) {
      throw std::runtime_error("file descriptor already in connections");
    }

    add_socket_to_pool(client_socket);

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    std::cout << "Client connected from " << client_ip << ":"
              << ntohs(client_addr.sin_port) << std::endl;
  }

  void setup() {
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
      throw std::runtime_error("socket");

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip_address.c_str());

    if (bind(server_socket, (struct sockaddr *)&server_addr,
             sizeof(server_addr)))
      throw std::runtime_error("bind");

    if (listen(server_socket, 10))
      throw std::runtime_error("listen");

    std::cout << "Listening on: " << ip_address << ":" << port << std::endl;

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1)
      throw std::runtime_error("epoll_create1");
  }

  static void set_nonblocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1)
      throw std::runtime_error("fcntl(F_GETFL) failed in set_nonblocking");
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1)
      throw std::runtime_error("fcntl(F_SETFL) failed in set_nonblocking");
  }
};

int main(int argc, char **argv) {
  int port = 8888;
  if (argc > 1) {
    port = std::stoi(argv[1]);
  }
  Server *server = new Server(port, "127.0.0.1");
  server->serve();
  delete server;
}