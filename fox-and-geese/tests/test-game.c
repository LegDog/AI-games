// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#define B(n) (G_GUINT64_CONSTANT(1) << (n))
static int point(const char *name) {
  for (int i = 0; i < GAME_POINTS; i++)
    if (g_str_equal(name, game_labels[i]))
      return i;
  g_assert_not_reached();
}
static GameMove move(const char *text) {
  GameMove m;
  g_assert_true(game_parse_move(text, &m));
  return m;
}
static void rules(void) {
  Game g;
  game_init(&g);
  g_assert_cmpuint(game_count(&g, 1), ==, 1);
  g_assert_cmpuint(game_count(&g, 2), ==, 13);
  g_assert_cmpuint(game_cell(&g, point("d4")), ==, 1);
  g_assert_true(game_move_legal(&g, move("d4-d5")));
  game_init_variant(&g, GAME_SEVENTEEN);
  g_assert_cmpuint(game_count(&g, 2), ==, 17);
  g.turn = 2;
  g_assert_false(game_move_legal(&g, move("a4-b5")));
  g = (Game){.pieces = {B(point("d4")), B(point("c4")) | B(point("g5")) |
                                            B(point("d1")) | B(point("e1")) |
                                            B(point("e2"))},
             .turn = 1};
  g_assert_false(game_move_legal(&g, move("d4-d5")));
  g_assert_true(game_move_legal(&g, move("d4xb4")));
  Game saved = g;
  g_assert_false(game_play(&g, move("d4xb4xd4")));
  g_assert_cmpmem(&g, sizeof g, &saved, sizeof saved);
  g_assert_true(game_play(&g, move("d4xb4")));
  g_assert_cmpuint(game_count(&g, 2), ==, 4);
  g_assert_cmpint(game_result(&g), ==, 0);
  /* Geese cannot capture even when a fox has an empty landing beyond it. */
  g.turn = 2;
  g_assert_false(game_move_legal(&g, move("d1xb3")));
  g = (Game){.pieces = {B(point("c1")), B(point("c2")) | B(point("d3")) |
                                            B(point("a3")) | B(point("g3"))},
             .turn = 1};
  g_assert_false(game_move_legal(&g, move("c1xc3")));
  g_assert_true(game_play(&g, move("c1xc3xe3")));
  g_assert_cmpint(game_result(&g), ==, 1);
  g_assert_cmpuint(game_count(&g, 2), ==, 2);
  g_assert_false(game_parse_move("a1-a2", &(GameMove){0}));
  g_assert_false(game_parse_move("c1xc3-e3", &(GameMove){0}));
  /* A goose closes the last exit of the corner fox. */
  g = (Game){.pieces = {B(point("c1")), B(point("d1")) | B(point("c2")) |
                                            B(point("e1")) | B(point("c3")) |
                                            B(point("e3")) | B(point("e2"))},
             .turn = 2};
  g_assert_true(game_play(&g, move("e2-d2")));
  g_assert_cmpint(game_result(&g), ==, 2);
}
static gboolean cancelled(gpointer data) {
  (void)data;
  return TRUE;
}
static void draws_and_cancellation(void) {
  Game g = {.pieces = {B(point("d5")), B(point("c1")) | B(point("d1")) |
                                           B(point("e1")) | B(point("c2"))},
            .turn = 1};
  Game start = g;
  const char *cycle[] = {"d5-d6", "e1-e2", "d6-d5", "e2-e1"};
  for (unsigned i = 0; i < 9; i++)
    g_assert_true(game_play(&g, move(cycle[i % 4])));
  g_assert_cmpint(g.result, ==, GAME_DRAW);
  g_assert_cmpint(g.end, ==, GAME_REPETITION);
  g = start;
  g.quiet = 49;
  g_assert_true(game_play(&g, move("d5-d6")));
  g_assert_cmpint(g.end, ==, GAME_QUIET);
  g_auto(GameMoves) legal = {0};
  game_init(&g);
  game_legal_moves(&g, &legal);
  g_assert_false(game_legal_moves_full(&g, &legal, cancelled, NULL));
  g_assert_cmpuint(legal.count, ==, 0);
  GRand *rng = g_rand_new_with_seed(1);
  SearchResult r = search_choose_full(&g, 6, 50000, TRUE, rng, cancelled, NULL);
  g_assert_cmpint(r.move.from, ==, -1);
  g_rand_free(rng);
}
static void search_and_replay(void) {
  for (int v = 0; v < 2; v++) {
    Game g;
    game_init_variant(&g, v);
    GRand *a = g_rand_new_with_seed(7), *b = g_rand_new_with_seed(7);
    SearchResult plain = search_choose(&g, 2, 50000, FALSE, a);
    SearchResult ab = search_choose(&g, 2, 50000, TRUE, b);
    g_assert_cmpuint(plain.depth, ==, 2);
    g_assert_cmpuint(ab.depth, ==, 2);
    char x[GAME_LABEL_SIZE], y[GAME_LABEL_SIZE];
    game_move_label(plain.move, x);
    game_move_label(ab.move, y);
    g_assert_cmpstr(x, ==, y);
    SearchResult limited = search_choose(&g, 6, 1, TRUE, a);
    g_assert_cmpuint(limited.nodes, <=, 1);
    g_assert_true(game_move_legal(&g, limited.move));
    for (unsigned turn = 0; turn < 2000 && !game_result(&g); turn++) {
      g_auto(GameMoves) legal = {0};
      game_legal_moves(&g, &legal);
      g_assert_cmpuint(legal.count, >, 0);
      GameMove m = legal.moves[g_rand_int_range(a, 0, legal.count)];
      game_move_label(m, x);
      GameMove parsed = move(x);
      unsigned before = game_count(&g, 2), actor = g.turn;
      g_assert_true(game_play(&g, parsed));
      g_assert_cmpuint(game_count(&g, 1), ==, 1);
      g_assert_cmpuint(game_count(&g, 2), ==,
                       before - (actor == 1 && m.capture ? m.length : 0));
      g_assert_cmpuint(g.pieces[0] & g.pieces[1], ==, 0);
    }
    g_assert_cmpint(game_result(&g), !=, 0);
    g_rand_free(a);
    g_rand_free(b);
  }
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/fox-geese/rules", rules);
  g_test_add_func("/fox-geese/search-replay", search_and_replay);
  g_test_add_func("/fox-geese/draws-cancel", draws_and_cancellation);
  return g_test_run();
}
