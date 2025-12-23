#include "server.hpp"
#include "protocol.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
#include <vector>

#define NUMBER_OF_ROOMS 4
#define MAX_EVENTS 10
#define TIMEOUT 15
#define ALIVE_CHECK_INTERVAL 4
#define MAX_PLAYERS_IN_ROOM 2

void Game::print() {
  // ANSI color codes for up to 6 players
  const char* colors[] = {"\033[31m", "\033[32m", "\033[33m", "\033[34m", "\033[35m", "\033[36m"};
  const char* reset = "\033[0m";
  char field[GRID_SIZE][GRID_SIZE];
  // Fill field with empty
  for (int y = 0; y < GRID_SIZE; ++y)
    for (int x = 0; x < GRID_SIZE; ++x)
      field[y][x] = '.';

  // Place apple
  field[apple.y][apple.x] = 'A';

  // Place snakes
  int pid = 0;
  for (Player* player : players) {
    if (!player->alive) continue;
    for (const Position& part : player->body) {
      field[part.y][part.x] = '0' + pid;
    }
    pid++;
  }

  // Print field with colors
  for (int y = 0; y < GRID_SIZE; ++y) {
    for (int x = 0; x < GRID_SIZE; ++x) {
      char c = field[y][x];
      if (c == 'A') {
        std::cout << "\033[41mA" << reset;
      } else if (c >= '0' && c <= '5') {
        std::cout << colors[c - '0'] << c << reset;
      } else {
        std::cout << c;
      }
    }
    std::cout << std::endl;
  }
};

bool Game::is_empty(Position pos) {
  bool tile_is_full = false;
  for (Player *player : players) {
    for (auto part : player->body) {
      if (pos.x == part.x && pos.y == part.y) {
        tile_is_full = true;
      }
    }
  }
  return tile_is_full;
}

bool Game::slither() {
  // are there enought players for the game to continue?
  if (std::count_if(this->players.begin(), this->players.end(),
                    [](Player *p) { return p->alive; }) < 2) {
    return false;
  }

  // newly occupied tiles
  std::vector<Position> snake_heads;

  // advance the snakes, kill if out of bounds
  for (Player *player : this->players) {
    if (!player->alive)
      continue;
    Position pos = player->body.front() + Game::dir_to_pos[player->dir];
    snake_heads.push_back(pos);
    if (pos.x < 0 || pos.x >= GRID_SIZE || pos.y < 0 || pos.y >= GRID_SIZE){
      player->alive = false;
    } else {
      player->body.push_front(pos);
    }
  }

  // check for colisions
  for (Player *player : this->players) {
    Position pos = player->body.front();
    if (grid[pos.y][pos.x]) {
      player->alive = false;
    };
  }

  // check for head to head colisions
  for (Player *outer : this->players) {
    if (!outer->alive)
      continue;
    for (Player *inner : this->players) {
      if (outer != inner && inner->alive &&
          outer->body.front() == inner->body.front()) {
        outer->alive = false;
        inner->alive = false;
      }
    }
  }

  // set colision tiles under new heads
  for (Position head : snake_heads) {
    grid[head.y][head.x] = true;
  }

  // remove tail of snakes who did not eat the apple
  bool apple_eaten = false;
  for (Player *player : this->players) {
    if (player->alive && player->body.front() == this->apple) {
      player->apples++;
      apple_eaten = true;
    } else {
      Position pos = player->body.back();
      grid[pos.y][pos.x] = false;
      player->body.pop_back();
    }
  }

  // respawn the apple if eaten
  if (apple_eaten) {
    this->apple = random_empty_tile();
  }

  // check if game ended after this tick
  if (std::count_if(this->players.begin(), this->players.end(),
                    [](Player *p) { return p->alive; }) < 2) {
    return false;
  }

  return true;
}

Position Game::random_empty_tile() {
  Position pos;
  do {
    pos = {std::rand() % GRID_SIZE, std::rand() % GRID_SIZE};
  } while (!is_empty(pos));
  return pos;
};

int Game::hatch() {
  if (this->players.size() < 2 || this->active) {
    return 0;
  }

  for (Player *player : this->players) {
    Position pos = Game::random_empty_tile();
    player->dir = static_cast<Direction>(std::rand() % 4);
    player->body.push_front(pos);
    grid[pos.y][pos.x] = true;
    player->alive = true;
  }

  Position pos = Game::random_empty_tile();
  this->active = true;
  this->apple = random_empty_tile();
  return 0;
}

