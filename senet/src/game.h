// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>
#define GAME_ID "senet"
#define GAME_NAME "AI Games: Senet"
#define GAME_HISTORY_FILE GAME_ID "-history.md"
enum { GAME_CHOICES = 5, GAME_PIECES = 5, GAME_HOUSES = 30 };
typedef struct {
  int cell;
} GameMove; /* source 1..30; -1 forced pass */
typedef struct {
  GameMove moves[GAME_CHOICES];
  unsigned count;
} GameMoves;
typedef struct {
  guint8 cells[31], finished[2]; /* index 0 unused; owners 0,1,2 */
  int turn, roll; /* active player 1 or 2, -1 awaits throwing sticks */
  unsigned actions;
} Game;
void game_init(Game *g);
int game_result(const Game *g);
int game_sticks_value(
    unsigned faces); /* 4-bit light-side mask; zero scores 5 */
gboolean game_set_roll(Game *g, int roll);
int game_roll(Game *g, GRand *rng);
gboolean game_protected(const Game *g, int house);
void game_legal_moves(const Game *g, GameMoves *moves);
gboolean game_move_legal(const Game *g, GameMove move);
/* Includes exchanges and water relocation, for atomic UI animations. */
int game_destination(const Game *g, GameMove move);
gboolean game_play(Game *g, GameMove move);
void game_move_label(GameMove move, char label[16]);
gboolean game_parse_move(const char *label, GameMove *move);
gboolean game_append_report(const char *path, const char *report,
                            GError **error);
