#include "server.hpp"
#include "protocol.hpp"
#include <arpa/inet.h>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>

#define MAX_EVENTS 10
#define TIMEOUT 15
#define ALIVE_CHECK_INTERVAL 1

// Game implementation
int Game::tick() { return 0; }

#include <chrono>

Connection::Connection(int socket, int timer_fd, sockaddr_in addr)
    : socket(socket), addr(addr), player(nullptr),
      last_active(std::chrono::steady_clock::now()), timer_fd(timer_fd) {}

std::string Connection::get_name() {
  if (player) {
    return player->nickname;
  }
  char client_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(addr.sin_addr), client_ip, INET_ADDRSTRLEN);
  int client_port = ntohs(addr.sin_port);
  return std::string(client_ip) + ":" + std::to_string(client_port);
}

Server::Server(int port, const std::string &ip_address)
    : port(port), ip_address(ip_address) {}

int Server::serve() {
  try {
    setup();

    if (add_socket_to_pool(server_socket))
      throw std::runtime_error("Failed to add server socket to pool");

    while (true) {
      int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
      if (!event_count) {
        // timeout
        std::set<int> to_terminate;
        for (auto &conn : connections) {
          if (conn.first != server_socket &&
              std::chrono::duration_cast<std::chrono::seconds>(
                  std::chrono::steady_clock::now() - conn.second->last_active)
                      .count() > TIMEOUT) {
            to_terminate.insert(conn.first);
          }
        }
        for (auto conn : to_terminate) {
          close_connection(conn);
        }
        std::cout << players.size() << std::endl;
      }
      for (int i = 0; i < event_count; i++) {
        int fd = events[i].data.fd;
        if (fd == server_socket) {
          handle_new_connection();
        } else if (connections.count(fd)) {
          handle_socket_read(fd);
        } else if (timer_to_conn.count(fd)) {
          handle_timer(timer_to_conn[fd]);
        }
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "Server error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}
void Server::handle_timer(int sock_fd) {
  // Read to clear the event
  uint64_t expirations;
  ssize_t s = read(sock_fd, &expirations, sizeof(expirations));
  if (s != sizeof(expirations)) {
    perror("timerfd read");
    return;
  }
  Connection &conn = *connections[sock_fd];

  if (std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now() - conn.last_active)
          .count() > TIMEOUT) {
    close_connection(sock_fd);
  }
}

void Server::handle_socket_read(int sock_fd) {
  auto it = connections.find(sock_fd);
  if (it == connections.end()) {
    printf("socket file descriptor not in connections, this should not happen");
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_fd, nullptr);
    close(sock_fd);
    return;
  }

  Connection &conn = *(it->second);
  std::string buff(1024, '\0');
  ssize_t bytes_received = recv(sock_fd, &buff[0], buff.size(), 0);

  if (bytes_received <= 0) {
    close_connection(sock_fd);
    return;
  }

  conn.last_active = std::chrono::steady_clock::now();

  buff.resize(bytes_received);
  conn.buff.append(buff);

  std::cout << "[" << conn.get_name() << "] : " << buff << std::endl;

  size_t separator = conn.buff.find('|');
  while (separator != std::string::npos) {

    std::string msg = conn.buff.substr(0, separator);
    conn.buff.erase(0, separator + 1);
    if (process_message(conn, msg)) {
      return;
    };
    separator = conn.buff.find('|');
  }
}

std::vector<std::string> split(const char *str, char c = ' ') {
  std::vector<std::string> result;

  do {
    const char *begin = str;

    while (*str != c && *str)
      str++;

    result.push_back(std::string(begin, str));
  } while (0 != *str++);

  return result;
}

int Server::process_message(Connection &conn, std::string msg) {
  std::cout << "rocessing message:" << msg << std::endl;
  auto tokens = split(msg.data(), ' ');
  for (const auto &token : tokens) {
    std::cout << "token: " << token << std::endl;
  }
  if (tokens.size() < 2 || tokens[0] != "SNK") {
    close_connection(conn.socket);
    return 1;
  }
  msg_type type = get_msg_type(tokens[1]);
  if (type != NICK && !conn.player) {
    close_connection(conn.socket);
    return 1;
  }
  switch (type) {
  case NICK:
    if (tokens.size() != 3) {
      close_connection(conn.socket);
      return 1;
    }
    players.emplace_back(tokens[2]);
    conn.player = &players.back();
    break;
  case INVALID:
    break;
  case LEAVE:
    break;
  case PONG:
    break;
  case MOVE:
    break;
  case START:
    break;
  case QUIT:
    break;
  }

  return 0;
}

int Server::add_socket_to_pool(int sock) {
  if (set_nonblocking(sock) != 0) {
    return -1;
  }
  event.events = EPOLLIN;
  event.data.fd = sock;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &event)) {
    perror("epoll_ctl");
    return -1;
  }
  return 0;
}

