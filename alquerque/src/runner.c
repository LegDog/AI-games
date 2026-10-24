// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <stdio.h>
int main(int argc, char **argv) {
  gint seed = 1, games = 10, depth = 3, budget = 50000;
  char *variant = NULL;
  gboolean swap = FALSE, plain = FALSE;
  GOptionEntry options[] = {
      {"variant", 0, 0, G_OPTION_ARG_STRING, &variant, "free or directional",
       "NAME"},
      {"seed", 0, 0, G_OPTION_ARG_INT, &seed, "Decision seed", "N"},
      {"games", 0, 0, G_OPTION_ARG_INT, &games, "Games", "N"},
      {"depth", 0, 0, G_OPTION_ARG_INT, &depth, "Search depth 1..6", "N"},
      {"budget", 0, 0, G_OPTION_ARG_INT, &budget, "Successor node budget", "N"},
      {"swap", 0, 0, G_OPTION_ARG_NONE, &swap, "Exchange search/random seats",
       NULL},
      {"plain", 0, 0, G_OPTION_ARG_NONE, &plain,
       "Plain Minimax instead of alpha-beta", NULL},
      {NULL, 0, 0, 0, NULL, NULL, NULL}};
  g_autoptr(GOptionContext) context =
      g_option_context_new("— Alquerque search versus random");
  g_option_context_add_main_entries(context, options, NULL);
  GError *error = NULL;
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("%s\n", error->message);
    g_clear_error(&error);
    return 2;
  }
  if (variant && !g_str_equal(variant, "free") &&
      !g_str_equal(variant, "directional")) {
    g_free(variant);
    return 2;
  }
  GameVariant rules = variant && g_str_equal(variant, "directional")
                          ? GAME_DIRECTIONAL
                          : GAME_FREE;
  g_free(variant);
  if (argc != 1 || games < 1 || games > 10000 || depth < 1 || depth > 6 ||
      budget < 1)
    return 2;
  puts("game,variant,seed,depth,budget,alpha_beta,swap,winner,end,actions,"
       "moves");
  for (int n = 0; n < games; n++) {
    guint32 game_seed = (guint32)seed + (guint32)n;
    GRand *rng = g_rand_new_with_seed(game_seed);
    Game g;
    game_init_variant(&g, rules);
    GString *moves = g_string_new(NULL);
    while (!game_result(&g)) {
      g_auto(GameMoves) legal = {0};
      game_legal_moves(&g, &legal);
      if (!legal.count)
        return 1;
      GameMove move = game_actor(&g) == (swap ? 2u : 1u)
                          ? search_choose(&g, depth, budget, !plain, rng).move
                          : legal.moves[g_rand_int_range(rng, 0, legal.count)];
      char label[GAME_LABEL_SIZE];
      game_move_label(move, label);
      g_string_append_printf(moves, "%s%s", g.actions ? ";" : "", label);
      if (!game_play(&g, move))
        return 1;
    }
    printf("%d,%s,%u,%d,%d,%d,%d,%d,%d,%u,%s\n", n + 1,
           game_variant_name(rules), game_seed, depth, budget, !plain, swap,
           game_result(&g), g.end, g.actions, moves->str);
    g_string_free(moves, TRUE);
    g_rand_free(rng);
  }
  return 0;
}
