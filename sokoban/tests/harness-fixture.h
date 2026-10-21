// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ollama.h"
/* Fixed public positions shared by protocol regressions and opt-in live
 * comparisons. */
static GPtrArray *fixture_levels;
static inline void harness_fixture_init(Game *g, unsigned index) {
  (void)index;
  fixture_levels = levels_builtin();
  game_init(g, g_ptr_array_index(fixture_levels, index));
}
static inline void harness_fixture_clear(Game *g) {
  (void)g;
  g_clear_pointer(&fixture_levels, g_ptr_array_unref);
}
static inline gboolean harness_fixture_valid(const Game *g,
                                             const OllamaResult *r) {
  g_autoptr(GByteArray) path = game_push_path(g, r->move);
  return path != NULL;
}
static inline int harness_fixture_metric(const Game *g, const OllamaResult *r) {
  g_autoptr(GByteArray) path = game_push_path(g, r->move);
  Game next = *g;
  for (unsigned i = 0; path && i < path->len; i++)
    game_step(&next, path->data[i], NULL);
  return game_solved(&next);
}
static inline char *harness_fixture_answer(const Game *g, gboolean last) {
  MoveList moves = game_pushes(g);
  unsigned move = moves.items[last ? moves.count - 1 : 0], cell = move / 4;
  const char *dirs[] = {"up", "down", "left", "right"};
  return g_strdup_printf("{\"x\":%u,\"y\":%u,\"direction\":\"%s\"}",
                         cell % g->level->width + 1, cell / g->level->width + 1,
                         dirs[move % 4]);
}

static inline char *harness_fixture_choice(const Game *g,
                                           const OllamaResult *r) {
  (void)g;
  unsigned cell = r->move / 4;
  const char *dirs[] = {"up", "down", "left", "right"};
  return g_strdup_printf("%u,%u %s", cell % g->level->width + 1,
                         cell / g->level->width + 1, dirs[r->move % 4]);
}
