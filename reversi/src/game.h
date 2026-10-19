// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>
#define GAME_ID "reversi"
#define GAME_NAME "AI Games: Reversi"
#define GAME_HISTORY_FILE GAME_ID "-history.md"
#define SQUARE_BIT(p) (G_GUINT64_CONSTANT(1)<<(p))
enum { GAME_POINTS=64, GAME_CHOICES=64, GAME_PASS=64, GAME_MAX_DEPTH=10, GAME_MAX_ACTIONS=120 };
typedef enum { PLAYER_HUMAN, PLAYER_RANDOM, PLAYER_POSITIONAL, PLAYER_MINIMAX, PLAYER_MCTS, PLAYER_OLLAMA, PLAYER_COUNT } PlayerKind;
typedef struct { guint64 black,white; int turn; unsigned moves,passes; } Game;
typedef struct { int positional,mobility,frontier,discs,total; } PositionValue;
extern const char * const game_labels[64];
void game_init(Game *game);
unsigned game_count(guint64 bits);
guint64 game_legal_for(const Game *game,int player);
guint64 game_legal_moves(const Game *game);
guint64 game_flips(const Game *game,unsigned square);
gboolean game_play(Game *game,unsigned square); /* GAME_PASS only when forced. */
int game_result(const Game *game); /* 0 ongoing, +/-1 winner, 2 draw. */
int game_point(const char *label);
int game_random_move(guint64 legal,GRand *random);
void game_position(const Game *game,int player,PositionValue *value);
int game_positional_move(const Game *game,GRand *random,int values[64]);
const char *player_name(PlayerKind kind);
gboolean game_append_report(const char *path,const char *report,GError **error);
