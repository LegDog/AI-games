// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <string.h>
#define B(p) (1u << (p))
static int at(const char *s) {
  for (int i = 0; i < 32; i++)
    if (!strcmp(game_labels[i], s))
      return i;
  g_assert_not_reached();
}
static Game position(GameVariant v, const char *light, const char *dark,
                     const char *kings, int turn) {
  Game g = {.variant = v, .turn = turn};
  const char *lists[] = {light, dark, kings};
  for (int i = 0; i < 3; i++) {
    g_auto(GStrv) words = g_strsplit(lists[i], " ", -1);
    for (char **p = words; *p; p++)
      if (**p) {
        int n = at(*p);
        if (i == 2)
          g.kings |= B(n);
        else
          g.pieces[i] |= B(n);
      }
  }
  return g;
}
static GameMove move(const char *s) {
  GameMove m;
  g_assert_true(game_parse_move(s, &m));
  return m;
}
static void play(Game *g, const char *s) {
  g_assert_true(game_play(g, move(s)));
}
static void initial(void) {
  for (int v = 0; v < 2; v++) {
    Game g;
    game_init_variant(&g, v);
    g_assert_cmpuint(game_count(&g, 1), ==, 12);
    g_assert_cmpuint(game_count(&g, 2), ==, 12);
    g_assert_cmpint(g.turn, ==, v ? 1 : 2);
    g_auto(GameMoves) legal = {0};
    game_legal_moves(&g, &legal);
    g_assert_cmpuint(legal.count, ==, 7);
    Game saved = g;
    g_assert_false(game_play(&g, move("a1xc3")));
    g_assert_cmpmem(&saved, sizeof saved, &g, sizeof g);
    for (unsigned i = 0; i < legal.count; i++) {
      char label[GAME_LABEL_SIZE];
      game_move_label(legal.moves[i], label);
      g_assert_true(game_move_legal(&g, move(label)));
    }
  }
  const char *bad[] = {"",         "a0-b1",  "b1-c2",    "a1-b2x",
                       "a1-b2-c3", "a1xc3-", "a1-a1junk"};
  for (unsigned i = 0; i < G_N_ELEMENTS(bad); i++) {
    GameMove m;
    g_assert_false(game_parse_move(bad[i], &m));
  }
}
static void rules(void) {
  Game g = position(GAME_ENGLISH, "c3", "b2 h8", "", 1);
  g_assert_false(game_move_legal(&g, move("c3xa1")));
  g.variant = GAME_BRAZILIAN;
  g_assert_true(game_move_legal(&g, move("c3xa1")));
  g_assert_false(game_move_legal(&g, move("c3-d4")));
  g = position(GAME_ENGLISH, "a1", "b2 d4 h8", "", 1);
  g_assert_true(game_prefix_valid(&g, move("a1xc3")));
  g_assert_false(game_move_legal(&g, move("a1xc3")));
  play(&g, "a1xc3xe5");
  g_assert_cmpuint(game_count(&g, 2), ==, 1);
  g = position(GAME_ENGLISH, "a1 e1", "b2 d4 f2 h8", "", 1);
  g_assert_true(game_move_legal(&g, move("e1xg3")));
  g.variant = GAME_BRAZILIAN;
  g_assert_false(game_prefix_valid(&g, move("e1xg3")));
  g_assert_true(game_move_legal(&g, move("a1xc3xe5")));
  g = position(GAME_ENGLISH, "b6", "c7 e7 h8", "", 1);
  play(&g, "b6xd8");
  g_assert_true(game_is_king(&g, at("d8")));
  g = position(GAME_BRAZILIAN, "b6", "c7 e7 h8", "", 1);
  g_assert_false(game_move_legal(&g, move("b6xd8")));
  play(&g, "b6xd8xf6");
  g_assert_false(game_is_king(&g, at("f6")));
  g = position(GAME_BRAZILIAN, "b6", "c7 h8", "", 1);
  play(&g, "b6xd8");
  g_assert_true(game_is_king(&g, at("d8")));
  g = position(GAME_BRAZILIAN, "c3", "e5 h8", "c3", 1);
  g_assert_true(game_move_legal(&g, move("c3xf6")));
  g_assert_true(game_move_legal(&g, move("c3xg7")));
  g.variant = GAME_ENGLISH;
  g_assert_false(game_move_legal(&g, move("c3xf6")));
  g = position(GAME_BRAZILIAN, "c3", "b2 d4 h8", "c3", 1);
  g_auto(GameMoves) legal = {0};
  game_legal_moves(&g, &legal);
  for (unsigned i = 0; i < legal.count; i++)
    g_assert_cmpuint(legal.moves[i].length, ==, 1);
  game_preview_hop(&g, at("c3"), at("e5"), TRUE);
  g_assert_cmpuint(game_cell(&g, at("d4")), ==, 2);
  g_assert_true(!!(g.captured & B(at("d4"))));
  /* A flying king can return to its vacated source without recapturing victims.
   */
  g = position(GAME_BRAZILIAN, "d2", "f8 e3 b8 c5 e7 b4 b6", "d2", 1);
  play(&g, "d2xa5xd8xg5xd2");
  g_assert_cmpuint(game_count(&g, 2), ==, 3);
  g_assert_true(game_is_king(&g, at("d2")));
}
static void endings(void) {
  Game g = position(GAME_ENGLISH, "a1", "b2", "", 1);
  play(&g, "a1xc3");
  g_assert_cmpint(g.end, ==, GAME_MATERIAL);
  g = position(GAME_ENGLISH, "c3", "a1", "", 1);
  play(&g, "c3-d4");
  g_assert_cmpint(g.end, ==, GAME_BLOCKED);
  for (int v = 0; v < 2; v++) {
    g = position(v, "a1", "h6", "a1 h6", 1);
    const char *cycle[] = {"a1-b2", "h6-g5", "b2-a1", "g5-h6"};
    for (unsigned i = 0; i < 9; i++)
      play(&g, cycle[i % 4]);
    g_assert_cmpint(g.end, ==, GAME_REPETITION);
    g = position(v, "a1", "h6", "a1 h6", 1);
    g.quiet = game_quiet_limit(&g) - 1;
    play(&g, "a1-b2");
    g_assert_cmpint(g.end, ==, GAME_QUIET);
    g = position(v, "a1", "h6", "h6", 1);
    g.quiet = game_quiet_limit(&g) - 1;
    play(&g, "a1-b2");
    g_assert_cmpuint(g.quiet, ==, 0);
    g_assert_cmpuint(g.history_count, ==, 1);
    g = position(v, "a1", "b2", "", 1);
    g.quiet = game_quiet_limit(&g) - 1;
    play(&g, "a1xc3");
    g_assert_cmpint(g.end, ==, GAME_MATERIAL);
  }
}
/* Coordinate-array reference: never uses engine hop or preview helpers. */
typedef struct {
  int cells[8][8], side;
  gboolean brazil;
} Ref;
static gboolean inside(int x, int y) {
  return x >= 0 && x < 8 && y >= 0 && y < 8;
}
static int index_of(int x, int y) { return y * 4 + x / 2; }
static void reference_chains(Ref r, int x, int y, GameMove path, guint32 taken,
                             GArray *out) {
  gboolean king = ABS(r.cells[y][x]) == 2, found = FALSE;
  for (int dy = -1; dy <= 1; dy += 2)
    for (int dx = -1; dx <= 1; dx += 2) {
      if (!king && !r.brazil && dy != (r.side == 1 ? 1 : -1))
        continue;
      int victim = -1;
      for (int d = 1; inside(x + dx * d, y + dy * d); d++) {
        int nx = x + dx * d, ny = y + dy * d, value = r.cells[ny][nx];
        if (value) {
          if (victim >= 0 || (value > 0) == (r.side == 1) ||
              (taken & B(index_of(nx, ny))))
            break;
          victim = index_of(nx, ny);
        } else if (victim >= 0) {
          Ref next = r;
          GameMove m = path;
          m.land[m.length++] = index_of(nx, ny);
          m.capture = TRUE;
          next.cells[ny][nx] = next.cells[y][x];
          next.cells[y][x] = 0;
          if (!r.brazil) {
            int vx = (victim % 4) * 2 + (victim / 4) % 2;
            next.cells[victim / 4][vx] = 0;
          }
          found = TRUE;
          if (!r.brazil && !king && ny == (r.side == 1 ? 7 : 0))
            g_array_append_val(out, m);
          else
            reference_chains(next, nx, ny, m, taken | B(victim), out);
          if (!king || !r.brazil)
            break;
        } else if (!king || !r.brazil)
          break;
        if ((!king || !r.brazil) && d == 2)
          break;
      }
    }
  if (!found && path.length)
    g_array_append_val(out, path);
}
static GArray *reference(const Game *g) {
  Ref r = {.side = g->turn, .brazil = g->variant == GAME_BRAZILIAN};
  for (int p = 0; p < 32; p++) {
    int side = game_cell(g, p);
    if (side)
      r.cells[p / 4][(p % 4) * 2 + (p / 4) % 2] =
          (side == 1 ? 1 : -1) * (game_is_king(g, p) ? 2 : 1);
  }
  GArray *out = g_array_new(FALSE, FALSE, sizeof(GameMove));
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++)
      if (r.cells[y][x] && (r.cells[y][x] > 0) == (r.side == 1))
        reference_chains(r, x, y, (GameMove){.from = index_of(x, y)}, 0, out);
  if (out->len) {
    if (r.brazil) {
      unsigned max = 0;
      for (unsigned i = 0; i < out->len; i++)
        max = MAX(max, g_array_index(out, GameMove, i).length);
      for (unsigned i = out->len; i-- > 0;)
        if (g_array_index(out, GameMove, i).length < max)
          g_array_remove_index(out, i);
    }
    return out;
  }
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
      int value = r.cells[y][x];
      if (!value || (value > 0) != (r.side == 1))
        continue;
      for (int dy = -1; dy <= 1; dy += 2)
        for (int dx = -1; dx <= 1; dx += 2) {
          if (ABS(value) == 1 && dy != (r.side == 1 ? 1 : -1))
            continue;
          for (int d = 1; inside(x + dx * d, y + dy * d); d++) {
            int nx = x + dx * d, ny = y + dy * d;
            if (r.cells[ny][nx])
              break;
            GameMove m = {.from = index_of(x, y),
                          .length = 1,
                          .land = {index_of(nx, ny)}};
            g_array_append_val(out, m);
            if (ABS(value) == 1 || !r.brazil)
              break;
          }
        }
    }
  return out;
}
static void independent(void) {
  g_autoptr(GRand) rng = g_rand_new_with_seed(431);
  for (int v = 0; v < 2; v++)
    for (int sample = 0; sample < 500; sample++) {
      Game g = {.variant = v, .turn = 1 + sample % 2};
      for (int p = 0; p < 32; p++) {
        unsigned cell = g_rand_int_range(rng, 0, 6);
        if (cell < 2) {
          g.pieces[cell] |= B(p);
          if (g_rand_boolean(rng))
            g.kings |= B(p);
        }
      }
      /* Respect the 12-piece capture storage bound. */
      if (game_count(&g, 1) > 12 || game_count(&g, 2) > 12) {
        sample--;
        continue;
      }
      g_autoptr(GArray) ref = reference(&g);
      g_auto(GameMoves) legal = {0};
      game_legal_moves(&g, &legal);
      g_assert_cmpuint(ref->len, ==, legal.count);
      for (unsigned i = 0; i < legal.count; i++) {
        unsigned matches = 0;
        for (unsigned j = 0; j < ref->len; j++) {
          GameMove m = g_array_index(ref, GameMove, j);
          matches += m.length == legal.moves[i].length &&
                     game_move_prefix(m, legal.moves[i]);
        }
        g_assert_cmpuint(matches, ==, 1);
        Game next = g;
        g_assert_true(game_play(&next, legal.moves[i]));
        g_assert_cmpuint(next.pieces[0] & next.pieces[1], ==, 0);
        g_assert_cmpuint(next.kings & ~(next.pieces[0] | next.pieces[1]), ==,
                         0);
      }
    }
}
static gboolean cancelled(gpointer p) {
  unsigned *n = p;
  return ++*n > 3;
}
static double exhaustive(const Game *g, int side, unsigned depth) {
  if (!depth || g->result)
    return search_value(g, side, 0);
  g_autoptr(GArray) moves = reference(g);
  double best = g->turn == side ? -1e9 : 1e9;
  for (unsigned i = 0; i < moves->len; i++) {
    Game next = *g;
    g_assert_true(game_play(&next, g_array_index(moves, GameMove, i)));
    double value = exhaustive(&next, side, depth - 1);
    best = g->turn == side ? MAX(best, value) : MIN(best, value);
  }
  return best;
}
static void search(void) {
  for (int v = 0; v < 2; v++) {
    Game g = position(v, "a1 c3", "f6 h8", "a1 h8", 1);
    double expected = exhaustive(&g, 1, 3);
    g_assert_cmpfloat(search_value(&g, 1, 3), ==, expected);
    g_autoptr(GRand) a = g_rand_new_with_seed(42), b = g_rand_new_with_seed(42);
    SearchResult plain = search_choose(&g, 3, 1000000, FALSE, a);
    SearchResult prune = search_choose(&g, 3, 1000000, TRUE, b);
    g_assert_cmpuint(plain.depth, ==, 3);
    g_assert_cmpuint(prune.depth, ==, 3);
    g_assert_true(game_move_prefix(plain.move, prune.move));
    Game next = g;
    game_play(&next, plain.move);
    g_assert_cmpfloat(exhaustive(&next, 1, 2), ==, expected);
    SearchResult limited = search_choose(&g, 6, 1, TRUE, a);
    g_assert_cmpuint(limited.depth, ==, 0);
    g_assert_cmpuint(limited.nodes, ==, 1);
    g_assert_true(game_move_legal(&g, limited.move));
    g_rand_set_seed(a, 91);
    SearchResult complete = search_choose(&g, 1, 1000000, TRUE, a);
    g_rand_set_seed(a, 91);
    SearchResult retained = search_choose(&g, 6, complete.nodes + 1, TRUE, a);
    g_assert_cmpuint(retained.depth, ==, 1);
    g_assert_true(game_move_prefix(complete.move, retained.move));
    g_assert_cmpuint(retained.nodes, ==, complete.nodes + 1);
    unsigned calls = 0;
    g_auto(GameMoves) moves = {0};
    g_assert_false(game_legal_moves_full(&g, &moves, cancelled, &calls));
    g_assert_cmpuint(moves.count, ==, 0);
    calls = 0;
    limited = search_choose_full(&g, 6, 1000000, TRUE, a, cancelled, &calls);
    g_assert_cmpuint(limited.depth, ==, 0);
  }
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/checkers/initial", initial);
  g_test_add_func("/checkers/rules", rules);
  g_test_add_func("/checkers/endings", endings);
  g_test_add_func("/checkers/independent", independent);
  g_test_add_func("/checkers/search", search);
  return g_test_run();
}
