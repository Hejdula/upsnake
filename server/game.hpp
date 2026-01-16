#ifndef GAME_HPP
#define GAME_HPP

#include "player.hpp"
#include <algorithm>
#include <array>
#include <list>
#include <string>

/**
 * @brief Manages the state and logic of a single game room.
 */
class Game {
  std::array<std::array<bool, GRID_SIZE>, GRID_SIZE> grid;
  std::array<Position, Direction::DIRECTION_COUNT> dir_to_pos;

public:
  std::list<Player *> players; ///< List of players currently in the room.
  bool active;                 ///< Whether the game is currently ongoing.
  bool waiting;                ///< Whether the game is waiting for players.
  Position apple;              ///< Position of the apple.

  /**
   * @brief Construct a new Game object.
   *
   * Initializes the grid and direction mappings.
   */
  Game();

  /**
   * @brief Checks if a position on the grid is empty.
   *
   * @param pos The position to check.
   * @return true If the tile is empty (no snake parts).
   * @return false If the tile is occupied.
   */
  bool is_empty(Position pos);

  /**
   * @brief Prints the current game state to the console, only for debug.
   */
  void print();

  /**
   * @brief Initializes the game state for a new game.
   *
   * Resets snakes, places them randomly, and spawns the first apple,
   * sets the game as active
   *
   * @return int 0 on success, 1 if not enough players or already active.
   */
  int hatch();

  /**
   * @brief Advances the game by one tick.
   *
   * Moves snakes, checks collisions, handles eating, and manages game end
   * conditions.
   *
   * @return true If the game continues.
   * @return false If the game ends (not enough players alive).
   */
  bool slither();

  /**
   * @brief Finds a random empty tile on the grid.
   *
   * @return Position A random empty position.
   */
  Position random_empty_tile();

  /**
   * @brief Generates a string representation of the current move/positions.
   *
   * Includes apple position and heads of snakes.
   * Format: "ax ay [nick dir]..." where:
   * - ax, ay: Apple coordinates
   * - nick: Player nickname
   * - dir: Current direction (U, D, L, R)
   *
   * @return std::string The encoded move string.
   */
  std::string current_move();

  /**
   * @brief Generates a full state string of the game.
   *
   * Includes apple and full bodies of all snakes.
   * Format: "ax ay [nick hx hy status+body]..." where:
   * - ax, ay: Apple coordinates
   * - nick: Player nickname
   * - hx, hy: Head coordinates
   * - status: 'H' (Alive), 'E' (Eliminated)
   * - body: String of directions (U, D, L, R) tracing the body segments
   * example 1 2 nick1 3 4 HDL nick2 7 8 EUUR
   *
   * @return std::string The encoded full state string.
   */
  std::string full_state();
};

#endif // GAME_HPP