void Server::close_connection(int sock_fd) {
  auto it = connections.find(sock_fd);
  if (!it->first)
    return;
  std::cout << "Closing connection with: " << it->second->get_name()
            << std::endl;
  epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_fd, nullptr);
  close(it->second->timer_fd);
  timer_to_conn.erase(it->second->timer_fd);
  close(sock_fd);
  connections.erase(sock_fd);
}

void Server::handle_new_connection() {
  sockaddr_in client_addr = {};
  socklen_t addrlen = sizeof(client_addr);

  // set up timer for client
  int timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
  if (timer_fd == -1) {
    perror("timer_fd");
    return;
  }
  itimerspec timer_spec;
  timer_spec.it_interval.tv_sec = ALIVE_CHECK_INTERVAL;
  timer_spec.it_interval.tv_nsec = 0;
  timer_spec.it_value.tv_sec = ALIVE_CHECK_INTERVAL;
  timer_spec.it_value.tv_nsec = 0;
  timerfd_settime(timer_fd, 0, &timer_spec, nullptr);

  // set up new connection
  int client_socket = accept(server_socket, (sockaddr *)&client_addr, &addrlen);
  if (client_socket == -1) {
    perror("accept");
    return;
  }
  if (set_nonblocking(client_socket) != 0) {
    std::cerr << "Failed to set non-blocking for client" << std::endl;
    close(client_socket);
    return;
  }
  auto res = connections.emplace(
      client_socket, std::make_unique<Connection>(
                         Connection(client_socket, timer_fd, client_addr)));
  if (!res.second) {
    std::cerr << "Error: Connection already exists for fd " << client_socket
              << std::endl;
    close(client_socket);
    return;
  }

  timer_to_conn.insert({timer_fd, client_socket});

  // add fds to pool
  if (add_socket_to_pool(client_socket) || add_socket_to_pool(timer_fd)) {
    throw std::runtime_error("Could not add to epoll pool");
    // std::cerr << "Failed to add client to pool" << std::endl;
    // close(client_socket);
    // connections.erase(client_socket);
    // return;
  }

  std::cout << "Client connected: " << res.first->second->get_name()
            << std::endl;
}

void Server::setup() {
  server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket == -1)
    throw std::runtime_error("socket");

  int opt = 1;
  if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt))) {
    perror("setsockopt");
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  server_addr.sin_addr.s_addr = inet_addr(ip_address.c_str());

  if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)))
    throw std::runtime_error("bind");

  if (listen(server_socket, 10))
    throw std::runtime_error("listen");

  std::cout << "Listening on: " << ip_address << ":" << port << std::endl;

  epoll_fd = epoll_create1(0);
  if (epoll_fd == -1)
    throw std::runtime_error("epoll_create1");
}

int Server::set_nonblocking(int sockfd) {
  int flags = fcntl(sockfd, F_GETFL, 0);
  if (flags == -1) {
    perror("fcntl(F_GETFL)");
    return -1;
  }
  if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl(F_SETFL)");
    return -1;
  }
  return 0;
}

int main(int argc, char **argv) {
  int port = 8888;
  if (argc > 1) {
    port = std::stoi(argv[1]);
  }
  Server server(port, "127.0.0.1");
  server.serve();
}