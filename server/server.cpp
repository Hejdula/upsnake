#include "server.hpp"
#include "protocol.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <type_traits>
#include <unistd.h>
#include <vector>

#define NUMBER_OF_ROOMS 4
#define MAX_EVENTS 10
#define PLAYER_REMOVAL_TIMEOUT 60
#define CONNECTION_TIMEOUT 10
#define GLOBAL_TIMER_CHECK 1
#define GAME_SPEED 1
#define PING_INTERVAL 2
#define MAX_PLAYERS_IN_ROOM 4

Server::Server(int port, const std::string &ip_address)
    : port(port), ip_address(ip_address),
      last_ping(std::chrono::steady_clock::now()) {
  for (int i = 0; i < NUMBER_OF_ROOMS; i++) {
    rooms.push_back(Game());
  }
}

int Server::serve() {
  try {
    this->setup();

    if (this->add_fd_to_epoll(server_socket))
      throw std::runtime_error("Failed to add server socket to pool");

    while (true) {
      int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
      for (int i = 0; i < event_count; i++) {
        int fd = events[i].data.fd;
        if (fd == server_socket) {
          this->handle_new_connection();
        } else if (fd == this->global_timer_fd) {
          this->handle_timer();
        } else if (fd == this->game_timer_fd) {
          this->handle_game_tick();
        } else if (connections.count(fd)) {
          this->handle_socket_read(fd);
        }
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "Server error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}

void Server::broadcast_game(Game &game, std::string msg) {
  for (Player *player : game.players) {
    auto it = std::find_if(
        this->connections.begin(), this->connections.end(),
        [player](const auto &pair) { return pair.second->player == player; });
    if (it != this->connections.end()) {
      send(it->second->socket, msg.c_str(), msg.size(), 0);
    }
  }
}

void Server::handle_game_tick() {
  uint64_t expirations;
  ssize_t s = read(this->game_timer_fd, &expirations, sizeof(expirations));
  if (s != sizeof(expirations)) {
    perror("gametimerfd read");
    return;
  }

  for (Game &game : rooms) {
    if (game.active) {
      std::vector<Player *> inactive;
      for (auto player : game.players)
        if (!player->updated)
          inactive.push_back(player);

      if (inactive.size()) {
        std::string msg = "WAIT";
        for (auto player : inactive) {
          msg += " " + player->nickname;
        }
        msg += "|";

        // Only send WAIT to players who have updated
        for (Player *player : game.players) {
          if (player->updated) {
            auto it =
                std::find_if(this->connections.begin(), this->connections.end(),
                             [player](const auto &pair) {
                               return pair.second->player == player;
                             });
            if (it != this->connections.end()) {
              send(it->second->socket, msg.c_str(), msg.size(), 0);
            }
          }
        }
        continue;
      };

      std::cout << game.full_state() << std::endl;
      std::cout << game.current_move() << std::endl;
      bool game_continues = game.slither();
      if (game_continues) {
        broadcast_game(game, "TICK " + game.full_state() + "|");
        std::cout << "-----" << std::endl;
        game.print();
        std::cout << "-----" << std::endl;
      } else {
        broadcast_game(game, "TICK " + game.full_state() + "|");
        auto it = std::find_if(game.players.begin(), game.players.end(),
                               [](Player *player) { return player->alive; });
        if (it == game.players.end()) {
          broadcast_game(game, "DRAW|");
        } else {
          broadcast_game(game, "WINS " + (*it)->nickname + "|");
        }
        game.active = false;
      };
    }
  }
}

void Server::handle_timer() {
  // Read to clear the event
  uint64_t expirations;
  ssize_t s = read(this->global_timer_fd, &expirations, sizeof(expirations));
  if (s != sizeof(expirations)) {
    perror("timerfd read");
    return;
  }

  // check for timeouts
  std::vector<int> to_close;
  for (auto &pair : connections) {
    Connection &conn = *pair.second;
    if (std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - conn.last_active)
            .count() > CONNECTION_TIMEOUT) {
      to_close.push_back(conn.socket);
    }
  }
  for (int fd : to_close) {
    this->close_connection(fd);
  }

  // removing inactive players
  {
    std::vector<Player *> to_remove;
    for (auto &player : players) {
      if (std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::steady_clock::now() - player->last_active)
              .count() > PLAYER_REMOVAL_TIMEOUT) {
        to_remove.push_back(player.get());
      }
    }

    for (Player *p : to_remove) {
      // Remove player from all rooms/games
      for (auto &room : rooms) {
        auto it = std::find(room.players.begin(), room.players.end(), p);
        if (it != room.players.end()) {
          room.players.erase(it);
          std::string update_msg = "LOBY";
          for (auto player : room.players) {
            update_msg += " " + player->nickname;
          }
          update_msg += "|";
          broadcast_game(room, update_msg);
        }
      }
      auto it = std::find_if(
          players.begin(), players.end(),
          [p](const std::unique_ptr<Player> &ptr) { return ptr.get() == p; });
      if (it != players.end()) {
        players.erase(it);
      }
    }
  }

  // ping connected clients
  if (std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now() - this->last_ping)
          .count() > PING_INTERVAL) {
    for (auto &pair : connections) {
      const char *ping_msg = "PING|";
      send(pair.first, ping_msg, strlen(ping_msg), 0);
    }
    this->last_ping = std::chrono::steady_clock::now();
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
    this->close_connection(sock_fd);
    return;
  }

  buff.resize(bytes_received);
  conn.buff.append(buff);

  // log
  // std::cout << "[" << conn.get_name() << "] : " << buff << std::endl;

  if (conn.buff.size() < 4)
    return;
  if (get_msg_type(conn.buff.substr(0, 4)) == INVALID) {
    this->close_connection(conn.socket);
    return;
  }

  size_t separator = conn.buff.find('|');

  // process whole messages
  while (separator != std::string::npos) {
    std::string msg = conn.buff.substr(0, separator);
    conn.buff.erase(0, separator + 1);
    if (this->process_message(conn, msg)) {
      this->close_connection(conn.socket);
      return;
    };

    // mark the connection and its player as active
    conn.last_active = std::chrono::steady_clock::now();
    if (conn.player) {
      conn.player->last_active = std::chrono::steady_clock::now();
    }
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
  std::cout << "[" << conn.get_name() << "] : " << msg << std::endl;
  // std::cout << "rocessing message:" << msg << std::endl;
  auto tokens = split(msg.data(), ' ');
  // for (const auto &token : tokens) {
  //   std::cout << "token: " << token << std::endl;
  // }
  if (tokens.size() < 1)
    return 1;

  msg_type type = get_msg_type(tokens[0]);
  if (type != NICK && type != PONG && !conn.player)
    return 1;

  switch (type) {
  case OK:
  case WAITING:
  case PONG:
    // so far only to reset last_msg time
    break;
  case NICK: {

    if (tokens.size() != 2 || conn.player)
      return 1;


    std::string nick = tokens[1];
    auto new_conn_player_it = std::find_if(
        this->players.begin(), this->players.end(),
        [nick](const auto &player) { return player->nickname == nick; });

    if (new_conn_player_it == this->players.end()) {
      players.push_back(std::make_unique<Player>(tokens[1]));
      conn.player = players.back().get();

      std::string reply = "ROOM";
      for (const auto &room : rooms) {
        reply += " " + std::to_string(room.players.size());
      }
      reply += "|";
      send(conn.socket, reply.c_str(), reply.size(), 0);
    } else {
      Player *player = new_conn_player_it->get();

      // check if a connection with the player exists and close is if it does
      auto old_conn_it = std::find_if(
          this->connections.begin(), this->connections.end(),
          [nick](const auto &c) {
            return c.second->player && c.second->player->nickname == nick;
          });
       
      if (old_conn_it != this->connections.end()) {
        this->close_connection(old_conn_it->second->socket);
      }

      conn.player = player;
      bool player_in_lobby = false;
      for (auto &room : rooms) {
        auto p_it = std::find(room.players.begin(), room.players.end(), player);
        if (p_it != room.players.end()) {
          player_in_lobby = true;
          std::string reply = "LOBY";
          for (auto p : room.players) {
            reply += " " + p->nickname;
          }
          reply += "|";
          send(conn.socket, reply.c_str(), reply.size(), 0);

          if (room.active) {
            std::string tick = "TICK " + room.full_state() + "|";
            send(conn.socket, tick.c_str(), tick.size(), 0);
          }
        }
      }

      if (!player_in_lobby) {
        std::string reply = "ROOM";
        for (const auto &room : rooms) {
          reply += " " + std::to_string(room.players.size());
        }
        reply += "|";
        send(conn.socket, reply.c_str(), reply.size(), 0);
      }
    }
    break;
  }
  case LIST_ROOMS: {

    if (tokens.size() != 1)
      return 1;

    std::string reply = "ROOM";
    for (const auto &room : rooms) {
      reply += " " + std::to_string(room.players.size());
    }
    reply += "|";
    send(conn.socket, reply.c_str(), reply.size(), 0);
  } break;
  case JOIN: {
    if (tokens.size() != 2)
      return 1;

    char *endptr = nullptr;
    int room_id = std::strtol(tokens[1].c_str(), &endptr, 10);
    if (*endptr != '\0' || room_id > NUMBER_OF_ROOMS || room_id < 0)
      return 1;
    if (rooms[room_id].players.size() >= MAX_PLAYERS_IN_ROOM) {
      const char *reply = "FULL|";
      send(conn.socket, reply, strlen(reply), 0);
      return 0;
    }

    // Remove player from any room they are in
    for (auto &room : rooms) {
      auto it =
          std::find(room.players.begin(), room.players.end(), conn.player);
      if (it != room.players.end()) {
        room.players.erase(it);
        std::string update_msg = "LOBY";
        for (auto player : room.players) {
          update_msg += " " + player->nickname;
        }
        update_msg += "|";
        broadcast_game(room, update_msg);
      }
    }

    rooms[room_id].players.push_back(conn.player);
    std::string reply = "LOBY";
    for (auto player : rooms[room_id].players) {
      reply += " " + player->nickname;
    }
    reply += "|";
    broadcast_game(rooms[room_id], reply);
  } break;
  case INVALID: {
    // std::cout << "invalid message: [" << msg << "] from " << conn.get_name()
    //           << std::endl;
    return 1;
  } break;
  case LEAVE: {
    if (tokens.size() != 1)
      return 1;

    // Remove player from any room they are in
    for (auto &room : rooms) {
      auto it =
          std::find(room.players.begin(), room.players.end(), conn.player);
      if (it != room.players.end()) {
        room.players.erase(it);
        std::string update_msg = "LOBY";
        for (auto player : room.players) {
          update_msg += " " + player->nickname;
        }
        update_msg += "|";
        broadcast_game(room, update_msg);
      }
    }
    const char *reply = "LEFT|";
    send(conn.socket, reply, strlen(reply), 0);
  } break;
  case MOVE: {
    if (tokens.size() != 2 || tokens[1].size() != 1)
      return 1;

    switch (tokens[1][0]) {
    case 'U':
      if (conn.player->last_move_dir != DOWN)
        conn.player->dir = UP;
      break;
    case 'D':
      if (conn.player->last_move_dir != UP)
        conn.player->dir = DOWN;
      break;
    case 'L':
      if (conn.player->last_move_dir != RIGHT)
        conn.player->dir = LEFT;
      break;
    case 'R':
      if (conn.player->last_move_dir != LEFT)
        conn.player->dir = RIGHT;
      break;
    default:
      return 1;
    }
    const char *msg = "MOVD|";
    send(conn.socket, msg, strlen(msg), 0);
    break;
  }
  case START: {
    if (tokens.size() != 1)
      return 1;

    auto game = std::find_if(
        this->rooms.begin(), this->rooms.end(), [&conn](Game &game) {
          return std::find(game.players.begin(), game.players.end(),
                           conn.player) != game.players.end();
        });

    if (game == this->rooms.end()) {
      std::cout << "Could not find game player is in";
      return 1;
    }

    int hatch_failed = game->hatch();
    if (hatch_failed) {
      const char *reply = "STRT FAIL|";
      send(conn.socket, reply, strlen(reply), 0);
      break;
    }
    game->active = true;
    game->print();

    const char *reply = "STRT OK|";
    send(conn.socket, reply, strlen(reply), 0);
    broadcast_game(*game, "TICK " + game->full_state() + "|");
  } break;
  case TACK: {
    conn.player->updated = true;
  } break;
  case QUIT: {

    if (tokens.size() != 1)
      return 1;

    // Remove player from any room they are in
    for (auto &room : rooms) {
      auto it =
          std::find(room.players.begin(), room.players.end(), conn.player);
      if (it != room.players.end()) {
        room.players.erase(it);
        std::string update_msg = "LOBY";
        for (auto player : room.players) {
          update_msg += " " + player->nickname;
        }
        update_msg += "|";
        broadcast_game(room, update_msg);
      }
    }

    // remove player from the server vector
    auto it = std::find_if(this->players.begin(), this->players.end(),
                           [&conn](const std::unique_ptr<Player> &p) {
                             return p.get() == conn.player;
                           });
    if (it != this->players.end()) {
      this->players.erase(it);
    }

    this->close_connection(conn.socket);

  } break;
  }
  return 0;
}

int Server::add_fd_to_epoll(int sock) {
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
  if (it == connections.end())
    return;
  std::cout << "Closing connection with: " << it->second->get_name()
            << std::endl;
  epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sock_fd, nullptr);
  close(sock_fd);
  connections.erase(sock_fd);
}

void Server::handle_new_connection() {
  sockaddr_in client_addr = {};
  socklen_t addrlen = sizeof(client_addr);

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
      client_socket,
      std::make_unique<Connection>(Connection(client_socket, client_addr)));
  if (!res.second) {
    std::cerr << "Error: Connection already exists for fd " << client_socket
              << std::endl;
    close(client_socket);
    return;
  }

  // add fd to pool
  if (Server::add_fd_to_epoll(client_socket)) {
    throw std::runtime_error("Could not add to epoll pool");
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

  // set up timer for client
  global_timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
  if (global_timer_fd == -1) {
    perror("timer_fd");
    return;
  }
  itimerspec timer_spec;
  timer_spec.it_interval.tv_sec = GLOBAL_TIMER_CHECK;
  timer_spec.it_interval.tv_nsec = 0;
  timer_spec.it_value.tv_sec = GLOBAL_TIMER_CHECK;
  timer_spec.it_value.tv_nsec = 0;
  timerfd_settime(global_timer_fd, 0, &timer_spec, nullptr);

  // set up timer for game loops
  game_timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
  if (game_timer_fd == -1) {
    perror("timer_fd");
    return;
  }
  timer_spec.it_interval.tv_sec = GAME_SPEED;
  timer_spec.it_interval.tv_nsec = 0;
  timer_spec.it_value.tv_sec = GAME_SPEED;
  timer_spec.it_value.tv_nsec = 0;
  timerfd_settime(game_timer_fd, 0, &timer_spec, nullptr);

  // add timer fd to pool
  if (Server::add_fd_to_epoll(global_timer_fd) ||
      Server::add_fd_to_epoll(game_timer_fd)) {
    throw std::runtime_error("Could not add to epoll pool");
  }
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
  std::string ip = "127.0.0.1";
  if (argc > 2) {
    ip = argv[2];
  }
  Server server(port, ip);
  server.serve();
}