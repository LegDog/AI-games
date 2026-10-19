// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game.h"
typedef enum { SEARCH_COMPLETE, SEARCH_LIMIT, SEARCH_CANCELLED, SEARCH_ERROR } SearchStatus;
typedef struct { unsigned depth; double seconds; gsize memory_bytes; guint64 node_limit; } SearchOptions;
typedef struct {
    gboolean (*cancelled)(gpointer data);
    void (*progress)(unsigned completed,guint64 nodes,gpointer data);
    gpointer data;
} SearchControl;
typedef struct {
    SearchStatus status;
    GameMoves legal;
    gboolean best[GAME_CHOICES];
    int values[GAME_CHOICES];
    unsigned requested,completed;
    guint64 nodes,leaves,cutoffs;
    gint64 elapsed_us;
    gsize peak_memory;
} SearchResult;
SearchOptions search_defaults(void);
const char *search_status_name(SearchStatus status);
gboolean minimax_evaluate(const Game *game,gboolean pruning,const SearchOptions *options,const SearchControl *control,SearchResult *result);
int search_choose(const SearchResult *result,GRand *random);
