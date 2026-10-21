// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ollama.h"
/* Fixed public positions shared by protocol regressions and opt-in live
 * comparisons. */
static inline void harness_fixture_init(Game *g, unsigned index) {
  (void)index;
  game_init(g);
  if (index) {
    for (int i = 0; i < 4; i++)
      g->cells[i] = index == 1 ? 1 : -1;
    g->cells[30] = g->cells[47] = g->cells[64] = index == 1 ? -1 : 1;
    g->actions = 7;
  }
}
static inline void harness_fixture_clear(Game *g) { (void)g; }
static inline gboolean harness_fixture_valid(const Game *g,
                                             const OllamaResult *r) {
  return game_move_legal(g, r->move);
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
  char label[16];
  game_move_label(r->move, label);
  return g_strdup(label);
}
