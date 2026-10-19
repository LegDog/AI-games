// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "search.h"
typedef struct {
    guint64 empty, adjacent[GAME_POINTS], components[GAME_POINTS];
    unsigned component_count, edge_count, max_final_pieces;
    int matrix_rank; /* -1 unless elimination was actually performed. */
} GraphAnalysis;
typedef enum { GRAPH_COMPLETE, GRAPH_CANCELLED, GRAPH_ERROR } GraphStatus;
typedef struct {
    GraphAnalysis analysis;
    GraphStatus status;
    MoveList best;
    int values[GAME_CHOICES]; /* Exact final-piece count, not the legacy score. */
    unsigned plan[GAME_MAX_DEPTH], length;
    guint64 xor_operations;
    gint64 elapsed_us;
    gsize peak_memory;
} GraphResult;
gboolean graph_controller(PlayerKind kind);
gboolean forest_controller(PlayerKind kind);
gboolean graph_analyze(const Game *game, GraphAnalysis *analysis);
gboolean graph_connectivity(const Game *game, const SearchControl *control, GraphResult *result);
gboolean graph_forest(const Game *game, gboolean gf2, GRand *random, const SearchControl *control, GraphResult *result);
