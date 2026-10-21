// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ollama.h"
/* Fixed public positions shared by protocol regressions and opt-in live
 * comparisons. */
static inline void harness_fixture_init(Game *g, unsigned index) {
  (void)index;
  game_init(g);
  for (unsigned i = 0; i < (index ? index == 1 ? 1 : 3 : 0); i++) {
    guint64 moves = game_legal_moves(g);
    if (!moves)
      break;
    for (unsigned m = 0; m < GAME_CHOICES; m++)
      if (moves & (G_GUINT64_CONSTANT(1) << m)) {
        game_play(g, m);
        break;
      }
  }
}
static inline void harness_fixture_clear(Game *g) { (void)g; }
static inline gboolean harness_fixture_valid(const Game *g,
                                             const OllamaResult *r) {
  return game_legal(g, r->move);
}
static inline int harness_fixture_metric(const Game *g, const OllamaResult *r) {
  Game next = *g;
  game_play(&next, r->move);
  return game_result(&next) == 1;
}
static inline char *harness_fixture_answer(const Game *g, gboolean last) {
  guint64 legal = game_legal_moves(g);
  unsigned m = 0;
  for (unsigned i = 0; i < GAME_CHOICES; i++)
    if (legal & (G_GUINT64_CONSTANT(1) << i)) {
      m = i;
      if (!last)
        break;
    }
  Move move = game_moves[m];
  return g_strdup_printf("{\"entry\":\"%s\",\"destination\":\"%s\"}",
                         game_labels[move.entry],
                         game_labels[move.destination]);
}

static inline char *harness_fixture_choice(const Game *g,
                                           const OllamaResult *r) {
  (void)g;
  return g_strdup_printf("move-index:%d", r->move);
}
