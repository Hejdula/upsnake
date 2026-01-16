#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>
/**
 * @brief Message types used in the communication protocol.
 */
enum msg_type {
  INVALID,    ///< Invalid or unrecognized message.
  PONG,       ///< Pong response to Ping.
  NICK,       ///< Set nickname.
  LEAVE,      ///< Leave current room.
  MOVE,       ///< Move snake (U/D/L/R).
  START,      ///< Start the game.
  QUIT,       ///< Disconnect from server.
  LIST_ROOMS, ///< Request list of rooms.
  JOIN,       ///< Join a specific room.
  TACK,       ///< Client acknowledge tick (Tick Ack).
  WAITING,    ///< Waiting state notification.
  OK,         ///< Generic OK response.
};

/**
 * @brief Helper function to parse a message token string to obtain the message
 * type.
 *
 * @param key_token The string command.
 * @return msg_type The corresponding enum value.
 */
msg_type get_msg_type(std::string key_token);
#endif