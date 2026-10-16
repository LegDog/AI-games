// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"
#include "minimax.h"
#include <stdio.h>
#include <string.h>

static gboolean valid_player(const char *name)
{
    return g_str_equal(name, "positional") || g_str_equal(name, "minimax");
}

int main(int argc, char **argv)
{
    gint games = 10, first_seed = 0, depth1 = GAME_CHOICES == 9 ? 9 : 6;
    gint depth2 = depth1;
    char *first = NULL, *second = NULL;
    GOptionEntry options[] = {
        {"games", 'n', 0, G_OPTION_ARG_INT, &games, "Number of games", "N"},
        {"seed", 0, 0, G_OPTION_ARG_INT, &first_seed, "First seed (incremented per game)", "N"},
        {"first", 0, 0, G_OPTION_ARG_STRING, &first, "First player: positional or minimax", "PLAYER"},
        {"second", 0, 0, G_OPTION_ARG_STRING, &second, "Second player: positional or minimax", "PLAYER"},
        {"depth-first", 0, 0, G_OPTION_ARG_INT, &depth1, "First Minimax depth", "N"},
        {"depth-second", 0, 0, G_OPTION_ARG_INT, &depth2, "Second Minimax depth", "N"},
        {NULL}
    };
    g_autoptr(GOptionContext) context = g_option_context_new("— deterministic headless games; CSV on stdout");
    g_option_context_add_main_entries(context, options, NULL);
    GError *error = NULL;
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("%s\n", error->message);
        g_clear_error(&error);
        g_free(first);
        g_free(second);
        return 2;
    }
    if (!first) first = g_strdup("positional");
    if (!second) second = g_strdup("minimax");
    if (argc != 1 || games < 1 || first_seed < 0 ||
        (guint64) first_seed + (guint64) games - 1 > G_MAXINT ||
        !valid_player(first) || !valid_player(second) ||
        depth1 < 1 || depth1 > GAME_MAX_DEPTH || depth2 < 1 || depth2 > GAME_MAX_DEPTH) {
        g_printerr("Invalid arguments. Use --help; depths must be 1–%d.\n", GAME_MAX_DEPTH);
        g_free(first);
        g_free(second);
        return 2;
    }
    const char *players[] = {first, second};
    const unsigned depths[] = {(unsigned) depth1, (unsigned) depth2};
    printf("game,seed,first,second,depth_first,depth_second,result,moves,sequence\n");
    for (int i = 0; i < games; i++) {
        guint32 seed = (guint32) (first_seed + i);
        GRand *randoms[] = {g_rand_new_with_seed(seed), g_rand_new_with_seed(seed ^ 0x9e3779b9u)};
        Game game;
        game_init(&game);
        g_autoptr(GString) sequence = g_string_new(NULL);
        while (!game_result(&game)) {
            unsigned side = game.turn == 1 ? 0 : 1;
            int move;
            if (g_str_equal(players[side], "minimax")) {
                MinimaxResult result;
                if (!minimax_evaluate(&game, depths[side], NULL, &result)) {
                    g_printerr("Search failed.\n");
                    return 1;
                }
                move = minimax_choose(&result, randoms[side]);
            } else {
                Score scores[GAME_CHOICES];
                game_evaluate(&game, randoms[side], scores);
                move = game_best_move(scores);
            }
            if (move < 0 || !game_play(&game, (unsigned) move)) {
                g_printerr("Engine selected an illegal move.\n");
                return 1;
            }
            if (sequence->len) g_string_append_c(sequence, ' ');
            unsigned number = GAME_CHOICES == 9 ? (2 - (unsigned) move / 3) * 3 + (unsigned) move % 3 + 1 : (unsigned) move + 1;
            g_string_append_printf(sequence, "%u", number);
        }
        int result = game_result(&game);
        printf("%s,%u,%s,%s,%u,%u,%s,%u,%s\n",
            GAME_CHOICES == 9 ? "tic-tac-toe" : "four-in-a-row", seed, first, second,
            depths[0], depths[1], result == 2 ? "draw" : result == 1 ? "first" : "second",
            game.moves, sequence->str);
        g_rand_free(randoms[0]);
        g_rand_free(randoms[1]);
    }
    g_free(first);
    g_free(second);
    return ferror(stdout) ? 1 : 0;
}
