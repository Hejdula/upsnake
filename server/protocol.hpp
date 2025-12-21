#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>
enum msg_type {
  INVALID,
  PONG,
  NICK,
  LEAVE,
  MOVE,
  START,
  QUIT,
};

msg_type get_msg_type(std::string key_token);
#endif