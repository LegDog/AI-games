// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game.h"
typedef struct { gboolean (*cancelled)(gpointer data); gpointer data; } SearchControl;
typedef struct {
    guint64 legal, best;
    int values[GAME_CHOICES];
    unsigned requested_depth, completed_depth;
    guint64 nodes;
    gint64 elapsed_us;
    gboolean cancelled;
} SearchResult;
gboolean search_evaluate(const Game *game, unsigned depth, const SearchControl *control, SearchResult *result);
int search_choose(const SearchResult *result, GRand *random);
/* Exact assistance must use the caller's cancellation control. */
gboolean search_allowed(const Game *game, const SearchControl *control, guint64 *allowed);
