// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"
static void rules(void)
{
    Game game; game_init(&game);
    g_assert_cmpuint(game_legal_moves(&game), ==, (1u << 20) - 1);
    for (unsigned m = 0; m < 20; m++) {
        Move move = game_moves[m];
        gboolean found = FALSE;
        for (unsigned l = 0; l < 5; l++) for (unsigned offset = 0; offset < 2; offset++) {
            const unsigned *line = game_lines[l] + offset;
            if (move.middle == line[1] && ((move.entry == line[0] && move.destination == line[2]) ||
                (move.entry == line[2] && move.destination == line[0]))) found = TRUE;
        }
        g_assert_true(found);
        game_init(&game);
        g_assert_true(game_play(&game, m));
        g_assert_cmpuint(game.occupied, ==, 1u << move.destination);
        g_assert_cmpuint(game.moves, ==, 1);
        g_assert_false(game_legal(&game, m));
        g_assert_true(game_undo(&game));
        g_assert_cmpuint(game.occupied, ==, 0);
        game.occupied = 1u << move.entry; game.moves = 1;
        g_assert_false(game_play(&game, m));
        game.occupied = 1u << move.middle;
        g_assert_true(game_play(&game, m));
        g_assert_cmpuint(game.occupied, ==, (1u << move.middle) | (1u << move.destination));
        g_assert_cmpint(game_find_move(move.entry, move.destination), ==, (int) m);
    }
    game_init(&game);
    g_assert_false(game_play(&game, 20));
    g_assert_false(game_undo(&game));
    g_assert_cmpint(game_find_move(0, 2), ==, -1); /* Skipping both intersections. */
    g_assert_cmpint(game_find_move(0, 5), ==, -1); /* Adjacent point. */
    g_assert_cmpint(game_find_move(0, 7), ==, -1); /* Turn off the line. */
    g_assert_cmpint(game_point("v1"), ==, 0);
    g_assert_cmpint(game_point(NULL), ==, -1);
    g_assert_cmpint(game_point("v6"), ==, -1);
    game.occupied = 1023 ^ 1u; game.moves = 9;
    g_assert_cmpint(game_result(&game), ==, 1);
    game.occupied = 1023 ^ 3u; game.moves = 8;
    g_assert_cmpint(game_result(&game), ==, 2); /* Two adjacent vertices cannot be endpoints. */
}
static void random_moves(void)
{
    g_autoptr(GRand) random = g_rand_new_with_seed(42);
    unsigned counts[20] = {0};
    for (unsigned i = 0; i < 20000; i++) counts[game_random_move((1u << 20) - 1, random)]++;
    for (unsigned m = 0; m < 20; m++) { g_assert_cmpuint(counts[m], >, 800); g_assert_cmpuint(counts[m], <, 1200); }
    g_assert_cmpint(game_random_move(0, random), ==, -1);
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/game/rules", rules);
    g_test_add_func("/game/random", random_moves);
    return g_test_run();
}
