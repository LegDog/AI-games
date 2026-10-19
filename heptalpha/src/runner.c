// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
    gint runs = 10, seed = 0, depth = GAME_MAX_DEPTH;
    char *controller = NULL;
    GOptionEntry options[] = {
        {"runs", 'n', 0, G_OPTION_ARG_INT, &runs, "Number of attempts", "N"},
        {"seed", 0, 0, G_OPTION_ARG_INT, &seed, "First seed", "N"},
        {"depth", 0, 0, G_OPTION_ARG_INT, &depth, "Search depth (1-20)", "N"},
        {"controller", 0, 0, G_OPTION_ARG_STRING, &controller, "random, heuristic, or search", "NAME"},
        {NULL}
    };
    g_autoptr(GOptionContext) context = g_option_context_new("— repeatable Heptalpha attempts; CSV on stdout");
    g_option_context_add_main_entries(context, options, NULL);
    GError *error = NULL;
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("%s\n", error->message); g_clear_error(&error); g_free(controller); return 2;
    }
    if (!controller) controller = g_strdup("search");
    if (argc != 1 || runs < 1 || seed < 0 || (guint64) seed + runs - 1 > G_MAXINT || depth < 1 || depth > GAME_MAX_DEPTH ||
        (!g_str_equal(controller, "random") && !g_str_equal(controller, "heuristic") && !g_str_equal(controller, "search"))) {
        g_printerr("Invalid arguments. Use --help.\n"); g_free(controller); return 2;
    }
    printf("game,seed,controller,depth,result,pieces,sequence,nodes,elapsed_us\n");
    for (int run = 0; run < runs; run++) {
        g_autoptr(GRand) random = g_rand_new_with_seed(seed + run);
        Game game; game_init(&game);
        g_autoptr(GString) sequence = g_string_new(NULL);
        guint64 nodes = 0;
        gint64 start = g_get_monotonic_time();
        while (!game_result(&game)) {
            guint64 candidates = game_legal_moves(&game);
            if (g_str_equal(controller, "search")) {
                SearchResult result; search_evaluate(&game, depth, NULL, &result);
                candidates = result.best; nodes += result.nodes;
            } else if (g_str_equal(controller, "heuristic")) {
                int best = -1; candidates = 0;
                for (unsigned m = 0; m < GAME_CHOICES; m++) if (game_legal(&game, m)) {
                    int score = game_heuristic(&game, m);
                    if (score > best) { best = score; candidates = 0; }
                    if (score == best) candidates |= MOVE_BIT(m);
                }
            }
            int move = game_random_move(candidates, random);
            if (move < 0 || !game_play(&game, move)) { g_free(controller); return 1; }
            Move m = game_moves[move];
            g_string_append_printf(sequence, "%s%s>%s>%s@p%u", sequence->len ? " " : "",
                game_labels[m.entry], game_labels[m.middle], game_labels[m.destination], m.path + 1);
        }
        printf("heptalpha,%d,%s,%d,%s,%u,%s,%" G_GUINT64_FORMAT ",%" G_GINT64_FORMAT "\n",
            seed + run, controller, depth, game_result(&game) == 1 ? "solved" : "stuck", game.moves,
            sequence->str, nodes, g_get_monotonic_time() - start);
    }
    g_free(controller);
    return 0;
}
