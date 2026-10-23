// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"
#include "search.h"
#include <math.h>
static void topology(void) {
  /* Independent geometrical edges in stable A..J notation. */
  const char *edges[] = {"A-B", "B-C", "A-D", "C-E", "E-F", "F-G", "D-H", "H-I",
                         "I-J", "J-G", "D-B", "B-E", "B-J", "H-J", "J-F"};
  guint16 expected[10] = {0};
  for (unsigned i = 0; i < G_N_ELEMENTS(edges); i++) {
    int a = edges[i][0] - 'A', b = edges[i][2] - 'A';
    expected[a] |= 1u << b;
    expected[b] |= 1u << a;
  }
  for (int p = 0; p < 10; p++)
    g_assert_cmpuint(game_neighbors[p], ==, expected[p]);
  /* Exhaust every six-piece placement and both active roles against the edge
   * list. */
  for (unsigned dogs = 0; dogs < 1024; dogs++) {
    unsigned count = 0;
    for (int i = 0; i < 10; i++)
      count += (dogs >> i) & 1;
    if (count != 4)
      continue;
    for (int a = 0; a < 10; a++)
      for (int b = a + 1; b < 10; b++) {
        if (dogs & ((1u << a) | (1u << b)))
          continue;
        Game g = {.limit = 100};
        for (int p = 0; p < 10; p++)
          g.cells[p] = (dogs & (1u << p)) ? 1 : p == a || p == b ? 2 : 0;
        for (int role = 1; role <= 2; role++) {
          g.turn = role;
          GameMoves legal;
          game_legal_moves(&g, &legal);
          unsigned expected_count = 0;
          for (int from = 0; from < 10; from++)
            for (int to = 0; to < 10; to++) {
              gboolean allowed = g.cells[from] == role && !g.cells[to] &&
                                 (expected[from] & (1u << to));
              g_assert_cmpint(game_move_legal(&g, (GameMove){from, to}), ==,
                              allowed);
              expected_count += allowed;
            }
          g_assert_cmpuint(game_mobility(&g, role), ==, expected_count);
          g_assert_cmpuint(legal.count, ==,
                           expected_count ? expected_count
                           : role == 1    ? 1
                                          : 0);
        }
      }
  }
}
static void rules(void) {
  Game g;
  game_init(&g);
  g_assert_cmpuint(game_actor(&g), ==, 1);
  Game before = g;
  g_assert_false(game_play(&g, (GameMove){0, 2}));
  g_assert_false(game_play(&g, (GameMove){-1, -1}));
  g_assert_false(game_play(&g, (GameMove){4, 2}));
  g_assert_false(game_play(&g, (GameMove){0, 3}));
  g_assert_false(game_next_round(&g));
  g_assert_cmpmem(&g, sizeof g, &before, sizeof before);
  g_assert_true(game_play(&g, (GameMove){0, 1}));
  g_assert_cmpint(g.turn, ==, 2);
  g_assert_true(game_play(&g, (GameMove){4, 2}));
  g_assert_cmpuint(g.hare_moves, ==, 1);
  /* Dogs cannot move when all exits from the left wall are occupied. */
  g = (Game){.cells = {1, 2, 0, 1, 0, 0, 0, 1, 1, 2}, .turn = 1, .limit = 100};
  g_assert_true(game_move_legal(&g, (GameMove){-1, -1}));
  game_play(&g, (GameMove){-1, -1});
  g_assert_cmpint(g.turn, ==, 2);
  g_assert_cmpuint(g.hare_moves, ==, 0);
  /* Two corner hares are blocked only after the final exit is covered. */
  g = (Game){.cells = {2, 1, 0, 0, 0, 1, 2, 1, 0, 1}, .turn = 1, .limit = 100};
  g_assert_false(game_hares_blocked(&g));
  g_assert_true(game_play(&g, (GameMove){7, 3}));
  g_assert_true(g.round_over);
  g_assert_cmpint(game_result(&g), ==, 0);
  g_assert_true(game_next_round(&g));
  g_assert_cmpuint(game_actor(&g), ==, 2);
  g_assert_cmpuint(game_hare_player(&g), ==, 1);
  g_assert_false(game_next_round(&g));
  GameMove move;
  const char *bad[] = {NULL, "a-b", "A-K", "A", "A-B ", "pass ", "A--B"};
  for (unsigned i = 0; i < G_N_ELEMENTS(bad); i++)
    g_assert_false(game_parse_move(bad[i], &move));
  g_assert_true(game_parse_move("A-B", &move));
  g_assert_cmpint(move.to, ==, 1);
  g_assert_true(game_parse_move("pass", &move));
  g_assert_cmpint(move.from, ==, -1);
  g.round_over = TRUE;
  g.survival[0] = 4;
  g.survival[1] = 3;
  g_assert_cmpint(game_result(&g), ==, 1);
  g.survival[1] = 5;
  g_assert_cmpint(game_result(&g), ==, 2);
  g.survival[0] = 5;
  g_assert_cmpint(game_result(&g), ==, GAME_DRAW);
}
static void matches(void) {
  GRand *rng = g_rand_new_with_seed(41);
  for (int n = 0; n < 100; n++) {
    Game g;
    game_init_limit(&g, 20);
    while (!game_result(&g)) {
      if (g.round_over) {
        g_assert_true(game_next_round(&g));
        continue;
      }
      GameMoves legal;
      game_legal_moves(&g, &legal);
      g_assert_cmpuint(legal.count, >, 0);
      game_play(&g, legal.moves[g_rand_int_range(rng, 0, legal.count)]);
      int counts[3] = {0};
      for (int p = 0; p < 10; p++)
        counts[g.cells[p]]++;
      g_assert_cmpint(counts[1], ==, 4);
      g_assert_cmpint(counts[2], ==, 2);
      g_assert_cmpuint(g.actions, <=, 82);
      g_assert_cmpuint(g.hare_moves, <=, 20);
    }
    g_assert_cmpuint(g.survival[0], <=, 20);
    g_assert_cmpuint(g.survival[1], <=, 20);
  }
  g_rand_free(rng);
}
static void search(void) {
  Game g;
  game_init(&g);
  for (unsigned depth = 1; depth <= 4; depth++) {
    GRand *a = g_rand_new_with_seed(7), *b = g_rand_new_with_seed(7);
    SearchResult plain = search_choose(&g, depth, 1000000, FALSE, a),
                 pruned = search_choose(&g, depth, 1000000, TRUE, b);
    g_assert_cmpuint(plain.depth, ==, depth);
    g_assert_cmpuint(pruned.depth, ==, depth);
    g_assert_cmpint(plain.move.from, ==, pruned.move.from);
    g_assert_cmpint(plain.move.to, ==, pruned.move.to);
    g_assert_cmpuint(pruned.nodes, <=, plain.nodes);
    g_rand_free(a);
    g_rand_free(b);
  }
  GRand *rng = g_rand_new_with_seed(11);
  SearchResult limited = search_choose(&g, 6, 1, TRUE, rng);
  g_assert_cmpuint(limited.nodes, <=, 1);
  g_assert_true(game_move_legal(&g, limited.move));
  g_assert_cmpuint(limited.depth, ==, 0);
  g = (Game){.cells = {2, 1, 0, 0, 0, 1, 2, 1, 0, 1}, .turn = 1, .limit = 100};
  SearchResult winning = search_choose(&g, 3, 10000, TRUE, rng);
  g_assert_cmpint(winning.move.from, ==, 7);
  g_assert_cmpint(winning.move.to, ==, 3);
  g_rand_free(rng);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/coriovalli/topology-exhaustive-legality", topology);
  g_test_add_func("/coriovalli/rules", rules);
  g_test_add_func("/coriovalli/paired-matches", matches);
  g_test_add_func("/coriovalli/search", search);
  return g_test_run();
}
