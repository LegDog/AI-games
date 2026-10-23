// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <math.h>
typedef struct {
  guint64 nodes, budget;
  gboolean stopped, prune;
} Context;
static double evaluate(const Game *g, int role) {
  double value;
  if (game_hares_blocked(g))
    value = -100000. + g->hare_moves;
  else if (g->round_over)
    value = 100000.;
  else {
    unsigned exits = game_mobility(g, 2), mobile = 0;
    for (int p = 0; p < 10; p++)
      if (g->cells[p] == 2)
        for (int q = 0; q < 10; q++)
          if (!g->cells[q] && (game_neighbors[p] & (1u << q))) {
            mobile++;
            break;
          }
    value =
        20. * exits + 30. * mobile - 2. * game_mobility(g, 1) + g->hare_moves;
  }
  return role == 2 ? value : -value;
}
static double visit(const Game *g, int role, unsigned depth, double alpha,
                    double beta, Context *c) {
  if (c->nodes >= c->budget) {
    c->stopped = TRUE;
    return 0;
  }
  c->nodes++;
  if (!depth || g->round_over)
    return evaluate(g, role);
  GameMoves legal;
  game_legal_moves(g, &legal);
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
SearchResult search_choose(const Game *g, unsigned depth, guint64 budget,
                           gboolean alpha_beta, GRand *rng) {
  GameMoves legal;
  game_legal_moves(g, &legal);
  SearchResult result = {.move = {-1, -1}};
  if (!legal.count)
    return result;
  result.move = legal.moves[0];
  Context c = {.budget = MAX(budget, 1), .prune = alpha_beta};
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
