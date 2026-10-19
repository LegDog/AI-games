// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <string.h>
typedef struct {
    guint16 *cache;
    const SearchControl *control;
    SearchResult *result;
} Search;
static gboolean stopped(Search *search)
{
    if (search->control && search->control->cancelled && search->control->cancelled(search->control->data))
        search->result->cancelled = TRUE;
    return search->result->cancelled;
}
static int visit(Search *search, guint32 occupied, unsigned pieces, unsigned depth)
{
    search->result->nodes++;
    if (stopped(search)) return 0;
    if (pieces == GAME_MAX_DEPTH) return GAME_MAX_DEPTH * 100;
    guint16 *cached = &search->cache[occupied];
    if (*cached) return *cached - 1;
    Game game = {.occupied = occupied, .moves = pieces};
    guint64 legal = game_legal_moves(&game);
    if (!legal || !depth) return pieces * 100 + (depth ? 0 : game_count(legal));
    int best = -1;
    guint32 seen_destinations = 0;
    for (unsigned m = 0; m < GAME_CHOICES; m++) if (legal & MOVE_BIT(m)) {
        guint32 bit = 1u << game_moves[m].destination;
        if (seen_destinations & bit) continue;
        seen_destinations |= bit;
        best = MAX(best, visit(search, occupied | bit, pieces + 1, depth - 1));
        if (search->result->cancelled) return 0;
        if (best == GAME_MAX_DEPTH * 100) break; /* Proven global upper bound. */
    }
    *cached = best + 1;
    return best;
}
gboolean search_evaluate(const Game *game, unsigned depth, const SearchControl *control, SearchResult *result)
{
    memset(result, 0, sizeof *result);
    result->requested_depth = CLAMP(depth, 1, GAME_MAX_DEPTH);
    gint64 start = g_get_monotonic_time();
    Search search = {.control = control, .result = result};
    if (stopped(&search)) goto done;
    result->legal = game_legal_moves(game);
    if (!result->legal) goto done;
    /* For a fixed root/depth, popcount determines the remaining depth, so a
     * single 4 MiB occupancy table suffices. Never allocate this on the stack. */
    search.cache = g_new0(guint16, 1u << GAME_POINTS);
    unsigned remaining = MIN(result->requested_depth, GAME_MAX_DEPTH - game->moves);
    int best = -1, destination_values[GAME_POINTS];
    for (unsigned p = 0; p < GAME_POINTS; p++) destination_values[p] = -1;
    for (unsigned m = 0; m < GAME_CHOICES; m++) if (result->legal & MOVE_BIT(m)) {
        unsigned destination = game_moves[m].destination;
        int *value = &destination_values[destination];
        if (*value < 0) *value = visit(&search, game->occupied | (1u << destination), game->moves + 1, remaining - 1);
        if (result->cancelled) break;
        result->values[m] = *value;
        if (*value > best) { best = *value; result->best = 0; }
        if (*value == best) result->best |= MOVE_BIT(m);
    }
    if (!result->cancelled) result->completed_depth = remaining;
done:
    result->elapsed_us = g_get_monotonic_time() - start;
    g_free(search.cache);
    return !result->cancelled;
}
int search_choose(const SearchResult *result, GRand *random)
{
    return result->cancelled ? -1 : game_random_move(result->best, random);
}
gboolean search_allowed(const Game *game, const SearchControl *control, guint64 *allowed)
{
    SearchResult result;
    gboolean ok = search_evaluate(game, GAME_MAX_DEPTH, control, &result);
    *allowed = ok ? result.best : 0;
    return ok;
}
