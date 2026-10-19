// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
static void replay(Game *g, const SearchResult *r) {
  for (unsigned i = 0; i < r->path->len; i++) {
    GByteArray *p = game_push_path(g, g_array_index(r->path, guint16, i));
    g_assert_nonnull(p);
    for (unsigned j = 0; j < p->len; j++)
      g_assert_true(game_step(g, p->data[j], NULL));
    g_byte_array_unref(p);
  }
  g_assert_true(game_solved(g));
}
/* Independent breadth-first enumeration of concrete walking positions, with
 * zero-cost walks and unit-cost pushes (Dijkstra by push layers). */
static char *oracle_key(const Game *g) {
  GString *s = g_string_new(NULL);
  g_string_append_printf(s, "%u", g->pos.player);
  for (unsigned i = 0; i < g->level->boxes; i++)
    g_string_append_printf(s, ":%u", g->pos.boxes[i]);
  return g_string_free(s, FALSE);
}
static unsigned oracle(const Game *start) {
  GQueue queue = G_QUEUE_INIT;
  GHashTable *best =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  Game *first = g_new(Game, 1);
  *first = *start;
  g_queue_push_tail(&queue, first);
  g_hash_table_insert(best, oracle_key(first), GUINT_TO_POINTER(1));
  unsigned answer = G_MAXUINT;
  while (!g_queue_is_empty(&queue)) {
    Game *g = g_queue_pop_head(&queue);
    g_autofree char *key = oracle_key(g);
    if (GPOINTER_TO_UINT(g_hash_table_lookup(best, key)) != g->pushes + 1) {
      g_free(g);
      continue;
    }
    if (game_solved(g)) {
      answer = g->pushes;
      g_free(g);
      break;
    }
    for (Direction d = UP; d <= RIGHT; d++) {
      Game next = *g;
      gboolean pushed;
      if (!game_step(&next, d, &pushed))
        continue;
      g_autofree char *k = oracle_key(&next);
      unsigned previous = GPOINTER_TO_UINT(g_hash_table_lookup(best, k));
      if (previous && previous <= next.pushes + 1)
        continue;
      g_hash_table_replace(best, g_strdup(k),
                           GUINT_TO_POINTER(next.pushes + 1));
      Game *copy = g_new(Game, 1);
      *copy = next;
      if (pushed)
        g_queue_push_tail(&queue, copy);
      else
        g_queue_push_head(&queue, copy);
    }
    g_free(g);
  }
  g_queue_clear_full(&queue, g_free);
  g_hash_table_unref(best);
  return answer;
}
static void builtins(void) {
  GPtrArray *pack = levels_builtin();
  SearchLimits l = search_defaults();
  l.seconds = 5;
  l.memory_bytes = 16 * 1024 * 1024;
  for (unsigned i = 0; i < pack->len; i++)
    for (PlayerKind kind = PLAYER_GREEDY; kind <= PLAYER_ASTAR; kind++) {
      Game g;
      game_init(&g, g_ptr_array_index(pack, i));
      SearchResult r = search_solve(&g, kind, &l, NULL, 13);
      g_assert_cmpint(r.status, ==, SEARCH_SOLVED);
      g_assert_cmpuint(r.peak_memory, <=, l.memory_bytes);
      g_assert_cmpint(r.optimal, ==, kind == PLAYER_ASTAR);
      replay(&g, &r);
      search_result_clear(&r);
    }
  g_ptr_array_unref(pack);
}
static void optimal(void) {
  GPtrArray *pack = levels_builtin();
  SearchLimits l = search_defaults();
  l.memory_bytes = 8 * 1024 * 1024;
  for (unsigned i = 0; i < 3; i++) {
    Game g;
    game_init(&g, g_ptr_array_index(pack, i));
    unsigned expected = oracle(&g);
    for (unsigned pruning = 0; pruning < 2; pruning++) {
      l.pruning = pruning;
      SearchResult r = search_solve(&g, PLAYER_ASTAR, &l, NULL, 0);
      g_assert_cmpint(r.status, ==, SEARCH_SOLVED);
      g_assert_cmpuint(r.path->len, ==, expected);
      search_result_clear(&r);
    }
  }
  g_ptr_array_unref(pack);
}
static gboolean cancel(gpointer data) {
  (void)data;
  return TRUE;
}
static void limits(void) {
  GPtrArray *pack = levels_builtin();
  Game g;
  game_init(&g, g_ptr_array_index(pack, 3));
  SearchLimits l = search_defaults();
  l.memory_bytes = 1;
  SearchResult r = search_solve(&g, PLAYER_ASTAR, &l, NULL, 0);
  g_assert_cmpint(r.status, ==, SEARCH_LIMIT);
  search_result_clear(&r);
  l = search_defaults();
  l.node_limit = 1;
  r = search_solve(&g, PLAYER_ASTAR, &l, NULL, 0);
  g_assert_cmpint(r.status, ==, SEARCH_LIMIT);
  search_result_clear(&r);
  l.seconds = 1e-12;
  r = search_solve(&g, PLAYER_ASTAR, &l, NULL, 0);
  g_assert_cmpint(r.status, ==, SEARCH_LIMIT);
  search_result_clear(&r);
  SearchControl c = {.cancelled = cancel};
  r = search_solve(&g, PLAYER_ASTAR, NULL, &c, 0);
  g_assert_cmpint(r.status, ==, SEARCH_CANCELLED);
  search_result_clear(&r);
  g_ptr_array_unref(pack);
  const char *text = "######\n#$@ .#\n#    #\n######";
  pack = levels_parse(text, strlen(text), NULL);
  game_init(&g, g_ptr_array_index(pack, 0));
  l = search_defaults();
  l.memory_bytes = 1024 * 1024;
  for (unsigned p = 0; p < 2; p++) {
    l.pruning = p;
    r = search_solve(&g, PLAYER_ASTAR, &l, NULL, 0);
    g_assert_cmpint(r.status, ==, SEARCH_UNSOLVABLE);
    search_result_clear(&r);
  }
  g_ptr_array_unref(pack);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/search/builtins", builtins);
  g_test_add_func("/search/optimal", optimal);
  g_test_add_func("/search/limits", limits);
  return g_test_run();
}
