// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>
#define GAME_ID "nine-mens-morris"
#define GAME_NAME "AI Games: Nine Men’s Morris"
#define GAME_HISTORY_FILE GAME_ID "-history.md"
enum {
  GAME_POINTS = 24,
  GAME_MILLS = 16,
  GAME_CHOICES = 768,
  GAME_DRAW = 3,
  GAME_QUIET_LIMIT = 50
};
typedef struct {
  int from, to, capture;
} GameMove; /* from=-1: placement; capture=-1: none */
typedef struct {
  GameMove moves[GAME_CHOICES];
  unsigned count;
} GameMoves;
typedef enum {
  GAME_PLAYING,
  GAME_MATERIAL,
  GAME_BLOCKED,
  GAME_REPETITION,
  GAME_QUIET
} GameEnd;
typedef struct {
  guint32 pieces[2];
  unsigned reserve[2];
  int turn, result;
  GameEnd end;
  unsigned actions, quiet, history_count;
  guint64 history[GAME_QUIET_LIMIT + 1]; /* since last irreversible turn */
} Game;
extern const double game_x[GAME_POINTS], game_y[GAME_POINTS];
extern const char *const game_labels[GAME_POINTS];
extern const guint32 game_neighbors[GAME_POINTS], game_mills[GAME_MILLS];
void game_init(Game *g);
unsigned game_cell(const Game *g, int p);
unsigned game_count(const Game *g, int side);
unsigned game_actor(const Game *g);
gboolean game_placing(const Game *g);
unsigned game_mobility(const Game *g, int side);
guint32 game_mill_pieces(guint32 pieces);
void game_legal_moves(const Game *g, GameMoves *moves);
gboolean game_move_legal(const Game *g, GameMove move);
gboolean game_play(Game *g, GameMove move);
int game_result(const Game *g);
void game_move_label(GameMove move, char label[16]);
gboolean game_parse_move(const char *label, GameMove *move);
gboolean game_append_report(const char *path, const char *report,
                            GError **error);
