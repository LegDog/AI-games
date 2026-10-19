// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <string.h>
static void replay(Game game, SearchResult result)
{
    guint64 initial = game.occupied; unsigned moves = game.moves;
    g_assert_cmpint(result.status,==,SEARCH_SOLVED);
    g_assert_cmpuint(result.length,==,game_count(game.occupied)-1);
    for (unsigned i = 0; i < result.length; i++) g_assert_true(game_play(&game,result.path[i]));
    g_assert_cmpint(game_result(&game),==,GAME_SOLVED);
    for (unsigned i = 0; i < result.length; i++) g_assert_true(game_undo(&game));
    g_assert_cmpuint(game.occupied,==,initial); g_assert_cmpuint(game.moves,==,moves);
}
static void presets(void)
{
    SearchLimits limits = {60,64u*1024*1024,0};
    for (unsigned kind = 0; kind < 2; kind++) for (unsigned goal = 0; goal < (kind == BOARD_ENGLISH ? 2u : 1u); goal++)
        for (unsigned algorithm = PLAYER_BACKTRACKING; algorithm <= PLAYER_ASTAR; algorithm++) {
            Game game; game_init(&game,kind,goal);
            SearchResult result = search_solve(&game,algorithm,&limits,NULL,0); replay(game,result);
            g_assert_cmpuint(result.peak_memory,<=,limits.memory_bytes);
        }
}
static gint8 reference_cache[1u << 15];
static gboolean reference(guint64 mask)
{
    if (game_count(mask) == 1) return TRUE;
    if (reference_cache[mask]) return reference_cache[mask] > 0;
    const Board *b = game_board(BOARD_TRIANGLE);
    /* Independent pairs of coordinate differences; no engine move enumeration. */
    for (unsigned source = 0; source < b->points; source++) if (mask & POINT_BIT(source))
        for (unsigned destination = 0; destination < b->points; destination++) if (!(mask & POINT_BIT(destination))) {
            int dr = b->row[destination]-b->row[source], dc = b->column[destination]-b->column[source];
            if (!((dr == 0 && ABS(dc) == 2) || (dc == 0 && ABS(dr) == 2) || (dr == dc && ABS(dr) == 2))) continue;
            for (unsigned middle = 0; middle < b->points; middle++)
                if (2*b->row[middle] == b->row[source]+b->row[destination] && 2*b->column[middle] == b->column[source]+b->column[destination] &&
                    (mask & POINT_BIT(middle)) && reference(mask ^ (POINT_BIT(source)|POINT_BIT(middle)|POINT_BIT(destination)))) {
                    reference_cache[mask] = 1; return TRUE;
                }
        }
    reference_cache[mask] = -1; return FALSE;
}
static void comparison(void)
{
    SearchLimits limits = {10,4u*1024*1024,0}; g_autoptr(GRand) random = g_rand_new_with_seed(71);
    for (unsigned i = 0; i < 100; i++) {
        Game game; game_init(&game,BOARD_TRIANGLE,GOAL_ANY);
        while (game_result(&game) == GAME_ONGOING && game.moves < i%11) {
            MoveList moves = game_legal_moves(&game); game_play(&game,game_random_move(&moves,random));
        }
        gboolean expected = reference(game.occupied);
        for (unsigned algorithm = PLAYER_BACKTRACKING; algorithm <= PLAYER_ASTAR; algorithm++) {
            SearchResult result = search_solve(&game,algorithm,&limits,NULL,i);
            g_assert_cmpint(result.status,==,expected ? SEARCH_SOLVED : SEARCH_UNSOLVABLE);
            if (expected) replay(game,result);
        }
    }
}
static gboolean canceled(gpointer data) { (void)data; return TRUE; }
static void limits_and_cancellation(void)
{
    Game game; game_init(&game,BOARD_ENGLISH,GOAL_ANY); SearchLimits limits = {60,16u*1024*1024,1};
    for (unsigned algorithm = PLAYER_BACKTRACKING; algorithm <= PLAYER_ASTAR; algorithm++) {
        SearchResult result = search_solve(&game,algorithm,&limits,NULL,1); g_assert_cmpint(result.status,==,SEARCH_LIMIT);
        limits.node_limit = 0; limits.memory_bytes = 1;
        result = search_solve(&game,algorithm,&limits,NULL,1); g_assert_cmpint(result.status,==,SEARCH_LIMIT);
        limits = (SearchLimits) {1e-12,16u*1024*1024,0};
        result = search_solve(&game,algorithm,&limits,NULL,1); g_assert_cmpint(result.status,==,SEARCH_LIMIT);
        limits = (SearchLimits) {60,16u*1024*1024,1};
        SearchControl control = {.cancelled = canceled}; result = search_solve(&game,algorithm,&limits,&control,1);
        g_assert_cmpint(result.status,==,SEARCH_CANCELLED);
    }
    game_init(&game,BOARD_ENGLISH,GOAL_CENTER); game.occupied = POINT_BIT(0);
    limits = search_defaults(); SearchResult result = search_solve(&game,PLAYER_BACKTRACKING,&limits,NULL,0);
    g_assert_cmpint(result.status,==,SEARCH_UNSOLVABLE);
}
static void certification(void)
{
    Game game; game_init(&game,BOARD_TRIANGLE,GOAL_ANY);
    SearchLimits limits = {10,4u*1024*1024,0}; MoveList certified;
    SearchResult result = search_certify(&game,&limits,NULL,0,&certified);
    g_assert_cmpint(result.status,==,SEARCH_SOLVED); g_assert_cmpuint(certified.count,>,0);
    for (unsigned i = 0; i < certified.count; i++) { Game child = game; game_play(&child,certified.items[i]); g_assert_true(reference(child.occupied)); }
    gboolean partial = FALSE;
    for (unsigned limit = 32; limit <= 1000 && !partial; limit += 16) {
        limits.node_limit = limit; result = search_certify(&game,&limits,NULL,0,&certified);
        partial = result.status == SEARCH_LIMIT && certified.count > 0;
    }
    g_assert_true(partial);
}
int main(int argc,char **argv)
{
    g_test_init(&argc,&argv,NULL); g_test_add_func("/search/presets",presets); g_test_add_func("/search/reference",comparison);
    g_test_add_func("/search/limits-cancel",limits_and_cancellation); g_test_add_func("/search/certification",certification); return g_test_run();
}
