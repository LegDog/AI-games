// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <string.h>
#define B(n) (1u << (n))
static GameMove parse(const char *text) {
  GameMove m;
  g_assert_true(game_parse_move(text, &m));
  return m;
}
static void play(Game *g, const char *text) {
  g_assert_true(game_play(g, parse(text)));
}
static void rules(void) {
  Game g;
  game_init(&g);
  g_assert_cmpuint(game_count(&g, 1), ==, 12);
  g_assert_cmpuint(game_count(&g, 2), ==, 12);
  g_assert_cmpuint(game_cell(&g, 12), ==, 0);
  unsigned edges = 0;
  for (int p = 0; p < 25; p++)
    for (int q = 0; q < 25; q++) {
      int dx = ABS(p % 5 - q % 5), dy = ABS(p / 5 - q / 5);
      gboolean expected =
          MAX(dx, dy) == 1 && (!dx || !dy || (p % 5 + p / 5) % 2 == 0);
      g_assert_cmpint(!!(game_neighbors[p] & B(q)), ==, expected);
      edges += !!(game_neighbors[p] & B(q));
    }
  g_assert_cmpuint(edges, ==, 112);
  play(&g, "c2-c3");
  g_assert_true(game_move_legal(&g, parse("c4xc2")));
  g_assert_false(game_move_legal(&g, parse("a4-c3")));
  play(&g, "c4xc2");
  g_assert_cmpuint(game_count(&g, 1), ==, 11);
  g_assert_cmpuint(g.quiet, ==, 0);
  g_assert_cmpuint(g.history_count, ==, 1);
  g = (Game){.pieces = {B(0), B(6) | B(18) | B(4)}, .turn = 1};
  Game saved = g;
  g_assert_true(game_prefix_valid(&g, parse("a1xc3")));
  g_assert_false(game_play(&g, parse("a1xc3")));
  g_assert_cmpmem(&g, sizeof g, &saved, sizeof saved);
  play(&g, "a1xc3xe5");
  g_assert_cmpuint(game_cell(&g, 24), ==, 1);
  g_assert_cmpuint(game_count(&g, 2), ==, 1);
  g_assert_cmpuint(g.actions, ==, 1);
  g = (Game){.pieces = {B(0), B(6) | B(16) | B(18) | B(4)}, .turn = 1};
  g_assert_true(game_move_legal(&g, parse("a1xc3xa5")));
  g_assert_true(game_move_legal(&g, parse("a1xc3xe5")));
  /* Shorter complete branches remain legal even when a longer chain exists. */
  g = (Game){.pieces = {B(0), B(1) | B(3) | B(4) | B(6)}, .turn = 1};
  g_assert_true(game_move_legal(&g, parse("a1xc3")));
  g_assert_true(game_move_legal(&g, parse("a1xc1xa3")));
  /* Removed victims cannot be captured again on the reverse hop. */
  g = (Game){.pieces = {B(0), B(6)}, .turn = 1};
  play(&g, "a1xc3");
  g_assert_cmpint(g.result, ==, 1);
  g_assert_cmpint(g.end, ==, GAME_MATERIAL);
  for (int side = 1; side <= 2; side++) {
    g = (Game){.pieces = {B(12), B(side == 1 ? 24 : 0)},
               .turn = side,
               .variant = GAME_FREE};
    const char *back = side == 1 ? "c3-c2" : "c3-c4";
    if (side == 2) {
      g.pieces[0] = B(0);
      g.pieces[1] = B(12);
    }
    g_assert_true(game_move_legal(&g, parse(back)));
    g.variant = GAME_DIRECTIONAL;
    g_assert_false(game_move_legal(&g, parse(back)));
  }
  g = (Game){.pieces = {B(22), B(5)}, .turn = 1, .variant = GAME_DIRECTIONAL};
  g_assert_cmpuint(game_mobility(&g, 1), ==, 0);
  g.pieces[1] |= B(17);
  g_assert_true(game_move_legal(&g, parse("c5xc3")));
  play(&g, "c5xc3");
  g.turn = 1;
  g_assert_true(game_move_legal(&g, parse("c3-c4")));
  g = (Game){.pieces = {B(0), B(20)}, .turn = 1, .quiet = 49};
  play(&g, "a1-b1");
  g_assert_cmpint(g.result, ==, GAME_DRAW);
  g_assert_cmpint(g.end, ==, GAME_QUIET);
  /* Blocking wins override the quiet limit. */
  g = (Game){.pieces = {B(0), B(24)},
             .turn = 1,
             .quiet = 49,
             .variant = GAME_DIRECTIONAL};
  /* Dark at row 5 can still move: use dark's far rank instead. */
  g.pieces[1] = B(4);
  play(&g, "a1-a2");
  g_assert_cmpint(g.result, ==, 1);
  g_assert_cmpint(g.end, ==, GAME_BLOCKED);
  g = (Game){.pieces = {B(0), B(24)}, .turn = 1};
  const char *cycle[] = {"a1-b1", "e5-d5", "b1-a1", "d5-e5"};
  for (unsigned i = 0; i < 12 && !g.result; i++)
    play(&g, cycle[i % 4]);
  g_assert_cmpint(g.end, ==, GAME_REPETITION);
  g_assert_cmpint(g.result, ==, GAME_DRAW);
  const char *bad[] = {"",         "a1",       "a1-",    "z1-a2",
                       "a1-a2-a3", "a1xc3-e5", "a1xc3x", "a1xc3junk"};
  for (unsigned i = 0; i < G_N_ELEMENTS(bad); i++) {
    GameMove m;
    g_assert_false(game_parse_move(bad[i], &m));
  }
}
/* Independent coordinate rules: no use of engine adjacency, capture, or apply.
 */
