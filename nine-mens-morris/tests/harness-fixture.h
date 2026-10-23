// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ollama.h"
/* Fixed public positions shared by protocol regressions and opt-in live
 * comparisons. */
static inline void harness_fixture_init(Game *g, unsigned index) {
  game_init(g);
  if (index == 1)
    *g = (Game){.pieces = {(1u << 0) | (1u << 1) | (1u << 3) | (1u << 8),
                           (1u << 16) | (1u << 17) | (1u << 18)},
                .turn = 1};
  if (index == 2)
    *g = (Game){.pieces = {(1u << 0) | (1u << 1) | (1u << 8),
                           (1u << 4) | (1u << 12) | (1u << 20) | (1u << 22)},
                .turn = 1};
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