std::string Game::current_move() { return ""; }
std::string Game::full_state() { return ""; }

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
    : port(port), ip_address(ip_address) {
  for (int i = 0; i < NUMBER_OF_ROOMS; i++) {
    rooms.push_back(Game());
  }
}

int Server::serve() {
  try {
    this->setup();

    if (this->add_socket_to_pool(server_socket))
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
          this->close_connection(conn);
        }
        std::cout << players.size() << std::endl;
      }
      for (int i = 0; i < event_count; i++) {
        int fd = events[i].data.fd;
        if (fd == server_socket) {
          this->handle_new_connection();
        } else if (connections.count(fd)) {
          this->handle_socket_read(fd);
        } else if (timer_to_conn.count(fd)) {
          this->handle_timer(timer_to_conn[fd]);
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
  Connection &conn = *connections[sock_fd];

  uint64_t expirations;
  ssize_t s = read(conn.timer_fd, &expirations, sizeof(expirations));
  if (s != sizeof(expirations)) {
    perror("timerfd read");
    return;
  }

  if (std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now() - conn.last_active)
          .count() > TIMEOUT) {
    this->close_connection(sock_fd);
  }

  const char *ping_msg = "PING|";
  send(conn.socket, ping_msg, strlen(ping_msg), 0);
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
    this->close_connection(sock_fd);
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
    if (this->process_message(conn, msg)) {
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
  // std::cout << "rocessing message:" << msg << std::endl;
  auto tokens = split(msg.data(), ' ');
  // for (const auto &token : tokens) {
  //   std::cout << "token: " << token << std::endl;
  // }
  if (tokens.size() < 2 || tokens[0] != "SNK") {
    this->close_connection(conn.socket);
    return 1;
  }
  msg_type type = get_msg_type(tokens[1]);
  if (type != NICK && type != PONG && !conn.player) {
    this->close_connection(conn.socket);
    return 1;
  }

  switch (type) {
  case NICK:
    if (tokens.size() != 3) {
      this->close_connection(conn.socket);
      return 1;
    }
    players.push_back(std::make_unique<Player>(tokens[2]));
    conn.player = players.back().get();
    break;
  case LIST_ROOMS: {

    if (tokens.size() != 2) {
      this->close_connection(conn.socket);
      return 1;
    }
    std::string reply = "ROOM";
    // Example: append all room names from a vector<std::string> rooms
    for (const auto &room : rooms) {
      reply += " " + std::to_string(room.players.size());
    }
    reply += "|";
    send(conn.socket, reply.c_str(), reply.size(), 0);
    break;
  }
  case JOIN: {
    if (tokens.size() != 3) {
      this->close_connection(conn.socket);
      return 1;
    }
    char *endptr = nullptr;
    int room_id = std::strtol(tokens[2].c_str(), &endptr, 10);
    if (*endptr != '\0' || room_id > NUMBER_OF_ROOMS ||
        rooms[room_id].players.size() >= MAX_PLAYERS_IN_ROOM) {
      this->close_connection(conn.socket);
      return 1;
    }

    // remove the player from current rooms
    for (auto &room : rooms) {
      auto it =
          std::find(room.players.begin(), room.players.end(), conn.player);
      if (it != room.players.end()) {
        room.players.erase(it);
      }
    }

    rooms[room_id].players.push_back(conn.player);
    std::string reply = "LOBY";
    for (auto player : rooms[room_id].players) {
      reply += " " + player->nickname;
    }
    reply += "|";
    send(conn.socket, reply.c_str(), reply.size(), 0);
    break;
  }
  case INVALID:
    break;
  case LEAVE: {
    if (tokens.size() != 2) {
      this->close_connection(conn.socket);
      return 1;
    }
    // Remove player from any room they are in
    for (auto &room : rooms) {
      auto it =
          std::find(room.players.begin(), room.players.end(), conn.player);
      if (it != room.players.end()) {
        room.players.erase(it);
      }
    }
    std::string reply = "LEFT|";
    send(conn.socket, reply.c_str(), reply.size(), 0);
    break;
  }
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
  if (Server::set_nonblocking(sock) != 0) {
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