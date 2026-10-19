// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>
#define GAME_ID "gomoku"
#define GAME_NAME "AI Games: Go-Moku"
#define GAME_HISTORY_FILE GAME_ID "-history.md"
enum {
  GAME_SIZE = 15,
  GAME_POINTS = 225,
  GAME_CHOICES = 225,
  GAME_MAX_DEPTH = 6,
  GAME_MAX_ACTIONS = 225,
  GAME_WIN_SCORE = 1000000
};
typedef enum {
  PLAYER_HUMAN,
  PLAYER_RANDOM,
  PLAYER_POSITIONAL,
  PLAYER_MINIMAX,
  PLAYER_ALPHA_BETA,
  PLAYER_OLLAMA,
  PLAYER_COUNT
} PlayerKind;
typedef enum {
  GAME_ONGOING = 0,
  GAME_BLACK = 1,
  GAME_WHITE = -1,
  GAME_DRAW = 2
} GameResult;
typedef struct {
  int cell;
} GameMove;
typedef struct {
  GameMove moves[GAME_CHOICES];
  unsigned count;
} GameMoves;
typedef struct {
  gint8 cells[GAME_POINTS];
  int turn;
  unsigned actions;
  int last;
} Game;
typedef struct {
  int own_score, other_score, total;
} PositionValue;
extern const char *const game_labels[GAME_POINTS];
void game_init(Game *game);
unsigned game_count(const Game *game, int player);
gboolean game_winning_point(const Game *game, unsigned point);
GameResult game_result(const Game *game);
void game_legal_moves(const Game *game, GameMoves *moves);
gboolean game_move_equal(GameMove a, GameMove b);
gboolean game_move_legal(const Game *game, GameMove move);
gboolean game_play(Game *game, GameMove move);
void game_apply_legal(Game *game, GameMove move);
int game_point(const char *label);
void game_move_label(GameMove move, char label[16]);
gboolean game_parse_move(const char *label, GameMove *move);
int game_random_move(const GameMoves *legal, GRand *random);
void game_position(const Game *game, int player, PositionValue *value);
int game_positional_move(const Game *game, GRand *random,
                         int values[GAME_CHOICES]);
const char *player_name(PlayerKind kind);
gboolean game_append_report(const char *path, const char *report,
                            GError **error);
