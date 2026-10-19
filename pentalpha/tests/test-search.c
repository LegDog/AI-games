// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
/* Independent endpoint graph: for this puzzle the middle occupancy is irrelevant.
 * Reference recursion operates directly on masks, without engine move generation. */
static const unsigned endpoints[10][2] = {{0,6},{5,2},{2,8},{7,4},{4,5},{9,1},{1,7},{6,3},{3,9},{8,0}};
static int reference(unsigned occupied, unsigned depth)
{
    unsigned pieces = game_count(occupied), mobility = 0;
    if (pieces == 9) return 900;
    for (unsigned i = 0; i < 10; i++) if (!(occupied & ((1u << endpoints[i][0]) | (1u << endpoints[i][1])))) mobility += 2;
    if (!mobility || !depth) return pieces * 100 + (depth ? 0 : mobility);
    int best = -1;
    for (unsigned i = 0; i < 10; i++) if (!(occupied & ((1u << endpoints[i][0]) | (1u << endpoints[i][1]))))
        for (unsigned direction = 0; direction < 2; direction++)
            best = MAX(best, reference(occupied | (1u << endpoints[i][direction]), depth - 1));
    return best;
}
static gboolean reachable[1024];
static void check_state(Game game)
{
    if (reachable[game.occupied]) return;
    reachable[game.occupied] = TRUE;
    for (unsigned depth = 1; depth <= 3; depth++) {
        SearchResult result;
        g_assert_true(search_evaluate(&game, depth, NULL, &result));
        for (unsigned m = 0; m < 20; m++) if (game_legal(&game, m)) {
            unsigned next = game.occupied | (1u << game_moves[m].destination);
            g_assert_cmpint(result.values[m], ==, reference(next, depth - 1));
            Game child = game; g_assert_true(game_play(&child, m)); check_state(child);
        }
    }
    SearchResult exact; search_evaluate(&game, 9, NULL, &exact);
    int expected = reference(game.occupied, 9 - game.moves);
    for (unsigned m = 0; m < 20; m++) if (exact.best & (1u << m)) g_assert_cmpint(exact.values[m], ==, expected);
}
static void comparison(void)
{
    Game game; game_init(&game); check_state(game);
    unsigned states = 0;
    for (unsigned i = 0; i < 1024; i++) states += reachable[i];
    g_assert_cmpuint(states, >, 500);
}
static void solution(void)
{
    for (unsigned seed = 0; seed < 20; seed++) {
        Game game; game_init(&game);
        g_autoptr(GRand) random = g_rand_new_with_seed(seed);
        while (!game_result(&game)) {
            SearchResult result; g_assert_true(search_evaluate(&game, 9, NULL, &result));
            int move = search_choose(&result, random);
            g_assert_cmpint(result.values[move], ==, 900);
            g_assert_true(game_play(&game, move));
        }
        g_assert_cmpint(game_result(&game), ==, 1);
        g_assert_cmpuint(game.moves, ==, 9);
        while (game.moves) g_assert_true(game_undo(&game));
        g_assert_cmpuint(game.occupied, ==, 0);
    }
}
static gboolean cancelled(gpointer data) { (void) data; return TRUE; }
static void cancellation(void)
{
    Game game; game_init(&game);
    SearchResult result;
    SearchControl control = {.cancelled = cancelled};
    g_assert_false(search_evaluate(&game, 9, &control, &result));
    g_assert_true(result.cancelled);
    g_autoptr(GRand) random = g_rand_new_with_seed(1);
    g_assert_cmpint(search_choose(&result, random), ==, -1);
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/search/reference", comparison);
    g_test_add_func("/search/solution", solution);
    g_test_add_func("/search/cancel", cancellation);
    return g_test_run();
}
