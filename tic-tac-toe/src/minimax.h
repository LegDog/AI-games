// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game.h"

typedef struct {
    gboolean (*cancelled)(gpointer data);
    void (*progress)(unsigned completed, gpointer data);
    gpointer data;
    gboolean disable_cache, disable_ordering;
} SearchControl;

typedef struct {
    gboolean legal[GAME_CHOICES];
    double values[GAME_CHOICES];
    int best_move;
    unsigned requested_depth, completed_depth;
    guint64 nodes;
    gint64 elapsed_us;
    gboolean cancelled, opening_random;
} MinimaxResult;

/* The result contains exact full-window root values at the completed depth.
 * Pure search: no random generator or GTK objects are accessed. */
gboolean minimax_evaluate(const Game *game, unsigned depth, const SearchControl *control,
                         MinimaxResult *result);
int minimax_choose(const MinimaxResult *result, GRand *random);
void minimax_record(GString *history, const MinimaxResult *result);
