// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>
#define GAME_ID "heptalpha"
#define GAME_NAME "AI Games: Heptalpha"
#define GAME_HISTORY_FILE "heptalpha-history.md"
enum { GAME_POINTS = 21, GAME_CHOICES = 56, GAME_MAX_DEPTH = 20,
       GAME_PATHS = 7, GAME_PATH_POINTS = 6 };
#define MOVE_BIT(move) (G_GUINT64_CONSTANT(1) << (move))
typedef enum { PLAYER_HUMAN, PLAYER_RANDOM, PLAYER_HEURISTIC, PLAYER_SEARCH, PLAYER_OLLAMA } PlayerKind;
typedef struct { unsigned entry, middle, destination, path; } Move;
typedef struct { guint32 occupied; unsigned moves; Move history[GAME_MAX_DEPTH]; } Game;
extern const char * const game_labels[GAME_POINTS];
extern const unsigned game_lines[GAME_PATHS][GAME_PATH_POINTS];
extern const Move game_moves[GAME_CHOICES];
void game_init(Game *game);
gboolean game_legal(const Game *game, unsigned move);
guint64 game_legal_moves(const Game *game);
/* 0: ongoing; 1: twenty pieces; 2: stuck. */
int game_result(const Game *game);
gboolean game_play(Game *game, unsigned move);
gboolean game_undo(Game *game);
int game_find_move(unsigned entry, unsigned destination);
int game_point(const char *label);
unsigned game_count(guint64 mask);
int game_random_move(guint64 mask, GRand *random);
/* Score = final count * 100 + mobility; terminal states omit mobility. */
int game_heuristic(const Game *game, unsigned move);
const char *player_name(PlayerKind kind);
gboolean game_append_report(const char *path, const char *report, GError **error);
