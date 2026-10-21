// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ollama.h"
/* Fixed public positions shared by protocol regressions and opt-in live
 * comparisons. */
static inline void harness_fixture_init(Game *g, unsigned index) {
  (void)index;
  game_init(g, index == 1 ? RUNNING : BACKGAMMON, SESSION_FIXED, 5);
  if (index == 1) {
    game_opening(g, 6, 1);
    game_roll(g, 2, 2);
  } else {
    g->phase = PRE_ROLL;
    if (index == 2)
      game_control(g, ACTION_DOUBLE);
  }
}
static inline void harness_fixture_clear(Game *g) { (void)g; }
static inline gboolean harness_fixture_valid(const Game *g,
                                             const OllamaResult *r) {
  if (!r->decision.checker && r->decision.action == ACTION_ROLL)
    return g->phase == PRE_ROLL;
  Game next = *g;
  return decision_apply(&next, &r->decision);
}
static inline int harness_fixture_metric(const Game *g, const OllamaResult *r) {
  Game next = *g;
  decision_apply(&next, &r->decision);
  return game_finished(&next) && next.winner == g->actor;
}
static inline char *harness_fixture_answer(const Game *g, gboolean last) {
  (void)g;
  (void)last;
  return NULL;
}

static inline char *harness_fixture_choice(const Game *g,
                                           const OllamaResult *r) {
  (void)g;
  GString *text = g_string_new(NULL);
  if (r->decision.checker) {
    for (unsigned i = 0; i < r->decision.plan.length; i++) {
      char label[64];
      step_label(r->decision.plan.steps[i], label, sizeof label);
      g_string_append_printf(text, "%s%s", i ? "; " : "", label);
    }
  } else
    g_string_append_printf(text, "action:%d", r->decision.action);
  return g_string_free(text, FALSE);
}