static gboolean edge(int a, int b) {
  int dx = ABS(a % 5 - b % 5), dy = ABS(a / 5 - b / 5);
  return MAX(dx, dy) == 1 && (!dx || !dy || (a % 5 + a / 5) % 2 == 0);
}
static unsigned owner(const int cells[25], int p) { return cells[p]; }
static void reference_chains(int cells[25], int side, int from, GameMove move,
                             GHashTable *out) {
  gboolean more = FALSE;
  for (int to = 0; to < 25; to++) {
    int dx = to % 5 - from % 5, dy = to / 5 - from / 5;
    if (MAX(ABS(dx), ABS(dy)) != 2 || (dx && ABS(dx) != 2) ||
        (dy && ABS(dy) != 2))
      continue;
    int mid = (from / 5 + dy / 2) * 5 + from % 5 + dx / 2;
    if (!edge(from, mid) || !edge(mid, to) ||
        owner(cells, mid) != (unsigned)(3 - side) || cells[to])
      continue;
    more = TRUE;
    GameMove next = move;
    next.land[next.length++] = to;
    cells[from] = 0;
    cells[mid] = 0;
    cells[to] = side;
    reference_chains(cells, side, to, next, out);
    cells[from] = side;
    cells[mid] = 3 - side;
    cells[to] = 0;
  }
  if (!more && move.length) {
    char label[GAME_LABEL_SIZE];
    game_move_label(move, label);
    g_hash_table_add(out, g_strdup(label));
  }
}
static void independent(void) {
  GRand *rng = g_rand_new_with_seed(2026);
  for (unsigned n = 0; n < 500; n++) {
    Game g = {.turn = 1 + (n % 2), .variant = (n / 2) % 2};
    int cells[25] = {0};
    for (int p = 0; p < 25; p++) {
      cells[p] = g_rand_int_range(rng, 0, 3);
      if (cells[p])
        g.pieces[cells[p] - 1] |= B(p);
    }
    /* Legal reachable material limits bound the capture path representation. */
    if (game_count(&g, 1) > 12 || game_count(&g, 2) > 12) {
      n--;
      continue;
    }
    g_autoptr(GHashTable) expected =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (int p = 0; p < 25; p++)
      if (cells[p] == g.turn)
        reference_chains(cells, g.turn, p,
                         (GameMove){.from = p, .capture = TRUE}, expected);
    if (!g_hash_table_size(expected))
      for (int p = 0; p < 25; p++)
        if (cells[p] == g.turn)
          for (int q = 0; q < 25; q++)
            if (!cells[q] && edge(p, q)) {
              if (g.variant == GAME_DIRECTIONAL &&
                  ((g.turn == 1 && (p / 5 == 4 || q / 5 < p / 5)) ||
                   (g.turn == 2 && (p / 5 == 0 || q / 5 > p / 5))))
                continue;
              GameMove m = {.from = p, .length = 1, .land = {q}};
              char label[GAME_LABEL_SIZE];
              game_move_label(m, label);
              g_hash_table_add(expected, g_strdup(label));
            }
    g_auto(GameMoves) legal = {0};
    game_legal_moves(&g, &legal);
    g_assert_cmpuint(legal.count, ==, g_hash_table_size(expected));
    for (unsigned i = 0; i < legal.count; i++) {
      char label[GAME_LABEL_SIZE], round[GAME_LABEL_SIZE];
      game_move_label(legal.moves[i], label);
      g_assert_true(g_hash_table_contains(expected, label));
      GameMove m = parse(label);
      game_move_label(m, round);
      g_assert_cmpstr(label, ==, round);
      Game next = g;
      g_assert_true(game_play(&next, m));
      g_assert_cmpuint(game_count(&next, g.turn), ==, game_count(&g, g.turn));
      g_assert_cmpuint(game_count(&next, 3 - g.turn), ==,
                       game_count(&g, 3 - g.turn) - (m.capture ? m.length : 0));
    }
  }
  g_rand_free(rng);
}
static gboolean cancel(gpointer data) {
  (void)data;
  return TRUE;
}
static double exhaustive(const Game *g, int side, unsigned depth) {
  if (!depth || game_result(g))
    return search_value(g, side, 0);
  g_auto(GameMoves) moves = {0};
  game_legal_moves(g, &moves);
  double best = g->turn == side ? -1e9 : 1e9;
  for (unsigned i = 0; i < moves.count; i++) {
    Game next = *g;
    g_assert_true(game_play(&next, moves.moves[i]));
    double v = exhaustive(&next, side, depth - 1);
    best = g->turn == side ? MAX(best, v) : MIN(best, v);
  }
  return best;
}
static void search(void) {
  for (int v = 0; v < 2; v++) {
    Game g = {.pieces = {B(0) | B(12), B(18) | B(24)}, .turn = 1, .variant = v};
    g_assert_cmpfloat(search_value(&g, 1, 3), ==, exhaustive(&g, 1, 3));
    GRand *a = g_rand_new_with_seed(42), *b = g_rand_new_with_seed(42);
    SearchResult plain = search_choose(&g, 3, 100000, FALSE, a),
                 prune = search_choose(&g, 3, 100000, TRUE, b);
    char x[GAME_LABEL_SIZE], y[GAME_LABEL_SIZE];
    game_move_label(plain.move, x);
    game_move_label(prune.move, y);
    g_assert_cmpstr(x, ==, y);
    g_assert_cmpuint(plain.depth, ==, 3);
    g_assert_cmpuint(prune.depth, ==, 3);
    SearchResult limit = search_choose(&g, 6, 1, TRUE, a);
    g_assert_cmpuint(limit.nodes, <=, 1);
    g_assert_cmpuint(limit.depth, ==, 0);
    g_assert_true(game_move_legal(&g, limit.move));
    g_auto(GameMoves) moves = {0};
    game_legal_moves(&g, &moves);
    unsigned count = moves.count;
    limit = search_choose(&g, 6, count, TRUE, a);
    g_assert_cmpuint(limit.depth, ==, 1);
    g_assert_cmpuint(limit.nodes, ==, count);
    limit = search_choose_full(&g, 6, 1000, TRUE, a, cancel, NULL);
    g_assert_cmpuint(limit.nodes, ==, 0);
    g_assert_false(game_legal_moves_full(&g, &moves, cancel, NULL));
    g_assert_cmpuint(moves.count, ==, 0);
    g_rand_free(a);
    g_rand_free(b);
  }
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/alquerque/rules", rules);
  g_test_add_func("/alquerque/independent", independent);
  g_test_add_func("/alquerque/search", search);
  return g_test_run();
}
