#include "connection.hpp"
#include <arpa/inet.h>

Connection::Connection(int socket, sockaddr_in addr)
    : socket(socket), addr(addr), player(nullptr),
      last_active(std::chrono::steady_clock::now()) {}

std::string Connection::get_name() {
  if (player) {
    return player->nickname;
  }
  char client_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(addr.sin_addr), client_ip, INET_ADDRSTRLEN);
  int client_port = ntohs(addr.sin_port);
  return std::string(client_ip) + ":" + std::to_string(client_port);
}
