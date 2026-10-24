// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game.h"
typedef struct {
  GameMove move;
  unsigned depth;
  guint64 nodes;
} SearchResult;
/* Iterative deepening; only complete iterations replace the previous choice. */
SearchResult search_choose(const Game *g, unsigned depth, guint64 budget,
                           gboolean alpha_beta, GRand *rng);
double search_value(const Game *g, int role, unsigned depth);

SearchResult search_choose_full(const Game *g, unsigned depth, guint64 budget,
                                gboolean alpha_beta, GRand *rng,
                                gboolean (*cancelled)(gpointer),
                                gpointer cancel_data);
