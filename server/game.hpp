#ifndef GAME_HPP
#define GAME_HPP

#include "player.hpp"
#include <list>
#include <array>
#include <string>
#include <algorithm>

class Game {
  std::array<std::array<bool, GRID_SIZE>, GRID_SIZE> grid;
  std::array<Position, Direction::DIRECTION_COUNT> dir_to_pos;

public:
  std::list<Player *> players;
  bool active;
  bool waiting;
  Position apple;
  
  Game();

  bool is_empty(Position pos);
  void print();
  int hatch();
  bool slither();
  Position random_empty_tile();
  std::string current_move();
  std::string full_state();
};

#endif // GAME_HPP
