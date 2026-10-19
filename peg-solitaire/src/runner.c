// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <stdio.h>
#include <math.h>
int main(int argc, char **argv)
{
    gint runs = 1, seed = 0, memory = 256; double seconds = 60;
    char *controller = NULL, *board_name = NULL, *goal_name = NULL;
    GOptionEntry options[] = {
        {"runs", 'n', 0, G_OPTION_ARG_INT, &runs, "Attempts", "N"},
        {"seed", 0, 0, G_OPTION_ARG_INT, &seed, "First seed", "N"},
        {"memory-limit", 0, 0, G_OPTION_ARG_INT, &memory, "Solver memory in MiB", "N"},
        {"time-limit", 0, 0, G_OPTION_ARG_DOUBLE, &seconds, "Planning seconds (0: unlimited)", "N"},
        {"board", 0, 0, G_OPTION_ARG_STRING, &board_name, "english or triangle", "BOARD"},
        {"goal", 0, 0, G_OPTION_ARG_STRING, &goal_name, "any or center (English only)", "GOAL"},
        {"controller", 0, 0, G_OPTION_ARG_STRING, &controller, "random, heuristic, backtracking, astar", "NAME"}, {NULL}
    };
    g_autoptr(GOptionContext) context = g_option_context_new("— seeded Peg Solitaire attempts; CSV on stdout");
    g_option_context_add_main_entries(context, options, NULL); GError *error = NULL;
    gboolean parsed = g_option_context_parse(context, &argc, &argv, &error);
    if (!controller) controller = g_strdup("backtracking");
    if (!board_name) board_name = g_strdup("english");
    if (!goal_name) goal_name = g_strdup("any");
    PlayerKind kind = g_str_equal(controller,"random") ? PLAYER_RANDOM : g_str_equal(controller,"heuristic") ? PLAYER_HEURISTIC :
        g_str_equal(controller,"backtracking") ? PLAYER_BACKTRACKING : g_str_equal(controller,"astar") ? PLAYER_ASTAR : PLAYER_HUMAN;
    BoardKind board = g_str_equal(board_name,"triangle") ? BOARD_TRIANGLE : BOARD_ENGLISH;
    GoalKind goal = g_str_equal(goal_name,"center") ? GOAL_CENTER : GOAL_ANY;
    Game game;
    if (!parsed || argc != 1 || runs < 1 || seed < 0 || (guint64) seed+runs-1 > G_MAXINT || memory < 1 || memory > 4096 ||
        !isfinite(seconds) || seconds < 0 || kind == PLAYER_HUMAN ||
        (!g_str_equal(board_name,"english") && !g_str_equal(board_name,"triangle")) ||
        (!g_str_equal(goal_name,"any") && !g_str_equal(goal_name,"center")) || !game_init(&game,board,goal)) {
        g_printerr("%s\n", error ? error->message : "Invalid arguments. Use --help."); g_clear_error(&error);
        g_free(controller); g_free(board_name); g_free(goal_name); return 2;
    }
    printf("game,seed,controller,board,goal,result,search_status,pegs_remaining,jumps,sequence,nodes,elapsed_us,peak_memory_bytes\n");
    for (int run = 0; run < runs; run++) {
        game_init(&game,board,goal); const Board *b = game_board(board); g_autoptr(GRand) random = g_rand_new_with_seed(seed+run);
        SearchLimits limits = {seconds, (gsize) memory*1024*1024, 0}; SearchResult plan = {0};
        g_autoptr(GString) sequence = g_string_new(NULL); guint64 nodes = 0; gsize peak = 0; const char *status = "not-used";
        gint64 start = g_get_monotonic_time();
        if (kind == PLAYER_BACKTRACKING || kind == PLAYER_ASTAR) {
            plan = search_solve(&game,kind,&limits,NULL,seed+run); nodes = plan.nodes; peak = plan.peak_memory; status = search_status_name(plan.status);
        }
        while (game_result(&game) == GAME_ONGOING) {
            int move = -1;
            if (kind == PLAYER_BACKTRACKING || kind == PLAYER_ASTAR) {
                if (plan.status != SEARCH_SOLVED || game.moves >= plan.length) break;
                move = plan.path[game.moves];
            } else {
                MoveList legal = game_legal_moves(&game), choices = {0}; gint64 best = G_MININT64;
                if (kind == PLAYER_RANDOM) choices = legal;
                else for (unsigned i = 0; i < legal.count; i++) {
                    gint64 score = game_heuristic(&game,legal.items[i]);
                    if (score > best) { best = score; choices.count = 0; }
                    if (score == best) choices.items[choices.count++] = legal.items[i];
                }
                move = game_random_move(&choices,random);
            }
            if (move < 0 || !game_play(&game,move)) { g_printerr("Illegal algorithm move\n"); return 1; }
            Move m = b->jumps[move]; g_string_append_printf(sequence,"%s%s>%s>%s",sequence->len ? " " : "",b->labels[m.source],b->labels[m.middle],b->labels[m.destination]);
        }
        printf("peg-solitaire,%d,%s,%s,%s,%s,%s,%u,%u,%s,%" G_GUINT64_FORMAT ",%" G_GINT64_FORMAT ",%zu\n",seed+run,controller,board_name,goal_name,
            game_result_name(game_result(&game)),status,game_count(game.occupied),game.moves,sequence->str,nodes,g_get_monotonic_time()-start,peak);
    }
    g_free(controller); g_free(board_name); g_free(goal_name); return 0;
}
