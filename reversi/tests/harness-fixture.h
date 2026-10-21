// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ollama.h"
/* Fixed public positions shared by protocol regressions and opt-in live
 * comparisons. */
static inline void harness_fixture_init(Game *g, unsigned index) {
  (void)index;
  game_init(g);
  for (unsigned i = 0; i < index * 2; i++) {
    guint64 legal = game_legal_moves(g);
    unsigned p = 0;
    while (p < 64 && !(legal & SQUARE_BIT(p)))
      p++;
    g_assert_true(game_play(g, p));
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
  return g_strdup(r->move < 64 ? game_labels[r->move] : "pass");
}
