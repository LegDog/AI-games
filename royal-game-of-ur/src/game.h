// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>
#define GAME_ID "royal-game-of-ur"
#define GAME_NAME "AI Games: The Royal Game of Ur"
#define GAME_HISTORY_FILE GAME_ID "-history.md"
enum { GAME_CHOICES = 15 };
typedef struct {
  int cell;
} GameMove; /* source 0=reserve, -1=forced pass */
typedef struct {
  GameMove moves[GAME_CHOICES];
  unsigned count;
} GameMoves;
typedef struct {
  guint8 piece[2][15], reserve[2], finished[2];
  int turn, roll; /* 1 or 2; roll -1 means awaiting dice */
  unsigned actions;
} Game;
void game_init(Game *g);
int game_result(const Game *g);
gboolean game_rosette(int pos);
gboolean game_set_roll(Game *g, int roll);
int game_roll(Game *g, GRand *rng);
void game_legal_moves(const Game *g, GameMoves *moves);
gboolean game_move_legal(const Game *g, GameMove m);
gboolean game_play(Game *g, GameMove m);
void game_move_label(GameMove m, char label[16]);
gboolean game_parse_move(const char *label, GameMove *m);
gboolean game_append_report(const char *path, const char *report,
                            GError **error);
