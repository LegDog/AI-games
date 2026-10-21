// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"
#include "search.h"
#include <math.h>
static Game position(void) { return (Game){.turn = 1, .roll = -1}; }
static void rules(void) {
  Game g;
  game_init(&g);
  Game initial = g;
  g_assert_false(game_play(&g, (GameMove){1}));
  g_assert_false(game_set_roll(&g, 0));
  g_assert_false(game_set_roll(&g, 6));
  g_assert_cmpmem(&g, sizeof g, &initial, sizeof initial);
  game_set_roll(&g, 1);
  g_assert_false(game_set_roll(&g, 4));
  g_assert_true(game_play(&g, (GameMove){1}));
  g_assert_cmpint(g.cells[1], ==, 2);
  g_assert_cmpint(g.cells[2], ==, 1);
  g_assert_cmpint(g.turn, ==, 1);
  g_assert_cmpint(g.roll, ==, -1);
  g = position();
  g.cells[10] = 1;
  g.cells[12] = 2;
  g.cells[13] = 2;
  game_set_roll(&g, 2);
  g_assert_false(game_move_legal(&g, (GameMove){10}));
  g_assert_true(game_play(&g, (GameMove){-1}));
  g_assert_cmpint(g.turn, ==, 2);
  g = position();
  g.cells[8] = 1;
  g.cells[10] = 2;
  g.cells[11] = 2;
  game_set_roll(&g, 2);
  g_assert_true(game_protected(&g, 10));
  g_assert_false(game_move_legal(&g, (GameMove){8}));
  g = position();
  g.cells[24] = 1;
  game_set_roll(&g, 3);
  g_assert_false(game_move_legal(&g, (GameMove){24}));
  g.roll = -1;
  game_set_roll(&g, 2);
  game_play(&g, (GameMove){24});
  g_assert_cmpint(g.cells[26], ==, 1);
  g_assert_cmpint(g.turn, ==, 2);
  g.turn = 1;
  game_set_roll(&g, 5);
  g_assert_false(game_move_legal(&g, (GameMove){26}));
  g.roll = -1;
  game_set_roll(&g, 1);
  game_play(&g, (GameMove){26});
  g_assert_cmpint(g.cells[15], ==, 1);
  g_assert_cmpint(g.cells[27], ==, 0);
  g_assert_cmpint(g.turn, ==, 1);
  g = position();
  g.cells[26] = 1;
  g.cells[15] = 2;
  g.cells[14] = 2;
  game_set_roll(&g, 1);
  g_assert_cmpint(game_destination(&g, (GameMove){26}), ==, 13);
  game_play(&g, (GameMove){26});
  g_assert_cmpint(g.cells[13], ==, 1);
  g_assert_cmpint(g.cells[15], ==, 2);
  for (int source = 28; source <= 30; source++) {
    for (int roll = 1; roll <= 5; roll++) {
      g = position();
      g.finished[0] = 4;
      g.cells[source] = 1;
      game_set_roll(&g, roll);
      g_assert_cmpint(game_move_legal(&g, (GameMove){source}), ==,
                      roll == 31 - source);
      if (roll == 31 - source) {
        g_assert_true(game_play(&g, (GameMove){source}));
        g_assert_cmpint(game_result(&g), ==, 1);
        g_assert_false(game_set_roll(&g, 1));
        GameMoves moves;
        game_legal_moves(&g, &moves);
        g_assert_cmpuint(moves.count, ==, 0);
      }
    }
  }
  g = position();
  g.cells[20] = 1;
  game_set_roll(&g, 4);
  g_assert_false(game_play(&g, (GameMove){-1}));
  GameMove move;
  const char *invalid[] = {NULL, "0", "31", "01", "-2", "3 ", "PASS"};
  for (unsigned i = 0; i < G_N_ELEMENTS(invalid); i++)
    g_assert_false(game_parse_move(invalid[i], &move));
  g_assert_true(game_parse_move("30", &move));
  g_assert_cmpint(move.cell, ==, 30);
}
static void sticks(void) {
  int counts[6] = {0};
  for (int mask = 0; mask < 16; mask++)
    counts[game_sticks_value(mask)]++;
  int expected[] = {0, 4, 6, 4, 1, 1};
  for (int r = 0; r <= 5; r++)
    g_assert_cmpint(counts[r], ==, expected[r]);
  g_assert_cmpint(game_sticks_value(16), ==, -1);
  GRand *dice = g_rand_new_with_seed(1);
  Game g;
  game_init(&g);
  game_roll(&g, dice);
  Game before = g;
  g_assert_cmpint(game_roll(&g, dice), ==, -1);
  g_assert_cmpmem(&g, sizeof g, &before, sizeof before);
  g_rand_free(dice);
}
static void conservation(void) {
  GRand *dice = g_rand_new_with_seed(41), *rng = g_rand_new_with_seed(42);
  for (int n = 0; n < 100; n++) {
    Game g;
    game_init(&g);
    while (!game_result(&g) && g.actions < 10000) {
      game_roll(&g, dice);
      GameMoves legal;
      game_legal_moves(&g, &legal);
      g_assert_cmpuint(legal.count, >, 0);
      g_assert_cmpuint(legal.count, <=, 5);
      game_play(&g, legal.moves[g_rand_int_range(rng, 0, legal.count)]);
      int count[2] = {g.finished[0], g.finished[1]};
      for (int p = 1; p <= 30; p++) {
        g_assert_cmpint(g.cells[p], <=, 2);
        if (g.cells[p])
          count[g.cells[p] - 1]++;
      }
      g_assert_cmpint(count[0], ==, 5);
      g_assert_cmpint(count[1], ==, 5);
      g_assert_cmpint(g.cells[27], ==, 0);
    }
    g_assert_cmpint(game_result(&g), >, 0);
  }
  g_rand_free(dice);
  g_rand_free(rng);
}
/* Independent chance enumeration and leaf scoring: no production weights. */
static double reference(const Game *g, int player, unsigned depth) {
  int winner = game_result(g);
  if (winner)
    return winner == player ? 10000 : -10000;
  if (!depth) {
    double score[2] = {g->finished[0] * 50., g->finished[1] * 50.};
    for (int p = 1; p <= 30; p++)
      if (g->cells[p]) {
        gboolean protected = (p > 1 && g->cells[p - 1] == g->cells[p]) ||
                             (p < 30 && g->cells[p + 1] == g->cells[p]);
        score[g->cells[p] - 1] += p + (p >= 26 ? 5 : 0) + (protected ? 3 : 0);
      }
    return score[player - 1] - score[2 - player];
  }
  if (g->roll == -1) {
    double total = 0;
    for (int mask = 0; mask < 16; mask++) {
      int sum = 0;
      for (int bit = 0; bit < 4; bit++)
        if (mask & (1 << bit))
          sum++;
      Game next = *g;
      next.roll = sum ? sum : 5;
      total += reference(&next, player, depth) / 16.;
    }
    return total;
  }
  double best = g->turn == player ? -1e9 : 1e9;
  for (int source = -1; source <= 30; source++) {
    Game next = *g;
    if (!game_play(&next, (GameMove){source}))
      continue;
    double value = reference(&next, player, depth - 1);
    best = g->turn == player ? MAX(best, value) : MIN(best, value);
  }
  return best;
}
static void search(void) {
  Game g = position();
  g.finished[0] = 4;
  g.cells[28] = 1;
  g.cells[25] = 2;
  g.finished[1] = 4;
  game_set_roll(&g, 3);
  GRand *rng = g_rand_new_with_seed(3);
  g_assert_cmpint(search_choose(&g, 3, rng).cell, ==, 28);
  for (int player = 1; player <= 2; player++) {
    game_init(&g);
    g.cells[1] = 0;
    g.cells[26] = 1;
    g.cells[2] = 0;
    g.cells[29] = 2;
    g_assert_cmpfloat_with_epsilon(search_value(&g, player, 2),
                                   reference(&g, player, 2), 1e-8);
  }
  g_rand_free(rng);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/senet/rules", rules);
  g_test_add_func("/senet/sticks", sticks);
  g_test_add_func("/senet/conservation", conservation);
  g_test_add_func("/senet/search", search);
  return g_test_run();
}
