// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ollama.h"
/* Fixed public positions shared by protocol regressions and opt-in live
 * comparisons. */
static inline void harness_fixture_init(Game *g, unsigned index) {
  (void)index;
  game_init(g);
  if (index) {
    const unsigned a[] = {0, 1, 0, 1, 0, 2}, b[] = {0, 1, 0, 1, 2, 1};
    for (unsigned i = 0; i < G_N_ELEMENTS(a); i++)
      g_assert_true(game_play(g, index == 1 ? a[i] : b[i]));
  }
}
static inline void harness_fixture_clear(Game *g) { (void)g; }
static inline gboolean harness_fixture_valid(const Game *g,
                                             const OllamaResult *r) {
  Game next = *g;
  return game_play(&next, r->move);
}
static inline int harness_fixture_metric(const Game *g, const OllamaResult *r) {
  Game next = *g;
  game_play(&next, r->move);
  return game_result(&next) == g->turn;
}
static inline char *harness_fixture_answer(const Game *g, gboolean last) {
  (void)g;
  (void)last;
  return NULL;
}

static inline char *harness_fixture_choice(const Game *g,
                                           const OllamaResult *r) {
  (void)g;
  return g_strdup_printf("%u", ollama_move_number(r->move));
}
