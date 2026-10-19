// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <string.h>
typedef struct {
    int cache[10][1024];
    const SearchControl *control;
    SearchResult *result;
} Search;
static int visit(Search *search, const Game *game, unsigned depth)
{
    search->result->nodes++;
    if (search->control && search->control->cancelled && search->control->cancelled(search->control->data)) {
        search->result->cancelled = TRUE;
        return 0;
    }
    guint32 legal = game_legal_moves(game);
    if (!legal || !depth) return (int) game->moves * 100 + (depth ? 0 : (int) game_count(legal));
    int *cached = &search->cache[depth][game->occupied];
    if (*cached >= 0) return *cached;
    int best = -1;
    for (unsigned m = 0; m < 20; m++) if (legal & (1u << m)) {
        Game next = *game;
        game_play(&next, m);
        int value = visit(search, &next, depth - 1);
        if (search->result->cancelled) return 0;
        best = MAX(best, value);
    }
    *cached = best;
    return best;
}
gboolean search_evaluate(const Game *game, unsigned depth, const SearchControl *control, SearchResult *result)
{
    memset(result, 0, sizeof *result);
    result->requested_depth = CLAMP(depth, 1, 9);
    gint64 start = g_get_monotonic_time();
    Search search = {.control = control, .result = result};
    memset(search.cache, -1, sizeof search.cache);
    result->legal = game_legal_moves(game);
    unsigned remaining = MIN(result->requested_depth, 9 - game->moves);
    int best = -1;
    for (unsigned m = 0; m < 20; m++) if (result->legal & (1u << m)) {
        Game next = *game;
        game_play(&next, m);
        result->values[m] = visit(&search, &next, remaining - 1);
        if (result->cancelled) break;
        if (result->values[m] > best) { best = result->values[m]; result->best = 0; }
        if (result->values[m] == best) result->best |= 1u << m;
    }
    if (!result->cancelled) result->completed_depth = remaining;
    result->elapsed_us = g_get_monotonic_time() - start;
    return !result->cancelled;
}
int search_choose(const SearchResult *result, GRand *random)
{
    return result->cancelled ? -1 : game_random_move(result->best, random);
}
guint32 search_allowed(const Game *game)
{
    SearchResult result;
    search_evaluate(game, 9, NULL, &result);
    return result.best;
}
