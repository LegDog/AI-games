// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <stdio.h>
int main(int argc, char **argv) {
  gint seed = 1, games = 10, depth = 1, limit = 10000;
  gboolean swap = FALSE;
  GOptionEntry options[] = {
      {"swap", 0, 0, G_OPTION_ARG_NONE, &swap, "Exchange controller seats",
       NULL},
      {"seed", 0, 0, G_OPTION_ARG_INT, &seed, "Dice seed", "N"},
      {"games", 0, 0, G_OPTION_ARG_INT, &games, "Number of games", "N"},
      {"depth", 0, 0, G_OPTION_ARG_INT, &depth,
       "Player 1 expectiminimax depth 1..3; player 2 random", "N"},
      {"max-actions", 0, 0, G_OPTION_ARG_INT, &limit,
       "Action limit (unfinished games marked)", "N"},
      {NULL, 0, 0, 0, NULL, NULL, NULL}};
  g_autoptr(GOptionContext) context =
      g_option_context_new("— seeded Ur experiment");
  g_option_context_add_main_entries(context, options, NULL);
  GError *error = NULL;
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("%s\n", error->message);
    g_clear_error(&error);
    return 2;
  }
  if (argc != 1 || games < 1 || games > 100000 || depth < 1 || depth > 3 ||
      limit < 1 || limit > 1000000)
    return 2;
  puts("game,seed,depth,swap,winner,actions,moves");
  for (int n = 0; n < games; n++) {
    guint32 game_seed = (guint32)seed + n;
    GRand *dice = g_rand_new_with_seed(game_seed),
          *rng = g_rand_new_with_seed(game_seed ^ 0x9e3779b9);
    Game g;
    game_init(&g);
    GString *moves = g_string_new(NULL);
    while (!game_result(&g) && g.actions < (unsigned)limit) {
      int roll = game_roll(&g, dice);
      GameMoves legal;
      game_legal_moves(&g, &legal);
      GameMove m = g.turn == (swap ? 2 : 1)
                       ? search_choose(&g, depth, rng)
                       : legal.moves[g_rand_int_range(rng, 0, legal.count)];
      char label[16];
      game_move_label(m, label);
      g_string_append_printf(moves, "%s%d:%d:%s", g.actions ? ";" : "", g.turn,
                             roll, label);
      game_play(&g, m);
    }
    printf("%d,%u,%d,%d,%d,%u,%s\n", n + 1, game_seed, depth, swap,
           game_result(&g), g.actions, moves->str);
    g_string_free(moves, TRUE);
    g_rand_free(dice);
    g_rand_free(rng);
  }
  return 0;
}
