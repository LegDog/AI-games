// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <string.h>
#define B(n) (1u << (n))
static void play(Game *g, const char *s) {
  g_test_message("play %s", s);
  GameMove m;
  g_assert_true(game_parse_move(s, &m));
  g_assert_true(game_play(g, m));
}
static Game position(guint32 a, guint32 b) {
  return (Game){.pieces = {a, b}, .turn = 1};
}
static void topology(void) {
  unsigned edges = 0;
  for (int p = 0; p < 24; p++)
    for (int q = 0; q < 24; q++) {
      g_assert_cmpint(!!(game_neighbors[p] & B(q)), ==,
                      !!(game_neighbors[q] & B(p)));
      if (game_neighbors[p] & B(q))
        edges++;
    }
  g_assert_cmpuint(edges, ==, 64);
  for (int i = 0; i < 16; i++) {
    Game g = position(game_mills[i], 0);
    g_assert_cmpuint(game_count(&g, 1), ==, 3);
    g_assert_cmpuint(game_mill_pieces(g.pieces[0]), ==, game_mills[i]);
    for (int j = 0; j < i; j++)
      g_assert_cmpuint(game_mills[i], !=, game_mills[j]);
  }
  g_assert_false(game_neighbors[0] & B(8));
  g_assert_false(game_neighbors[17] & B(21));
}
static void rules(void) {
  Game g;
  game_init(&g);
  GameMoves moves;
  game_legal_moves(&g, &moves);
  g_assert_cmpuint(moves.count, ==, 24);
  play(&g, "a7");
  play(&g, "b6");
  play(&g, "d7");
  play(&g, "d6");
  Game saved = g;
  GameMove m = {-1, 2, -1};
  g_assert_false(game_play(&g, m));
  g_assert_cmpmem(&g, sizeof g, &saved, sizeof saved);
  play(&g, "g7xb6");
  g_assert_cmpuint(game_count(&g, 2), ==, 1);
  g_assert_cmpuint(g.reserve[1], ==, 7);
  g_assert_cmpint(g.result, ==, 0); /* reserves prevent a premature loss */
  g = position(B(0) | B(1) | B(3) | B(8), B(16) | B(17) | B(18) | B(22));
  g_assert_false(game_move_legal(&g, (GameMove){3, 2, 16}));
  g_assert_true(game_move_legal(&g, (GameMove){3, 2, 22}));
  play(&g, "g4-g7xc3");
  g_assert_cmpuint(game_count(&g, 2), ==, 3);
  g = position(B(0) | B(1) | B(3) | B(8), B(16) | B(17) | B(18));
  g_assert_true(game_move_legal(&g, (GameMove){3, 2, 16}));
  play(&g, "g4-g7xc5");
  g_assert_cmpint(g.result, ==, 1);
  g = position(B(0) | B(1) | B(8), B(4) | B(12) | B(20) | B(22));
  g_assert_true(game_move_legal(&g, (GameMove){8, 18, -1})); /* fly */
  g.pieces[0] |= B(6);
  g_assert_false(game_move_legal(&g, (GameMove){8, 18, -1}));
  g = position(B(0) | B(2) | B(9) | B(13), B(1) | B(3) | B(5) | B(7));
  g.turn = 2;
  play(&g, "a4-b4");
  g_assert_cmpint(g.result, ==, 0);
  /* All four corners occupied; all eight corner exits blocked. */
  g = position(B(0) | B(2) | B(4) | B(6), B(1) | B(3) | B(5) | B(15) | B(23));
  g.turn = 2;
  g.quiet = 49; /* A blocking win takes priority over the quiet draw. */
  play(&g, "b4-a4");
  g_assert_cmpint(g.result, ==, 2);
  g_assert_cmpint(g.end, ==, GAME_BLOCKED);
}
static void double_mill(void) {
  Game g = position(B(0) | B(2) | B(9) | B(17), B(4) | B(6) | B(8) | B(10));
  g.reserve[0] = 1;
  play(&g, "d7xg1");
  g_assert_cmpuint(game_count(&g, 2), ==, 3); /* exactly one capture */
  g = position(B(0) | B(1) | B(2) | B(8), B(4) | B(12) | B(20) | B(22));
  play(&g, "g7-g4");
  play(&g, "g1-d1");
  g_assert_true(game_move_legal(&g, (GameMove){3, 2, 5})); /* re-formed mill */
}
static void draws(void) {
  Game g = position(B(0) | B(2) | B(8) | B(10), B(4) | B(6) | B(12) | B(14));
  /* Seed history with the starting position via a harmless reversible cycle. */
  const char *cycle[] = {"a7-a4", "g1-g4", "a4-a7", "g4-g1"};
  for (int n = 0; n < 3 && !g.result; n++)
    for (int i = 0; i < 4 && !g.result; i++)
      play(&g, cycle[i]);
  g_assert_cmpint(g.result, ==, GAME_DRAW);
  g_assert_cmpint(g.end, ==, GAME_REPETITION);
  g = position(B(0) | B(2) | B(8) | B(10), B(4) | B(6) | B(12) | B(14));
  g.quiet = 49;
  play(&g, "a7-a4");
  g_assert_cmpint(g.end, ==, GAME_QUIET);
  g = position(B(0) | B(1) | B(3) | B(8), B(16) | B(17) | B(18) | B(22));
  g.quiet = 49;
  play(&g, "g4-g7xc3");
  g_assert_cmpuint(g.quiet, ==, 0);
  g_assert_cmpuint(g.history_count, ==, 1);
  g_assert_cmpint(g.result, ==, 0);
}
static void notation(void) {
  const char *bad[] = {"",      "a",        "a7-",   "z9",
                       "z9-a7", "a7x",      "a7xz9", "a7-a7garbage",
                       "pass",  "a7xg7xg1", NULL};
  GameMove m;
  for (int i = 0; bad[i]; i++)
    g_assert_false(game_parse_move(bad[i], &m));
  Game g;
  game_init(&g);
  GRand *rng = g_rand_new_with_seed(2026);
  for (int n = 0; n < 120 && !g.result; n++) {
    GameMoves legal;
    game_legal_moves(&g, &legal);
    for (unsigned i = 0; i < legal.count; i++) {
      char s[16];
      game_move_label(legal.moves[i], s);
      g_assert_true(game_parse_move(s, &m));
      g_assert_cmpint(m.from, ==, legal.moves[i].from);
      g_assert_cmpint(m.to, ==, legal.moves[i].to);
      g_assert_cmpint(m.capture, ==, legal.moves[i].capture);
    }
    g_assert_true(
        game_play(&g, legal.moves[g_rand_int_range(rng, 0, legal.count)]));
    g_assert_cmpuint(g.pieces[0] & g.pieces[1], ==, 0);
    for (int side = 1; side <= 2; side++)
      g_assert_cmpuint(game_count(&g, side) + g.reserve[side - 1], <=, 9);
  }
  g_rand_free(rng);
}
static gboolean cancel(gpointer data) {
  (void)data;
  return TRUE;
}
static void search(void) {
  Game g;
  game_init(&g);
  GRand *a = g_rand_new_with_seed(42), *b = g_rand_new_with_seed(42);
  for (int n = 0; n < 12; n++) {
    SearchResult plain = search_choose(&g, 2, 1000000, FALSE, a);
    SearchResult pruned = search_choose(&g, 2, 1000000, TRUE, b);
    g_assert_cmpuint(plain.depth, ==, 2);
    g_assert_cmpuint(pruned.depth, ==, 2);
    g_assert_cmpint(plain.move.from, ==, pruned.move.from);
    g_assert_cmpint(plain.move.to, ==, pruned.move.to);
    g_assert_cmpint(plain.move.capture, ==, pruned.move.capture);
    g_assert_true(game_play(&g, plain.move));
  }
  SearchResult r = search_choose(&g, 6, 1, TRUE, a);
  g_assert_cmpuint(r.depth, ==, 0);
  g_assert_cmpuint(r.nodes, ==, 1);
  g_assert_true(game_move_legal(&g, r.move));
  r = search_choose_full(&g, 6, 50000, TRUE, a, cancel, NULL);
  g_assert_cmpuint(r.nodes, ==, 0);
  g_assert_cmpuint(r.depth, ==, 0);
  g_assert_true(game_move_legal(&g, r.move));
  g = position(B(0) | B(1) | B(3) | B(8), B(16) | B(17) | B(18));
  r = search_choose(&g, 2, 100000, TRUE, a);
  g_assert_true(game_play(&g, r.move));
  g_assert_cmpint(g.result, ==, 1);
  g_rand_free(a);
  g_rand_free(b);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/morris/topology", topology);
  g_test_add_func("/morris/rules", rules);
  g_test_add_func("/morris/double-reformed-mill", double_mill);
  g_test_add_func("/morris/draws", draws);
  g_test_add_func("/morris/notation-replay", notation);
  g_test_add_func("/morris/search", search);
  return g_test_run();
}
