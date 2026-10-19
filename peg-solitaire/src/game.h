// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>
#define GAME_ID "peg-solitaire"
#define GAME_NAME "AI Games: Peg Solitaire"
#define GAME_HISTORY_FILE "peg-solitaire-history.md"
enum { MAX_POINTS = 33, MAX_MOVES = 76, MAX_JUMPS = 31 };
#define POINT_BIT(p) (G_GUINT64_CONSTANT(1) << (p))
typedef enum { BOARD_ENGLISH, BOARD_TRIANGLE } BoardKind;
typedef enum { GOAL_ANY, GOAL_CENTER } GoalKind;
typedef enum { PLAYER_HUMAN, PLAYER_RANDOM, PLAYER_HEURISTIC, PLAYER_BACKTRACKING, PLAYER_ASTAR, PLAYER_OLLAMA } PlayerKind;
typedef enum { GAME_ONGOING, GAME_SOLVED, GAME_STUCK, GAME_WRONG_FINISH } GameResult;
typedef struct { unsigned source, middle, destination; } Move;
typedef struct {
    const char *name;
    unsigned points, choices, opening, symmetries;
    char labels[MAX_POINTS][4];
    int row[MAX_POINTS], column[MAX_POINTS];
    double x[MAX_POINTS], y[MAX_POINTS];
    guint64 neighbors[MAX_POINTS], full;
    Move jumps[MAX_MOVES];
    unsigned transform[8][MAX_POINTS];
    guint64 transform_bytes[8][5][256];
} Board;
typedef struct {
    BoardKind board;
    GoalKind goal;
    guint64 occupied;
    unsigned moves;
    unsigned history[MAX_JUMPS];
} Game;
typedef struct { unsigned count, items[MAX_MOVES]; } MoveList;
const Board *game_board(BoardKind kind);
gboolean game_init(Game *game, BoardKind board, GoalKind goal);
unsigned game_count(guint64 mask);
int game_point(const Board *board, const char *label);
int game_find_move(const Board *board, unsigned source, unsigned destination);
gboolean game_legal(const Game *game, unsigned move);
MoveList game_legal_moves(const Game *game);
GameResult game_result(const Game *game);
gboolean game_play(Game *game, unsigned move);
gboolean game_undo(Game *game);
int game_random_move(const MoveList *moves, GRand *random);
gint64 game_heuristic(const Game *game, unsigned move);
gint64 game_position_heuristic(const Game *game);
guint64 game_canonical(const Game *game);
const char *player_name(PlayerKind kind);
const char *game_result_name(GameResult result);
gboolean game_append_report(const char *path, const char *report, GError **error);
