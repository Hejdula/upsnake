#ifndef SERVER_HPP
#define SERVER_HPP

#include "connection.hpp"
#include "game.hpp"
#include <chrono>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <unordered_map>
#include <vector>

/**
 * @brief Main server class for multiplayer snake game
 *
 * Handles network connections, pooling, lobby state, game loop, and message
 * broadcasting.
 */
class Server {
public:
  /**
   * @brief Construct a new Server object.
   *
   * @param port Port number to listen on.
   * @param ip_address IP address to bind to.
   */
  Server(int port, const std::string &ip_address);

  /**
   * @brief Starts the server loop.
   *
   * Enters the main event loop, using epoll to handle I/O events.
   *
   * @return int 0 on success, 1 on error.
   */
  int serve();

  /**
   * @brief Adds a file descriptor to the epoll instance.
   *
   * @param sock The socket file descriptor to add.
   * @return int 0 on success, -1 on error.
   */
  int add_fd_to_epoll(int sock);

  /**
   * @brief Processes a received message from a client.
   *
   * @param conn The connection object representing the client.
   * @param msg The message string received.
   * @return int 0 on success, non-zero on error indicating connection should be
   * closed.
   */
  int process_message(Connection &conn, std::string msg);

  /**
   * @brief Handles global timer events, player and connection timeouts and
   * pings.
   */
  void handle_timer();

  /**
   * @brief Handles game tick timer events.
   *
   * Updates game state and broadcasts updates to players.
   */
  void handle_game_tick();

  /**
   * @brief Handles read events on a socket.
   *
   * Reads data from the socket and buffers it for processing.
   *
   * @param sock_fd The file descriptor that is ready for reading.
   */
  void handle_socket_read(int sock_fd);

  /**
   * @brief Handles incoming connection requests.
   *
   * Accepts new connections and adds them to the epoll instance.
   */
  void handle_new_connection();

  /**
   * @brief Sets up the server socket and resources.
   *
   * Creates sockets, binds, listens, and initializes epoll and timers.
   */
  void setup();

  /**
   * @brief Closes a client connection.
   *
   * Removes from epoll, closes the socket, and cleans up connection data.
   *
   * @param sock_fd The socket file descriptor to close.
   */
  void close_connection(int sock_fd);

  /**
   * @brief Broadcasts a message to all players in a specific game room.
   *
   * @param game The game instance to broadcast to.
   * @param msg The message to send.
   */
  void broadcast_game(Game &game, std::string msg);

  /**
   * @brief Sets a socket to non-blocking mode.
   *
   * @param sockfd The socket file descriptor.
   * @return int 0 on success, -1 on error.
   */
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
