// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "search.h"
typedef struct { guint64 sibling_prunes, cache_hits; } SymmetryStats;
/* Independent variation: the legacy search_evaluate implementation is untouched. */
gboolean search_symmetry_evaluate(const Game *game,unsigned depth,const SearchLimits *limits,const SearchControl *control,SearchResult *result,SymmetryStats *stats);
