// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game.h"
typedef enum {
  SEARCH_SOLVED,
  SEARCH_UNSOLVABLE,
  SEARCH_LIMIT,
  SEARCH_CANCELLED,
  SEARCH_ERROR
} SearchStatus;
typedef struct {
  double seconds;
  gsize memory_bytes;
  guint64 node_limit;
  gboolean pruning;
} SearchLimits;
typedef struct {
  gboolean (*cancelled)(gpointer);
  void (*progress)(guint64, gint64, gsize, gpointer);
  gpointer data;
} SearchControl;
typedef struct {
  SearchStatus status;
  GArray *path;
  guint64 nodes;
  gint64 elapsed_us;
  gsize peak_memory;
  gboolean optimal;
} SearchResult;
SearchLimits search_defaults(void);
SearchResult search_solve(const Game *game, PlayerKind kind,
                          const SearchLimits *limits,
                          const SearchControl *control, guint32 seed);
void search_result_clear(SearchResult *result);
const char *search_status_name(SearchStatus status);
