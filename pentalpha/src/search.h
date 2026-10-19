// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game.h"
typedef struct { gboolean (*cancelled)(gpointer data); gpointer data; } SearchControl;
typedef struct {
    guint32 legal, best;
    int values[20];
    unsigned requested_depth, completed_depth;
    guint64 nodes;
    gint64 elapsed_us;
    gboolean cancelled;
} SearchResult;
gboolean search_evaluate(const Game *game, unsigned depth, const SearchControl *control, SearchResult *result);
int search_choose(const SearchResult *result, GRand *random);
/* Search assistance is explicit; pure mode never calls this function. */
guint32 search_allowed(const Game *game);
