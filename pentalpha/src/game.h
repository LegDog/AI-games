// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>
#define GAME_ID "pentalpha"
#define GAME_NAME "AI Games: Pentalpha"
#define GAME_HISTORY_FILE "pentalpha-history.md"
enum { GAME_POINTS = 10, GAME_CHOICES = 20, GAME_MAX_DEPTH = 9 };
typedef enum { PLAYER_HUMAN, PLAYER_RANDOM, PLAYER_HEURISTIC, PLAYER_SEARCH, PLAYER_OLLAMA } PlayerKind;
typedef struct { unsigned entry, middle, destination; } Move;
typedef struct { guint16 occupied; unsigned moves; Move history[9]; } Game;
extern const char * const game_labels[10];
extern const unsigned game_lines[5][4];
extern const Move game_moves[20];
void game_init(Game *game);
gboolean game_legal(const Game *game, unsigned move);
guint32 game_legal_moves(const Game *game);
/* 0: ongoing; 1: nine pieces; 2: stuck. */
int game_result(const Game *game);
gboolean game_play(Game *game, unsigned move);
gboolean game_undo(Game *game);
int game_find_move(unsigned entry, unsigned destination);
int game_point(const char *label);
unsigned game_count(guint32 mask);
int game_random_move(guint32 mask, GRand *random);
/* Score = final count * 100 + mobility; terminal states omit mobility. */
int game_heuristic(const Game *game, unsigned move);
const char *player_name(PlayerKind kind);
gboolean game_append_report(const char *path, const char *report, GError **error);
