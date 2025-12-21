#include "protocol.hpp"
#include <string>
#include <unordered_map>

std::unordered_map<std::string, msg_type> msg_type_map = {
    {"PONG", PONG}, {"NICK", NICK},   {"LEAV", LEAVE},
    {"MOVE", MOVE}, {"START", START}, {"QUIT", QUIT},
};

msg_type get_msg_type(std::string key_token) {
  auto it = msg_type_map.find(key_token);
  if (it == msg_type_map.end())
    return INVALID;
  return it->second;
}