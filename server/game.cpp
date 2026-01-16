#include "game.hpp"
#include <iostream>
#include <algorithm>
#include <vector>

Game::Game() : active(false) {
  grid.fill({});
  dir_to_pos = {
      Position{0, -1},  // UP
      Position{0, 1}, // DOWN
      Position{-1, 0}, // LEFT
      Position{1, 0},  // RIGHT
  };
}

void Game::print() {
  // ANSI color codes for up to 6 players
  const char *colors[] = {"\033[31m", "\033[32m", "\033[33m",
                          "\033[34m", "\033[35m", "\033[36m"};
  const char *reset = "\033[0m";
  char field[GRID_SIZE][GRID_SIZE];
  // Fill field with empty
  for (int y = 0; y < GRID_SIZE; ++y)
    for (int x = 0; x < GRID_SIZE; ++x)
      field[y][x] = '.';

  // Place apple
  field[apple.y][apple.x] = 'A';

  // Place snakes
  int pid = 0;
  for (Player *player : players) {
    if (!player->alive)
      continue;
    for (const Position &part : player->body) {
      field[part.y][part.x] = '0' + pid;
    }
    pid++;
  }

  // Print player names in their color
  pid = 0;
  for (Player *player : players) {
    if (!player->alive)
      continue;
    std::cout << colors[pid % 6] << player->nickname << reset << " ";
    pid++;
  }
  std::cout << std::endl;

  // Print field with colors
  for (int y = 0; y < GRID_SIZE; ++y) {
    for (int x = 0; x < GRID_SIZE; ++x) {
      char c = field[y][x];
      if (c == 'A') {
        std::cout << "\033[41mA" << reset;
      } else if (c >= '0' && c <= '5') {
        std::cout << colors[c - '0'] << c << reset;
      } else {
        std::cout << c;
      }
    }
    std::cout << std::endl;
  }
};

bool Game::is_empty(Position pos) {
  bool tile_is_full = false;
  for (Player *player : players) {
    for (auto part : player->body) {
      if (pos.x == part.x && pos.y == part.y) {
        tile_is_full = true;
      }
    }
  }
  return !tile_is_full;
}

bool Game::slither() {
  // are there enought players for the game to continue?
  if (std::count_if(this->players.begin(), this->players.end(),
                    [](Player *p) { return p->alive; }) < 2) {
    return false;
  }

  // newly occupied tiles
  std::vector<Position> snake_heads;

  // advance the snakes, kill if out of bounds
  for (Player *player : this->players) {

    if (!player->alive)
      continue;
    player->updated = false;
    Position pos = player->body.front() + this->dir_to_pos[player->dir];
    if (pos.x < 0 || pos.x >= GRID_SIZE || pos.y < 0 || pos.y >= GRID_SIZE) {
      player->alive = false;
    } else {
      snake_heads.push_back(pos);
      player->body.push_front(pos);
      player->last_move_dir = player->dir;
    }
  }

  // check for colisions
  for (Player *player : this->players) {
    Position pos = player->body.front();
    if (grid[pos.y][pos.x]) {
      player->alive = false;
    };
  }

  // check for head to head colisions
  for (Player *outer : this->players) {
    if (!outer->alive)
      continue;
    for (Player *inner : this->players) {
      if (outer != inner && inner->alive &&
          outer->body.front() == inner->body.front()) {
        outer->alive = false;
        inner->alive = false;
      }
    }
  }

  // set colision tiles under new heads
  for (Position head : snake_heads) {
    grid[head.y][head.x] = true;
  }

  // remove tail of snakes who did not eat the apple
  bool apple_eaten = false;
  for (Player *player : this->players) {
    if (player->alive && player->body.front() == this->apple) {
      player->apples++;
      player->length++;
      apple_eaten = true;
    } else if ((int)player->body.size() > player->length) {
      Position pos = player->body.back();
      grid[pos.y][pos.x] = false;
      player->body.pop_back();
    }
  }

  // respawn the apple if eaten
  if (apple_eaten) {
    this->apple = random_empty_tile();
  }

  // check if game ended after this tick
  if (std::count_if(this->players.begin(), this->players.end(),
                    [](Player *p) { return p->alive; }) < 2) {
    return false;
  }

  return true;
}

Position Game::random_empty_tile() {
  Position pos;
  do {
    pos = {std::rand() % GRID_SIZE, std::rand() % GRID_SIZE};
  } while (!is_empty(pos));
  return pos;
};

int Game::hatch() {
  if (this->players.size() < 2 || this->active) {
    return 1;
  }

  for (auto &row : this->grid) {
    row.fill(false);
  }

  for (Player *player : this->players) {
    player->body.clear();
    player->length = 3;
    Position pos = Game::random_empty_tile();
    player->dir = static_cast<Direction>(std::rand() % 4);
    player->body.push_front(pos);
    grid[pos.y][pos.x] = true;
    player->alive = true;
  }

  this->apple = random_empty_tile();
  this->active = true;
  return 0;
}

std::string Game::current_move() {
  std::string move_str = "";
  move_str +=
      std::to_string(this->apple.x) + " " + std::to_string(this->apple.y);
  for (auto player : this->players) {
    move_str += " " + player->nickname + " " + dir_to_string(player->dir);
  }
  return move_str;
}

std::string Game::full_state() {
  std::string state_str = "";
  state_str +=
      std::to_string(this->apple.x) + " " + std::to_string(this->apple.y);
  for (auto player : this->players) {
    if (player->body.size() == 0)
      continue;
    state_str += " " + player->nickname + " " +
                 std::to_string(player->body.front().x) + " " +
                 std::to_string(player->body.front().y) + " ";

    state_str += player->alive
                     ? "H"
                     : "E"; // H for head if the player only has head so far;
    Position last_body_part = player->body.front();
    for (auto body_part : player->body) {
      if (last_body_part == body_part)
        continue;
      for (int dir = 0; dir < DIRECTION_COUNT; ++dir) {
        if (dir_to_pos[dir] == body_part - last_body_part) {
          state_str += dir_to_string(static_cast<Direction>(dir));
        };
      }
      last_body_part = body_part;
    }
  }
  return state_str;
}
