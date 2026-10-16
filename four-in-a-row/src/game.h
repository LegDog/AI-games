// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <glib.h>
#include "player.h"

#define GAME_ID "four-in-a-row"
#define GAME_NAME "Four in a Row"
#define GAME_HISTORY_FILE GAME_ID "-history.md"

enum { GAME_ROWS = 6, GAME_COLS = 7, GAME_MAX_LINES = 69, GAME_CHOICES = 7, GAME_MAX_DEPTH = 12 };

typedef struct {
    int cells[GAME_ROWS][GAME_COLS];
    int turn;
    unsigned moves;
} Game;

typedef struct {
    unsigned row1, col1, row2, col2;
} WinningLine;

typedef struct {
    unsigned empty_windows, own_windows[5], opponent_windows[5];
    unsigned own_ready, opponent_ready, own_supported, opponent_supported;
    double own_potential, opponent_potential, total;
} PositionValue;

typedef struct {
    gboolean legal;
    int row;
    unsigned win, block, fork, opponent_forks, immediate_loss;
    unsigned stacked, opponent_stacked;
    unsigned forcing_win, forcing_loss, forcing_limited;
    unsigned prepared_win, prepared_loss;
    PositionValue position;
    double random, total;
} Score;

/* Tactical terms dominate the sum of all positional windows. */
#define SCORE_WIN 1000000000.0
#define SCORE_BLOCK 10000000.0
#define SCORE_FORK 100000.0
#define SCORE_FORK_RISK 100000.0
#define SCORE_IMMEDIATE_LOSS 100000000.0
#define SCORE_DEFENSE_FACTOR 1.25
#define SCORE_POSITIONAL_SCALE 0.1
#define SCORE_FORCING_WIN 1000000.0
#define SCORE_FORCING_LOSS 20000000.0
enum { FORCING_MAX_PLIES = 12, FORCING_NODE_BUDGET = 1024 };
enum { PREPARATION_MAX_MOVES = 2, PREPARATION_NODE_BUDGET = 4096 };

void game_init(Game *game);
/* Lowest empty row, or -1 for a full/invalid column. */
int game_drop_row(const Game *game, unsigned column);
gboolean game_play(Game *game, unsigned column);
/* 0 ongoing, 1/-1 winner, 2 draw. */
int game_result(const Game *game);
/* Maximal winning segments, including runs longer than four; output may be NULL. */
unsigned game_winning_lines(const Game *game, WinningLine output[GAME_MAX_LINES]);
/* All 69 windows, from the supplied player's perspective, with gravity discounts. */
void game_position_evaluate(const Game *game, int player, PositionValue *value);
void game_evaluate(const Game *game, GRand *random, Score scores[GAME_COLS]);
int game_best_move(const Score scores[GAME_COLS]);
char game_symbol(int player);
const char *game_player_name(int player);
void game_record(GString *history, const Game *before, const Score scores[GAME_COLS],
                 unsigned column, PlayerKind controller);
char *game_report(const Game *game, const char *history, const char *configuration,
                  GDateTime *finished_at);
gboolean game_append_report(const char *path, const char *report, GError **error);
