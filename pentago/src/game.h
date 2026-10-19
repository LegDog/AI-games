// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>
#define GAME_ID "pentago"
#define GAME_NAME "AI Games: Pentago"
#define GAME_HISTORY_FILE GAME_ID "-history.md"
#define SQUARE_BIT(p) (G_GUINT64_CONSTANT(1) << (p))
#define GAME_BOARD_MASK ((G_GUINT64_CONSTANT(1) << 36) - 1)
enum { GAME_POINTS=36, GAME_CHOICES=288, GAME_MAX_DEPTH=6, GAME_MAX_ACTIONS=36, GAME_WIN_SCORE=1000000 };
typedef enum { PLAYER_HUMAN, PLAYER_RANDOM, PLAYER_POSITIONAL, PLAYER_MINIMAX, PLAYER_ALPHA_BETA, PLAYER_OLLAMA, PLAYER_COUNT } PlayerKind;
typedef enum { GAME_ONGOING=0, GAME_BLACK=1, GAME_WHITE=-1, GAME_DRAW=2 } GameResult;
/* Quadrants A..D in reading order; direction 0 = cw, 1 = ccw.
 * quadrant -1/direction 0 is the unique placement-only winning move. */
typedef struct { int cell, quadrant, direction; } GameMove;
typedef struct { GameMove moves[GAME_CHOICES]; unsigned count; } GameMoves;
typedef struct { guint64 black, white; int turn; unsigned actions; } Game;
typedef struct { int own_score, other_score, total; } PositionValue;
extern const char *const game_labels[GAME_POINTS];
void game_init(Game *game);
unsigned game_count(guint64 bits);
guint64 game_winning_path(const Game *game,int player);
GameResult game_result(const Game *game);
void game_legal_moves(const Game *game,GameMoves *moves);
gboolean game_move_equal(GameMove a,GameMove b);
gboolean game_move_legal(const Game *game,GameMove move);
gboolean game_play(Game *game,GameMove move);
/* Internal successor for a move already returned by game_legal_moves. */
void game_apply_legal(Game *game,GameMove move);
guint64 game_rotate(guint64 bits,unsigned quadrant,unsigned direction);
int game_rotated_cell(int cell,unsigned quadrant,unsigned direction);
int game_point(const char *label);
void game_move_label(GameMove move,char label[16]);
gboolean game_parse_move(const char *label,GameMove *move);
int game_random_move(const GameMoves *legal,GRand *random);
void game_position(const Game *game,int player,PositionValue *value);
int game_positional_move(const Game *game,GRand *random,int values[GAME_CHOICES]);
const char *player_name(PlayerKind kind);
gboolean game_append_report(const char *path,const char *report,GError **error);
