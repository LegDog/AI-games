// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <math.h>
static double evaluate(const Game *g, int player) {
  int winner = game_result(g);
  if (winner)
    return winner == player ? 10000 : -10000;
  double score[2] = {g->finished[0] * 50., g->finished[1] * 50.};
  for (int p = 1; p <= 30; p++)
    if (g->cells[p])
      score[g->cells[p] - 1] +=
          p + (p >= 26 ? 5 : 0) + (game_protected(g, p) ? 3 : 0);
  return score[player - 1] - score[2 - player];
}
double search_value(const Game *g, int player, unsigned depth) {
  if (!depth || game_result(g))
    return evaluate(g, player);
  if (g->roll < 0) {
    /* Four independent sticks: sums 1,2,3,4,5 have weights 4,6,4,1,1. */
    static const int weight[] = {0, 4, 6, 4, 1, 1};
    double total = 0;
    for (int roll = 1; roll <= 5; roll++) {
      Game next = *g;
      game_set_roll(&next, roll);
      total += weight[roll] * search_value(&next, player, depth);
    }
    return total / 16;
  }
  GameMoves moves;
  game_legal_moves(g, &moves);
  double best = g->turn == player ? -1e9 : 1e9;
  for (unsigned i = 0; i < moves.count; i++) {
    Game next = *g;
    game_play(&next, moves.moves[i]);
    double value = search_value(&next, player, depth - 1);
    best = g->turn == player ? MAX(best, value) : MIN(best, value);
  }
  return best;
}
GameMove search_choose(const Game *g, unsigned depth, GRand *rng) {
  GameMoves moves;
  game_legal_moves(g, &moves);
  GameMove chosen = {-1};
  double best = -1e9;
  unsigned ties = 0;
  for (unsigned i = 0; i < moves.count; i++) {
    Game next = *g;
    game_play(&next, moves.moves[i]);
    double value = search_value(&next, g->turn, CLAMP(depth, 1u, 3u) - 1);
    if (value > best + 1e-8) {
      best = value;
      chosen = moves.moves[i];
      ties = 1;
    } else if (fabs(value - best) < 1e-8 &&
               g_rand_int_range(rng, 0, ++ties) == 0)
      chosen = moves.moves[i];
  }
  return chosen;
}
