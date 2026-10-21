// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ollama.h"
/* Fixed public positions shared by protocol regressions and opt-in live
 * comparisons. */
static inline void harness_fixture_init(Game *g, unsigned index) {
  (void)index;
  game_init(g);
  if (index == 1) {
    g->black = 15;
    g->white = SQUARE_BIT(30) | SQUARE_BIT(32) | SQUARE_BIT(34);
    g->actions = 7;
  } else if (index == 2) {
    game_play(g, (GameMove){0, 0, 0});
    game_play(g, (GameMove){12, 1, 1});
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
