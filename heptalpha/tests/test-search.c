// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include "board.h"
#include <math.h>
/* Reference enumerates the seven ordered paths directly, with no move table or
 * transposition cache. Keep exhaustive comparisons to small remaining boards. */
static int reference(guint32 occupied, unsigned depth)
{
    unsigned pieces = game_count(occupied), mobility = 0;
    if (pieces == GAME_MAX_DEPTH) return pieces * 100;
    for (unsigned path = 0; path < GAME_PATHS; path++) for (unsigned offset = 0; offset < 4; offset++) {
        unsigned a = game_lines[path][offset], b = game_lines[path][offset+2];
        if (!(occupied & ((1u << a) | (1u << b)))) mobility += 2;
    }
    if (!mobility || !depth) return pieces * 100 + (depth ? 0 : mobility);
    int best = -1;
    for (unsigned path = 0; path < GAME_PATHS; path++) for (unsigned offset = 0; offset < 4; offset++) {
        unsigned a = game_lines[path][offset], b = game_lines[path][offset+2];
        if (occupied & ((1u << a) | (1u << b))) continue;
        best = MAX(best, reference(occupied | (1u << a), depth-1));
        best = MAX(best, reference(occupied | (1u << b), depth-1));
    }
    return best;
}
static void check_state(const Game *game)
{
    for (unsigned depth = 1; depth <= MIN(4u, GAME_MAX_DEPTH-game->moves); depth++) {
        SearchResult result; g_assert_true(search_evaluate(game, depth, NULL, &result));
        int best = -1;
        for (unsigned m = 0; m < GAME_CHOICES; m++) if (game_legal(game, m)) {
            int expected = reference(game->occupied | (1u << game_moves[m].destination), depth-1);
            g_assert_cmpint(result.values[m], ==, expected); best = MAX(best, expected);
        }
        for (unsigned m = 0; m < GAME_CHOICES; m++) if (result.best & MOVE_BIT(m)) g_assert_cmpint(result.values[m], ==, best);
    }
    if (game->moves >= 15) {
        SearchResult result; g_assert_true(search_evaluate(game, 20, NULL, &result));
        int expected = reference(game->occupied, GAME_MAX_DEPTH-game->moves);
        for (unsigned m = 0; m < GAME_CHOICES; m++) if (result.best & MOVE_BIT(m)) g_assert_cmpint(result.values[m], ==, expected);
    }
}
static void comparison(void)
{
    g_autoptr(GRand) random = g_rand_new_with_seed(73);
    for (unsigned sample = 0; sample < 80; sample++) {
        Game game; game_init(&game);
        while (!game_result(&game) && game.moves < 12 + sample % 8) game_play(&game, game_random_move(game_legal_moves(&game), random));
        check_state(&game);
    }
    /* A few shallow comparisons from the empty board exercise large branching. */
    Game game; game_init(&game);
    SearchResult result; search_evaluate(&game, 2, NULL, &result);
    for (unsigned m = 0; m < GAME_CHOICES; m++)
        g_assert_cmpint(result.values[m], ==, reference(1u << game_moves[m].destination, 1));
}
static void solution(void)
{
    for (unsigned seed = 0; seed < 20; seed++) {
        Game game; game_init(&game); g_autoptr(GRand) random = g_rand_new_with_seed(seed);
        while (!game_result(&game)) {
            SearchResult result; g_assert_true(search_evaluate(&game, 20, NULL, &result));
            int move = search_choose(&result, random);
            g_assert_cmpint(result.values[move], ==, 2000); g_assert_true(game_play(&game, move));
        }
        g_assert_cmpint(game_result(&game), ==, 1); g_assert_cmpuint(game.moves, ==, 20);
        while (game.moves) g_assert_true(game_undo(&game));
        g_assert_cmpuint(game.occupied, ==, 0);
    }
}
static void symmetry(void)
{
    Game solved; game_init(&solved);
    g_autoptr(GRand) random = g_rand_new_with_seed(42);
    while (!game_result(&solved)) {
        SearchResult result; search_evaluate(&solved, 20, NULL, &result);
        g_assert_true(game_play(&solved, search_choose(&result, random)));
    }
    for (unsigned reflection = 0; reflection < 2; reflection++) for (unsigned rotation = 0; rotation < 7; rotation++) {
        unsigned map[GAME_POINTS]; double angle = 2*G_PI*rotation/7;
        for (unsigned p = 0; p < GAME_POINTS; p++) {
            double x = board_points[p].x-251, y = board_points[p].y-251;
            if (reflection) x = -x;
            double rx = x*cos(angle)-y*sin(angle)+251, ry = x*sin(angle)+y*cos(angle)+251;
            unsigned nearest = 0; double distance = G_MAXDOUBLE;
            for (unsigned q = 0; q < GAME_POINTS; q++) {
                double d = hypot(rx-board_points[q].x, ry-board_points[q].y);
                if (d < distance) { distance = d; nearest = q; }
            }
            g_assert_cmpfloat(distance, <, 1e-8); map[p] = nearest;
        }
        for (unsigned m = 0; m < GAME_CHOICES; m++) {
            Move move = game_moves[m]; int mapped = game_find_move(map[move.entry], map[move.destination]);
            g_assert_cmpint(mapped, >=, 0); g_assert_cmpuint(game_moves[mapped].middle, ==, map[move.middle]);
        }
        Game replay; game_init(&replay);
        for (unsigned step = 0; step < GAME_MAX_DEPTH; step++) {
            Move move = solved.history[step]; int mapped = game_find_move(map[move.entry], map[move.destination]);
            g_assert_true(game_play(&replay, mapped));
            if (step == 15) {
                SearchResult result; g_assert_true(search_evaluate(&replay, 20, NULL, &result));
                for (unsigned m = 0; m < GAME_CHOICES; m++) if (result.best & MOVE_BIT(m)) g_assert_cmpint(result.values[m], ==, 2000);
            }
        }
        g_assert_cmpint(game_result(&replay), ==, 1);
    }
}
static gboolean cancelled(gpointer data) { unsigned *count = data; return ++*count >= 100; }
static void cancellation(void)
{
    Game game; game_init(&game); SearchResult result;
    unsigned calls = 0; SearchControl control = {.cancelled = cancelled, .data = &calls};
    g_assert_false(search_evaluate(&game, 20, &control, &result)); g_assert_true(result.cancelled);
    g_autoptr(GRand) random = g_rand_new_with_seed(1); g_assert_cmpint(search_choose(&result, random), ==, -1);
    guint64 allowed = 123; g_assert_false(search_allowed(&game, &control, &allowed)); g_assert_cmpuint(allowed, ==, 0);
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/search/reference", comparison); g_test_add_func("/search/solution", solution);
    g_test_add_func("/search/cancellation", cancellation); g_test_add_func("/search/symmetries", symmetry);
    return g_test_run();
}
