// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game.h"
typedef enum { SEARCH_SOLVED, SEARCH_UNSOLVABLE, SEARCH_LIMIT, SEARCH_CANCELLED, SEARCH_ERROR } SearchStatus;
typedef struct {
    double seconds;
    gsize memory_bytes;
    guint64 node_limit; /* Optional deterministic experiment/test limit; zero disables. */
} SearchLimits;
typedef struct {
    gboolean (*cancelled)(gpointer data);
    void (*progress)(guint64 nodes, gint64 elapsed_us, gsize memory_bytes, gpointer data);
    gpointer data;
} SearchControl;
typedef struct {
    SearchStatus status;
    unsigned path[MAX_JUMPS], length;
    guint64 nodes;
    gint64 elapsed_us;
    gsize peak_memory;
} SearchResult;
SearchLimits search_defaults(void);
SearchResult search_solve(const Game *game, PlayerKind algorithm, const SearchLimits *limits, const SearchControl *control, guint32 seed);
/* Certify first jumps with winning continuations; unresolved candidates remain unknown. */
SearchResult search_certify(const Game *game, const SearchLimits *limits, const SearchControl *control, guint32 seed, MoveList *certified);
const char *search_status_name(SearchStatus status);
