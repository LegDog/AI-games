#pragma once

#include <glib.h>

typedef struct {
    int cells[3][3];
    int turn;
    unsigned moves;
} Game;

typedef struct {
    gboolean empty;
    unsigned wins, blocks, own_lines, opponent_lines;
    unsigned center_bonus;
    unsigned fork, opponent_forks, immediate_loss;
    unsigned open_lines; /* Considered from the second move; zero on an empty board. */
    double random, total;
} Score;

void game_init(Game *game);
/* First winning row/column/diagonal, or NULL. Owned by the engine. */
const unsigned *game_winning_line(const Game *game);
/* 0: ongoing; 1/-1: winner; 2: draw. */
int game_result(const Game *game);
gboolean game_play(Game *game, unsigned cell);
void game_evaluate(const Game *game, GRand *random, Score scores[9]);
int game_best_move(const Score scores[9]);
char game_symbol(int player);
void game_record(GString *history, const Game *before, const Score scores[9],
                 unsigned cell, gboolean human);
/* Returns NULL for a draw or unfinished game. Caller owns the result. */
char *game_report(const Game *game, const char *history, const char *configuration,
                  GDateTime *finished_at);
gboolean game_append_report(const char *path, const char *report, GError **error);
