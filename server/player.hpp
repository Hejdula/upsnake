#ifndef PLAYER_HPP
#define PLAYER_HPP

#include <string>
#include <deque>
#include <chrono>
#include <array>

#define GRID_SIZE 10
#define INITIAL_SNAKE_LENGTH 3

enum Direction {
  UP,
  DOWN,
  LEFT,
  RIGHT,
  DIRECTION_COUNT,
};

inline std::string dir_to_string(Direction dir) {
  switch (dir) {
  case UP:
    return "U";
  case DOWN:
    return "D";
  case LEFT:
    return "L";
  case RIGHT:
    return "R";
  default:
    return "?";
  }
}

struct Position {
  int x;
  int y;

  Position operator+(const Position &other) const {
    return {x + other.x, y + other.y};
  }
  Position operator-(const Position &other) const {
    return {x - other.x, y - other.y};
  }
  bool operator==(const Position &other) const {
    return x == other.x && y == other.y;
  }
};

class Player {
public:
  std::string nickname;
  Direction dir;
  Direction last_move_dir;
  bool alive;
  bool updated;
  int apples;
  int length;
  std::deque<Position> body;
  std::chrono::steady_clock::time_point last_active;
  
  Player(const std::string &nickname)
      : nickname(nickname), last_move_dir(DIRECTION_COUNT),
        length(INITIAL_SNAKE_LENGTH) {}
};

#endif // PLAYER_HPP
