#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "player.hpp"
#include <chrono>
#include <netinet/in.h>
#include <string>

/**
 * @brief Represents a client connection to the server.
 *
 * Manages socket information, buffers, and associated player state.
 */
class Connection {
public:
  /**
   * @brief Construct a new Connection object.
   *
   * @param socket The client socket file descriptor.
   * @param addr The address structure of the client.
   */
  Connection(int socket, sockaddr_in addr);

  /**
   * @brief Get the name of the connection.
   *
   * Returns the player's nickname if set, otherwise the IP address and port.
   *
   * @return std::string The name or identifier of the connection.
   */
  std::string get_name();

  int socket;       ///< Socket file descriptor.
  sockaddr_in addr; ///< Client address information.
  std::string buff; ///< Input buffer for received data.
  Player *player;   ///< Pointer to associated Player object (if any).
  std::chrono::steady_clock::time_point
      last_active; ///< Timestamp of last message.
};

#endif // CONNECTION_HPP
