// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"
#include "search.h"
static void empty(Game *g) { *g = (Game){.turn = 1, .roll = -1}; }
static void rules(void) {
  Game g;
  game_init(&g);
  Game initial = g;
  g_assert_false(game_play(&g, (GameMove){0}));
  g_assert_false(game_set_roll(&g, 5));
  g_assert_cmpmem(&g, sizeof g, &initial, sizeof initial);
  g_assert_true(game_set_roll(&g, 4));
  g_assert_false(game_set_roll(&g, 2));
  g_assert_true(game_play(&g, (GameMove){0}));
  g_assert_cmpuint(g.reserve[0], ==, 6);
  g_assert_cmpint(g.turn, ==, 1);
  g_assert_cmpint(g.roll, ==, -1);
  game_set_roll(&g, 4);
  g_assert_false(game_play(&g, (GameMove){0}));
  g_assert_true(game_play(&g, (GameMove){4}));
  g_assert_cmpint(g.turn, ==, 1);
  g_assert_cmpint(g.piece[0][8], ==, 1);
  g.turn = 2;
  game_set_roll(&g, 4);
  g.piece[1][4] = 1;
  g_assert_false(game_move_legal(&g, (GameMove){4}));
  empty(&g);
  g.piece[0][5] = 1;
  g.piece[1][7] = 1;
  game_set_roll(&g, 2);
  g_assert_true(game_play(&g, (GameMove){5}));
  g_assert_cmpint(g.piece[1][7], ==, 0);
  g_assert_cmpint(g.reserve[1], ==, 1);
  g_assert_cmpint(g.turn, ==, 2);
  empty(&g);
  g.piece[0][13] = 1;
  g.piece[1][13] = 1;
  game_set_roll(&g, 1);
  game_play(&g, (GameMove){13});
  g_assert_cmpint(g.piece[1][13], ==, 1);
  g_assert_cmpint(g.turn, ==, 1);
  empty(&g);
  g.finished[0] = 6;
  g.piece[0][14] = 1;
  game_set_roll(&g, 2);
  GameMoves m;
  game_legal_moves(&g, &m);
  g_assert_cmpuint(m.count, ==, 1);
  g_assert_cmpint(m.moves[0].cell, ==, -1);
  game_play(&g, m.moves[0]);
  g.turn = 1;
  game_set_roll(&g, 1);
  g_assert_true(game_play(&g, (GameMove){14}));
  g_assert_cmpint(game_result(&g), ==, 1);
  g_assert_false(game_set_roll(&g, 1));
  game_legal_moves(&g, &m);
  g_assert_cmpuint(m.count, ==, 0);
  game_init(&g);
  game_set_roll(&g, 0);
  g_assert_false(game_play(&g, (GameMove){0}));
  g_assert_true(game_play(&g, (GameMove){-1}));
  g_assert_cmpint(g.turn, ==, 2);
  GameMove move;
  g_assert_false(game_parse_move("01", &move));
  g_assert_false(game_parse_move(NULL, &move));
  g_assert_false(game_parse_move("15", &move));
  g_assert_true(game_parse_move("pass", &move));
}
static void conservation(void) {
  GRand *dice = g_rand_new_with_seed(41), *rng = g_rand_new_with_seed(42);
  for (int n = 0; n < 100; n++) {
    Game g;
    game_init(&g);
    while (!game_result(&g) && g.actions < 10000) {
      game_roll(&g, dice);
      GameMoves moves;
      game_legal_moves(&g, &moves);
      g_assert_cmpuint(moves.count, >, 0);
      game_play(&g, moves.moves[g_rand_int_range(rng, 0, moves.count)]);
      for (int s = 0; s < 2; s++) {
        int count = g.reserve[s] + g.finished[s];
        for (int p = 1; p <= 14; p++) {
          count += g.piece[s][p];
          g_assert_cmpint(g.piece[s][p], <=, 1);
        }
        g_assert_cmpint(count, ==, 7);
      }
      for (int p = 5; p <= 12; p++)
        g_assert_false(g.piece[0][p] && g.piece[1][p]);
    }
    g_assert_cmpint(game_result(&g), >, 0);
  }
  g_rand_free(dice);
  g_rand_free(rng);
}
static void search(void) {
  Game g;
  empty(&g);
  g.finished[0] = 6;
  g.piece[0][14] = 1;
  game_set_roll(&g, 1);
  GRand *rng = g_rand_new_with_seed(1);
  g_assert_cmpint(search_choose(&g, 3, rng).cell, ==, 14);
  g.roll = -1;
  double expected = 0;
  const int weights[] = {1, 4, 6, 4, 1};
  for (int r = 0; r < 5; r++) {
    Game next = g;
    game_set_roll(&next, r);
    expected += weights[r] * search_value(&next, 1, 1) / 16.;
  }
  g_assert_cmpfloat_with_epsilon(search_value(&g, 1, 1), expected, 1e-9);
  g_rand_free(rng);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/ur/rules", rules);
  g_test_add_func("/ur/conservation", conservation);
  g_test_add_func("/ur/search", search);
  return g_test_run();
}
