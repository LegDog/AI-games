// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>
#define GAME_ID "hex"
#define GAME_NAME "AI Games: Hex"
#define GAME_HISTORY_FILE GAME_ID "-history.md"
#define SQUARE_BIT(p) (G_GUINT64_CONSTANT(1)<<(p))
/* Fixed stride seven keeps coordinates stable across board sizes. Bit 63 is swap. */
enum { GAME_POINTS=49, GAME_CHOICES=64, GAME_SWAP=63, GAME_MAX_DEPTH=10, GAME_MAX_ACTIONS=50 };
typedef enum { PLAYER_HUMAN,PLAYER_RANDOM,PLAYER_POSITIONAL,PLAYER_MINIMAX,PLAYER_MCTS,PLAYER_OLLAMA,PLAYER_COUNT } PlayerKind;
typedef struct { guint64 red,blue; int turn; unsigned size,moves,actions; gboolean swap_enabled,swapped; } Game;
typedef struct { int own_distance,other_distance,total; } PositionValue;
extern const char *const game_labels[64];
void game_init(Game *game);
gboolean game_init_variant(Game *game,unsigned size,gboolean swap);
unsigned game_count(guint64 bits);
guint64 game_board_mask(const Game *game);
guint64 game_neighbors(const Game *game,unsigned square);
guint64 game_winning_path(const Game *game,int player);
guint64 game_legal_moves(const Game *game);
gboolean game_play(Game *game,unsigned square);
int game_result(const Game *game); /* 0 ongoing; +1 Red (top-bottom); -1 Blue (left-right). */
int game_point(const char *label);
int game_random_move(guint64 legal,GRand *random);
int game_distance(const Game *game,int player);
void game_position(const Game *game,int player,PositionValue *value);
int game_positional_move(const Game *game,GRand *random,int values[64]);
const char *player_name(PlayerKind kind);
gboolean game_append_report(const char *path,const char *report,GError **error);
