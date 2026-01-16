#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "player.hpp"
#include <netinet/in.h>
#include <string>
#include <chrono>

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

#endif // CONNECTION_HPP
