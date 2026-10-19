// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game.h"
typedef enum {
  SEARCH_COMPLETE,
  SEARCH_LIMIT,
  SEARCH_CANCELLED,
  SEARCH_ERROR
} SearchStatus;
typedef struct {
  unsigned iterations;
  double seconds;
  gsize memory_bytes;
  guint64 node_limit;
} SearchOptions;
typedef struct {
  gboolean (*cancelled)(gpointer);
  void (*progress)(unsigned, guint64, gpointer);
  gpointer data;
} SearchControl;
typedef struct {
  SearchStatus status;
  unsigned requested, completed, tree_nodes, cutoffs;
  guint64 nodes;
  unsigned visits[GO_CHOICES];
  double means[GO_CHOICES];
  gint64 elapsed_us;
  gsize peak_memory;
} SearchResult;
SearchOptions search_defaults(void);
gboolean mcts_evaluate(const Game *game, const SearchOptions *options,
                       const SearchControl *control, GRand *random,
                       SearchResult *result);
int search_choose(const SearchResult *result, GRand *random);
const char *search_status_name(SearchStatus status);
