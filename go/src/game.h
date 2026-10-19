// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>
#define GAME_ID "go"
#define GAME_NAME "AI Games: Go"
#define GAME_HISTORY_FILE "go-history.md"
enum {
  GO_SIZE = 9,
  GO_POINTS = 81,
  GO_PASS = 81,
  GO_CHOICES = 82,
  GO_BLOOM = 64
};
typedef enum { BLACK, WHITE } Color;
typedef enum {
  PLAYER_HUMAN,
  PLAYER_RANDOM,
  PLAYER_HEURISTIC,
  PLAYER_MCTS,
  PLAYER_OLLAMA,
  PLAYER_COUNT
} PlayerKind;
typedef enum {
  MOVE_OK,
  MOVE_FINISHED,
  MOVE_OUTSIDE,
  MOVE_OCCUPIED,
  MOVE_SUICIDE,
  MOVE_SUPERKO
} MoveError;
typedef struct {
  guint64 stones[2][2];
} Board;
typedef struct {
  Board board;
  unsigned captures[2], turn, passes;
  int last;
  guint64 removed[2], hash;
} State;
typedef struct {
  State state;
  double komi;
  GArray *history;
  guint64 seen[GO_BLOOM];
} Game;
typedef struct {
  unsigned count;
  guint8 items[GO_CHOICES];
} MoveList;
typedef struct {
  guint64 stones[2];
  unsigned size, liberties;
} Group;
typedef struct {
  unsigned stones[2], territory[2], neutral;
  double area[2], margin;
  int owner[GO_POINTS];
} Score;
void game_init(Game *game, double komi);
void game_clear(Game *game);
void game_copy(Game *target, const Game *source, unsigned reserve_actions);
gboolean game_undo(Game *game);
int game_at(const Board *board,
            unsigned point); /* -1 empty; 0 Black; 1 White. */
void game_set(Board *board, unsigned point, int color);
Group game_group(const Board *board, unsigned point);
MoveError game_try(const Game *game, unsigned move, State *next);
gboolean game_play(Game *game, unsigned move);
MoveList game_legal(const Game *game);
gboolean game_finished(const Game *game);
gboolean game_valid(const Game *game);
void game_score(const Game *game, Score *score);
int game_winner(const Game *game); /* -1 ongoing, 0 Black, 1 White, 2 draw. */
int game_random(const MoveList *legal, GRand *random);
int game_heuristic(const Game *game, GRand *random, int values[GO_CHOICES]);
int game_evaluate(const Game *game, unsigned color);
int game_point(const char *label);
const char *game_label(unsigned move);
const char *move_error_name(MoveError error);
const char *player_name(PlayerKind kind);
char *game_map(const Game *game);
gboolean game_append_report(const char *path, const char *report,
                            GError **error);
