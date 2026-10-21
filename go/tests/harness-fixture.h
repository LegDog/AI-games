// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ollama.h"
/* Fixed public positions shared by protocol regressions and opt-in live
 * comparisons. */
static inline void harness_fixture_init(Game *g, unsigned index) {
  (void)index;
  game_init(g, 7.5);
  if (index == 1) {
    game_play(g, 0);
    game_play(g, 1);
  } else if (index == 2)
    game_play(g, GO_PASS);
}
static inline void harness_fixture_clear(Game *g) { game_clear(g); }
static inline gboolean harness_fixture_valid(const Game *g,
                                             const OllamaResult *r) {
  return game_try(g, r->move, NULL) == MOVE_OK;
}
static inline int harness_fixture_metric(const Game *g, const OllamaResult *r) {
  (void)g;
  (void)r;
  return -1;
}
static inline char *harness_fixture_answer(const Game *g, gboolean last) {
  (void)g;
  (void)last;
  return NULL;
}

static inline char *harness_fixture_choice(const Game *g,
                                           const OllamaResult *r) {
  (void)g;
  return g_strdup(game_label(r->move));
}
