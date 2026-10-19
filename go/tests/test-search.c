// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
static void mcts(void) {
  Game g;
  game_init(&g, 7.5);
  SearchOptions o = {8, 0, 1024 * 1024, 0};
  GRand *a = g_rand_new_with_seed(5), *b = g_rand_new_with_seed(5);
  SearchResult first, second;
  g_assert_true(mcts_evaluate(&g, &o, NULL, a, &first));
  g_assert_true(mcts_evaluate(&g, &o, NULL, b, &second));
  g_assert_cmpuint(first.completed, ==, 8);
  g_assert_cmpmem(first.visits, sizeof first.visits, second.visits,
                  sizeof second.visits);
  g_assert_cmpmem(first.means, sizeof first.means, second.means,
                  sizeof second.means);
  g_assert_cmpuint(first.peak_memory, <=, o.memory_bytes);
  g_assert_cmpuint(g.history->len, ==, 1);
  g_assert_true(game_valid(&g));
  int move = search_choose(&first, a);
  g_assert_cmpint(game_try(&g, move, NULL), ==, MOVE_OK);
  g_rand_free(a);
  g_rand_free(b);
  game_clear(&g);
}
static gboolean cancel(gpointer data) {
  (void)data;
  return TRUE;
}
static void limits(void) {
  Game g;
  game_init(&g, 0);
  GRand *r = g_rand_new_with_seed(0);
  SearchOptions o = search_defaults();
  SearchResult result;
  o.memory_bytes = 1;
  g_assert_false(mcts_evaluate(&g, &o, NULL, r, &result));
  g_assert_cmpint(result.status, ==, SEARCH_LIMIT);
  o = search_defaults();
  o.seconds = 1e-12;
  g_assert_false(mcts_evaluate(&g, &o, NULL, r, &result));
  g_assert_cmpint(result.status, ==, SEARCH_LIMIT);
  SearchControl c = {.cancelled = cancel};
  o = search_defaults();
  g_assert_false(mcts_evaluate(&g, &o, &c, r, &result));
  g_assert_cmpint(result.status, ==, SEARCH_CANCELLED);
  o = (SearchOptions){1000, 0, 1024 * 1024, 400};
  g_assert_true(mcts_evaluate(&g, &o, NULL, r, &result));
  g_assert_cmpint(result.status, ==, SEARCH_LIMIT);
  g_assert_cmpuint(result.completed, >, 0);
  g_assert_cmpuint(result.completed, <, 1000);
  g_assert_cmpint(game_try(&g, search_choose(&result, r), NULL), ==, MOVE_OK);
  g_rand_free(r);
  game_clear(&g);
}
static void heuristic(void) {
  Game g;
  game_init(&g, 7.5);
  GRand *r = g_rand_new_with_seed(1);
  int values[GO_CHOICES];
  int move = game_heuristic(&g, r, values);
  g_assert_cmpint(game_try(&g, move, NULL), ==, MOVE_OK);
  g_assert_cmpint(values[GO_PASS], !=, G_MININT);
  g_rand_free(r);
  game_clear(&g);
}
static void cutoff(void) {
  Game g;
  game_init(&g, 7.5);
  GRand *random = g_rand_new_with_seed(21);
  SearchOptions options = {1, 0, 1024 * 1024, 0};
  SearchResult result;
  g_assert_true(mcts_evaluate(&g, &options, NULL, random, &result));
  g_assert_cmpint(result.status, ==, SEARCH_COMPLETE);
  g_assert_cmpuint(result.completed, ==, 1);
  g_assert_cmpuint(result.cutoffs, ==, 1);
  g_assert_cmpuint(result.nodes, ==, 324);
  g_assert_false(game_finished(&g));
  g_assert_cmpuint(g.history->len, ==, 1);
  int move = search_choose(&result, random);
  g_assert_cmpint(game_try(&g, move, NULL), ==, MOVE_OK);
  g_assert_cmpfloat(result.means[move], >=, -1);
  g_assert_cmpfloat(result.means[move], <=, 1);
  g_rand_free(random);
  game_clear(&g);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/search/mcts-repeatability", mcts);
  g_test_add_func("/search/limits", limits);
  g_test_add_func("/search/heuristic", heuristic);
  g_test_add_func("/search/rollout-cutoff", cutoff);
  return g_test_run();
}
