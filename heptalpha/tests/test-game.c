// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"
static void rules(void)
{
    Game game; game_init(&game);
    g_assert_cmpuint(game_legal_moves(&game), ==, (MOVE_BIT(GAME_CHOICES) - 1));
    guint64 seen = 0;
    for (unsigned m = 0; m < GAME_CHOICES; m++) {
        Move move = game_moves[m];
        gboolean found = FALSE;
        for (unsigned offset = 0; offset < 4; offset++) {
            const unsigned *path = game_lines[move.path] + offset;
            if (move.middle == path[1] && ((move.entry == path[0] && move.destination == path[2]) ||
                (move.entry == path[2] && move.destination == path[0]))) found = TRUE;
        }
        g_assert_true(found);
        g_assert_cmpint(game_find_move(move.entry, move.destination), ==, (int) m);
        seen |= MOVE_BIT(m);
        game_init(&game); g_assert_true(game_play(&game, m));
        g_assert_cmpuint(game.occupied, ==, 1u << move.destination);
        g_assert_cmpuint(game.history[0].path, ==, move.path);
        g_assert_false(game_play(&game, m));
        g_assert_true(game_undo(&game)); g_assert_cmpuint(game.occupied, ==, 0);
        game.occupied = 1u << move.entry; game.moves = 1;
        g_assert_false(game_play(&game, m));
        game.occupied = 1u << move.middle;
        g_assert_true(game_play(&game, m));
        g_assert_cmpuint(game.occupied, ==, (1u << move.middle) | (1u << move.destination));
    }
    g_assert_cmpuint(game_count(seen), ==, 56);
    game_init(&game); g_assert_false(game_play(&game, GAME_CHOICES)); g_assert_false(game_undo(&game));
    for (unsigned p = 0; p < GAME_POINTS; p++) g_assert_cmpint(game_point(game_labels[p]), ==, (int) p);
    g_assert_cmpint(game_point(NULL), ==, -1); g_assert_cmpint(game_point("v8"), ==, -1);
    for (unsigned path = 0; path < GAME_PATHS; path++) {
        g_assert_cmpint(game_find_move(game_lines[path][0], game_lines[path][1]), ==, -1);
        g_assert_cmpint(game_find_move(game_lines[path][0], game_lines[path][3]), ==, -1);
        g_assert_cmpint(game_find_move(game_lines[path][0], game_lines[path][5]), ==, -1);
    }
    game.occupied = ((1u << GAME_POINTS) - 1) ^ 1u; game.moves = GAME_MAX_DEPTH;
    g_assert_cmpint(game_result(&game), ==, 1);
    game.occupied ^= 2u; game.moves--; /* Two outer vertices are never a legal endpoint pair. */
    g_assert_cmpint(game_result(&game), ==, 2);
}
static void random_moves(void)
{
    g_autoptr(GRand) random = g_rand_new_with_seed(42);
    unsigned counts[GAME_CHOICES] = {0};
    for (unsigned i = 0; i < 56000; i++) counts[game_random_move(MOVE_BIT(GAME_CHOICES) - 1, random)]++;
    for (unsigned m = 0; m < GAME_CHOICES; m++) { g_assert_cmpuint(counts[m], >, 800); g_assert_cmpuint(counts[m], <, 1200); }
    g_assert_cmpint(game_random_move(0, random), ==, -1);
    for (unsigned m = 32; m < GAME_CHOICES; m++) g_assert_cmpint(game_random_move(MOVE_BIT(m), random), ==, (int) m);
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/game/rules", rules); g_test_add_func("/game/random", random_moves);
    return g_test_run();
}
