// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>
#define GAME_ID "ludus-coriovalli"
#define GAME_NAME "AI Games: Ludus Coriovalli"
#define GAME_HISTORY_FILE GAME_ID "-history.md"
enum {
  GAME_POINTS = 10,
  GAME_CHOICES = 24,
  GAME_DEFAULT_LIMIT = 100,
  GAME_DRAW = 3
};
typedef struct {
  int from, to;
} GameMove; /* {-1,-1} = forced dogs pass */
typedef struct {
  GameMove moves[GAME_CHOICES];
  unsigned count;
} GameMoves;
typedef struct {
  guint8 cells[GAME_POINTS]; /* 0 empty, 1 dog, 2 hare; roles, not player IDs */
  int turn;                  /* role 1 dogs / 2 hares */
  unsigned round, hare_moves, actions, limit;
  unsigned survival[2]; /* hare moves survived by each player */
  gboolean capped[2], round_over;
} Game;
/* Indices are stable Ludii board-1 vertices, coordinates retained for
 * inspection. */
extern const double game_x[GAME_POINTS], game_y[GAME_POINTS];
extern const char *const game_labels[GAME_POINTS];
extern const guint16 game_neighbors[GAME_POINTS];
void game_init(Game *g);
void game_init_limit(Game *g, unsigned limit);
unsigned game_actor(const Game *g);
unsigned game_hare_player(const Game *g);
unsigned game_mobility(const Game *g, int role);
gboolean game_hares_blocked(const Game *g);
void game_legal_moves(const Game *g, GameMoves *moves);
gboolean game_move_legal(const Game *g, GameMove move);
gboolean game_play(Game *g, GameMove move);
gboolean game_next_round(Game *g);
int game_result(const Game *g); /* whole paired match; 3 administrative draw */
void game_move_label(GameMove move, char label[16]);
gboolean game_parse_move(const char *label, GameMove *move);
gboolean game_append_report(const char *path, const char *report,
                            GError **error);
