// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>
#define GAME_ID "alquerque"
#define GAME_NAME "AI Games: Alquerque"
#define GAME_HISTORY_FILE GAME_ID "-history.md"
enum {
  GAME_POINTS = 25,
  GAME_DRAW = 3,
  GAME_QUIET_LIMIT = 50,
  GAME_MAX_JUMPS = 12,
  GAME_LABEL_SIZE = 40
};
typedef enum { GAME_FREE, GAME_DIRECTIONAL } GameVariant;
typedef struct {
  int from;
  unsigned length;
  int land[GAME_MAX_JUMPS];
  gboolean capture;
} GameMove;
typedef struct {
  GameMove *moves;
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
  GameVariant variant;
  int turn, result;
  GameEnd end;
  unsigned actions, quiet, history_count;
  guint64 history[GAME_QUIET_LIMIT + 1];
} Game;
extern const double game_x[GAME_POINTS], game_y[GAME_POINTS];
extern const char *const game_labels[GAME_POINTS];
extern const guint32 game_neighbors[GAME_POINTS];
void game_init(Game *g);
void game_init_variant(Game *g, GameVariant variant);
const char *game_variant_name(GameVariant variant);
unsigned game_cell(const Game *g, int p);
unsigned game_count(const Game *g, int side);
unsigned game_actor(const Game *g);
unsigned game_mobility(const Game *g, int side);
void game_moves_clear(GameMoves *moves);
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(GameMoves, game_moves_clear)
/* Initialize collections to {0}; enumeration replaces their contents. */
void game_legal_moves(const Game *g, GameMoves *moves);
gboolean game_legal_moves_full(const Game *g, GameMoves *moves,
                               gboolean (*cancelled)(gpointer), gpointer data);
gboolean game_move_prefix(GameMove complete, GameMove prefix);
gboolean game_prefix_valid(const Game *g, GameMove prefix);
gboolean game_move_legal(const Game *g, GameMove move);
gboolean game_play(Game *g, GameMove move);
/* Apply a validated hop to a preview only, without changing turn/history. */
void game_preview_hop(Game *g, int from, int to, gboolean capture);
int game_result(const Game *g);
void game_move_label(GameMove move, char label[GAME_LABEL_SIZE]);
gboolean game_parse_move(const char *label, GameMove *move);
gboolean game_append_report(const char *path, const char *report,
                            GError **error);
