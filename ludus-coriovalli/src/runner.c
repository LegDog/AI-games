// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <stdio.h>
int main(int argc, char **argv) {
  gint seed = 1, matches = 10, depth = 3, limit = 100;
  gboolean swap = FALSE, plain = FALSE;
  GOptionEntry options[] = {
      {"seed", 0, 0, G_OPTION_ARG_INT, &seed, "Decision seed", "N"},
      {"matches", 0, 0, G_OPTION_ARG_INT, &matches, "Paired matches", "N"},
      {"depth", 0, 0, G_OPTION_ARG_INT, &depth, "Search depth 1..6", "N"},
      {"limit", 0, 0, G_OPTION_ARG_INT, &limit, "Hare moves per round 1..1000",
       "N"},
      {"swap", 0, 0, G_OPTION_ARG_NONE, &swap, "Exchange controller seats",
       NULL},
      {"plain", 0, 0, G_OPTION_ARG_NONE, &plain,
       "Plain Minimax instead of alpha-beta", NULL},
      {NULL, 0, 0, 0, NULL, NULL, NULL}};
  g_autoptr(GOptionContext) context =
      g_option_context_new("— paired Ludus Coriovalli matches");
  g_option_context_add_main_entries(context, options, NULL);
  GError *error = NULL;
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("%s\n", error->message);
    g_clear_error(&error);
    return 2;
  }
  if (argc != 1 || matches < 1 || matches > 10000 || depth < 1 || depth > 6 ||
      limit < 1 || limit > 1000)
    return 2;
  puts("match,seed,depth,alpha_beta,swap,limit,winner,survival1,survival2,"
       "capped1,capped2,actions,moves");
  for (int n = 0; n < matches; n++) {
    guint32 match_seed = (guint32)seed + n;
    GRand *rng = g_rand_new_with_seed(match_seed);
    Game g;
    game_init_limit(&g, limit);
    GString *moves = g_string_new(NULL);
    while (!game_result(&g)) {
      if (g.round_over) {
        game_next_round(&g);
        continue;
      }
      GameMoves legal;
      game_legal_moves(&g, &legal);
      GameMove move = game_actor(&g) == (swap ? 2u : 1u)
                          ? search_choose(&g, depth, 50000, !plain, rng).move
                          : legal.moves[g_rand_int_range(rng, 0, legal.count)];
      char label[16];
      game_move_label(move, label);
      g_string_append_printf(moves, "%s%u:%u:%s", g.actions ? ";" : "",
                             g.round + 1, game_actor(&g), label);
      if (!game_play(&g, move))
        return 1;
    }
    printf("%d,%u,%d,%d,%d,%d,%d,%u,%u,%d,%d,%u,%s\n", n + 1, match_seed, depth,
           !plain, swap, limit, game_result(&g), g.survival[0], g.survival[1],
           g.capped[0], g.capped[1], g.actions, moves->str);
    g_string_free(moves, TRUE);
    g_rand_free(rng);
  }
  return 0;
}
