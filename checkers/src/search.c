// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <math.h>
typedef struct {
  guint64 nodes, budget;
  gboolean stopped, prune;
  gboolean (*cancelled)(gpointer);
  gpointer cancel_data;
} Context;
/* Shared phase-aware evaluation; terminal scores dominate every heuristic. */
static double evaluate(const Game *g, int role) {
  int result = game_result(g);
  if (result == GAME_DRAW)
    return 0;
  if (result)
    return result == role ? 100000. : -100000.;
  double values[2] = {0, 0};
  for (int side = 1; side <= 2; side++) {
    unsigned kings = 0, advancement = 0;
    for (int p = 0; p < GAME_POINTS; p++)
      if (game_cell(g, p) == (unsigned)side) {
        if (game_is_king(g, p))
          kings++;
        else
          advancement += side == 1 ? p / 4 : 7 - p / 4;
      }
    values[side - 1] = 100. * game_count(g, side) + 200. * kings +
                       4. * game_mobility(g, side) + 2. * advancement;
  }
  return values[role - 1] - values[2 - role];
}
static double visit(const Game *g, int role, unsigned depth, double alpha,
                    double beta, Context *c) {
  if (c->nodes >= c->budget || (c->cancelled && c->cancelled(c->cancel_data))) {
    c->stopped = TRUE;
    return 0;
  }
  c->nodes++;
  if (!depth || game_result(g))
    return evaluate(g, role);
  g_auto(GameMoves) legal = {0};
  if (!game_legal_moves_full(g, &legal, c->cancelled, c->cancel_data)) {
    c->stopped = TRUE;
    return 0;
  }
  if (!legal.count)
    return evaluate(g, role);
  double best = g->turn == role ? -1e9 : 1e9;
  for (unsigned i = 0; i < legal.count; i++) {
    Game next = *g;
    game_play(&next, legal.moves[i]);
    double v = visit(&next, role, depth - 1, alpha, beta, c);
    if (c->stopped)
      return 0;
    if (g->turn == role) {
      best = MAX(best, v);
      alpha = MAX(alpha, best);
    } else {
      best = MIN(best, v);
      beta = MIN(beta, best);
    }
    if (c->prune && alpha >= beta)
      break;
  }
  return best;
}
double search_value(const Game *g, int role, unsigned depth) {
  Context c = {.budget = G_MAXUINT64};
  return visit(g, role, MIN(depth, 6u), -1e9, 1e9, &c);
}
SearchResult search_choose_full(const Game *g, unsigned depth, guint64 budget,
                                gboolean alpha_beta, GRand *rng,
                                gboolean (*cancelled)(gpointer),
                                gpointer cancel_data) {
  g_auto(GameMoves) legal = {0};
  SearchResult empty = {.move = {.from = -1}};
  if (!game_legal_moves_full(g, &legal, cancelled, cancel_data))
    return empty;
  SearchResult result = {.move = {.from = -1}};
  if (!legal.count)
    return result;
  result.move = legal.moves[0];
  Context c = {.budget = MAX(budget, 1),
               .prune = alpha_beta,
               .cancelled = cancelled,
               .cancel_data = cancel_data};
  for (unsigned d = 1; d <= CLAMP(depth, 1u, 6u); d++) {
    double best = -1e9;
    GameMove chosen = result.move;
    unsigned ties = 0;
    for (unsigned i = 0; i < legal.count; i++) {
      Game next = *g;
      game_play(&next, legal.moves[i]);
      /* Full root windows preserve exact ties for both algorithms. */
      double value = visit(&next, g->turn, d - 1, -1e9, 1e9, &c);
      if (c.stopped)
        break;
      if (value > best + 1e-8) {
        best = value;
        chosen = legal.moves[i];
        ties = 1;
      } else if (fabs(value - best) < 1e-8 &&
                 g_rand_int_range(rng, 0, ++ties) == 0)
        chosen = legal.moves[i];
    }
    if (c.stopped)
      break;
    result.move = chosen;
    result.depth = d;
  }
  result.nodes = c.nodes;
  return result;
}

SearchResult search_choose(const Game *g, unsigned depth, guint64 budget,
                           gboolean alpha_beta, GRand *rng) {
  return search_choose_full(g, depth, budget, alpha_beta, rng, NULL, NULL);
}
