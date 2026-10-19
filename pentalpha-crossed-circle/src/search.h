// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game.h"
typedef enum { SEARCH_COMPLETE, SEARCH_LIMIT, SEARCH_CANCELLED, SEARCH_ERROR } SearchStatus;
typedef struct { double seconds; gsize memory_bytes; guint64 node_limit; } SearchLimits;
typedef struct {
    gboolean (*cancelled)(gpointer data);
    void (*progress)(guint64 nodes, unsigned depth, gint64 elapsed_us, gsize memory_bytes, gpointer data);
    gpointer data;
} SearchControl;
typedef struct {
    MoveList legal,best;
    int values[GAME_CHOICES];
    unsigned requested_depth,completed_depth;
    guint64 nodes;
    gint64 elapsed_us;
    gsize peak_memory;
    SearchStatus status;
} SearchResult;
SearchLimits search_defaults(void);
const char *search_status_name(SearchStatus status);
/* TRUE means at least one complete iteration is available, never a partial root. */
gboolean search_evaluate(const Game *game,unsigned depth,const SearchLimits *limits,const SearchControl *control,SearchResult *result);
int search_choose(const SearchResult *result,GRand *random);
