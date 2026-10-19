// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static int controller(const char *s) {
  if (!strcmp(s, "random"))
    return PLAYER_RANDOM;
  if (!strcmp(s, "heuristic"))
    return PLAYER_HEURISTIC;
  if (!strcmp(s, "mcts"))
    return PLAYER_MCTS;
  return -1;
}
int main(int argc, char **argv) {
  int games = 1, seed = 0, iterations[2] = {1000, 1000}, memory = 64,
      action_limit = 1000;
  double seconds = 5, komi = 7.5;
  char *names[2] = {NULL, NULL};
  GOptionEntry options[] = {
      {"games", 'n', 0, G_OPTION_ARG_INT, &games, "Games", "N"},
      {"seed", 0, 0, G_OPTION_ARG_INT, &seed, "First seed", "N"},
      {"first", 0, 0, G_OPTION_ARG_STRING, &names[0],
       "Black: random, heuristic, mcts", "NAME"},
      {"second", 0, 0, G_OPTION_ARG_STRING, &names[1], "White controller",
       "NAME"},
      {"komi", 0, 0, G_OPTION_ARG_DOUBLE, &komi,
       "White komi, 0–20 in half points", "K"},
      {"iterations-first", 0, 0, G_OPTION_ARG_INT, &iterations[0],
       "Black MCTS simulations", "N"},
      {"iterations-second", 0, 0, G_OPTION_ARG_INT, &iterations[1],
       "White MCTS simulations", "N"},
      {"time-limit", 0, 0, G_OPTION_ARG_DOUBLE, &seconds,
       "Seconds per decision; 0 unlimited", "S"},
      {"memory-limit", 0, 0, G_OPTION_ARG_INT, &memory, "MiB per decision",
       "M"},
      {"action-limit", 0, 0, G_OPTION_ARG_INT, &action_limit,
       "Whole-game action limit", "N"},
      {NULL}};
  g_autoptr(GOptionContext) context =
      g_option_context_new("— reproducible 9×9 Go; CSV on stdout");
  g_option_context_add_main_entries(context, options, NULL);
  GError *error = NULL;
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("%s\n", error->message);
    g_clear_error(&error);
    g_free(names[0]);
    g_free(names[1]);
    return 2;
  }
  if (!names[0])
    names[0] = g_strdup("heuristic");
  if (!names[1])
    names[1] = g_strdup("random");
  gboolean valid = argc == 1 && games >= 1 && games <= 10000 && seed >= 0 &&
                   (guint64)seed + games - 1 <= G_MAXUINT32 && memory >= 1 &&
                   memory <= 4096 && isfinite(seconds) && seconds >= 0 &&
                   isfinite(komi) && komi >= 0 && komi <= 20 &&
                   komi * 2 == floor(komi * 2) && action_limit >= 1 &&
                   action_limit <= 100000;
  for (unsigned i = 0; i < 2; i++)
    valid &= controller(names[i]) >= 0 && iterations[i] >= 1 &&
             iterations[i] <= 1000000;
  if (!valid) {
    g_printerr("Invalid arguments; see --help.\n");
    g_free(names[0]);
    g_free(names[1]);
    return 2;
  }
  puts("game,rules,size,seed,first,second,komi,iterations_first,iterations_"
       "second,time_limit,memory_limit_bytes,action_limit,result,winner,"
       "actions,placements,passes,black_captures,white_captures,black_stones,"
       "white_stones,black_territory,white_territory,neutral,black_score,white_"
       "score,margin,sequence,nodes,completed_simulations,cutoffs,limited_"
       "decisions,elapsed_us,peak_memory_bytes");
  for (int run = 0; run < games; run++) {
    Game g;
    game_init(&g, komi);
    GRand *random[2] = {
        g_rand_new_with_seed((guint32)seed + run),
        g_rand_new_with_seed(((guint32)seed + run) ^ 0x9e3779b9u)};
    GString *sequence = g_string_new(NULL);
    guint64 nodes = 0, completed = 0, cutoffs = 0;
    gsize peak = 0;
    unsigned limited = 0, placements = 0, passes = 0;
    gint64 started = g_get_monotonic_time();
    while (!game_finished(&g) && g.history->len - 1 < (unsigned)action_limit) {
      unsigned color = g.state.turn;
      int kind = controller(names[color]), move = -1;
      if (kind == PLAYER_RANDOM) {
        MoveList legal = game_legal(&g);
        move = game_random(&legal, random[color]);
      } else if (kind == PLAYER_HEURISTIC) {
        int values[GO_CHOICES];
        move = game_heuristic(&g, random[color], values);
      } else {
        SearchOptions config = {iterations[color], seconds,
                                (gsize)memory * 1024 * 1024, 0};
        SearchResult result;
        gboolean ok = mcts_evaluate(&g, &config, NULL, random[color], &result);
        nodes += result.nodes;
        completed += result.completed;
        cutoffs += result.cutoffs;
        peak = MAX(peak, result.peak_memory);
        limited += result.status == SEARCH_LIMIT;
        if (!ok)
          break;
        move = search_choose(&result, random[color]);
      }
      if (move < 0 || !game_play(&g, move)) {
        g_printerr("Internal illegal move\n");
        return 1;
      }
      if (move == GO_PASS)
        passes++;
      else
        placements++;
      g_string_append_printf(sequence, "%s%s", sequence->len ? " " : "",
                             game_label(move));
    }
    Score score;
    game_score(&g, &score);
    int winner = game_winner(&g);
    printf("go,chinese-style-area-no-suicide-positional-superko,9,%u,%s,%s,%."
           "1f,%d,%d,%.9g,%zu,%d,%s,%s,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.1f,%.1f,"
           "%.1f,%s,%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT
           ",%" G_GUINT64_FORMAT ",%u,%" G_GINT64_FORMAT ",%zu\n",
           (guint32)seed + run, names[0], names[1], komi, iterations[0],
           iterations[1], seconds, (gsize)memory * 1024 * 1024, action_limit,
           winner < 0 ? "incomplete" : "completed",
           winner < 0        ? "none"
           : winner == BLACK ? "black"
           : winner == WHITE ? "white"
                             : "draw",
           g.history->len - 1, placements, passes, g.state.captures[0],
           g.state.captures[1], score.stones[0], score.stones[1],
           score.territory[0], score.territory[1], score.neutral, score.area[0],
           score.area[1], score.margin, sequence->str, nodes, completed,
           cutoffs, limited, g_get_monotonic_time() - started, peak);
    g_string_free(sequence, TRUE);
    game_clear(&g);
    g_rand_free(random[0]);
    g_rand_free(random[1]);
  }
  g_free(names[0]);
  g_free(names[1]);
  return 0;
}
