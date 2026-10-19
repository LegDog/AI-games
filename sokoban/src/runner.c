// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <gio/gio.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void csv(const char *s) {
  putchar('"');
  for (; *s; s++) {
    if (*s == '"')
      putchar('"');
    putchar(*s);
  }
  putchar('"');
}
int main(int argc, char **argv) {
  int level = 1, runs = 1, seed = 0, memory = 256, push_limit = 10000;
  double seconds = 60;
  gboolean no_pruning = FALSE;
  char *file = NULL, *controller = NULL;
  GOptionEntry entries[] = {
      {"level", 0, 0, G_OPTION_ARG_INT, &level, "Level number (1-based)", "N"},
      {"file", 0, 0, G_OPTION_ARG_FILENAME, &file,
       "XSB pack (otherwise built-in)", "FILE"},
      {"controller", 0, 0, G_OPTION_ARG_STRING, &controller,
       "astar, greedy, random", "NAME"},
      {"runs", 0, 0, G_OPTION_ARG_INT, &runs, "Runs", "N"},
      {"seed", 0, 0, G_OPTION_ARG_INT, &seed, "Initial seed", "N"},
      {"time-limit", 0, 0, G_OPTION_ARG_DOUBLE, &seconds,
       "Search seconds; 0 unlimited", "S"},
      {"memory-limit", 0, 0, G_OPTION_ARG_INT, &memory, "Search MiB", "M"},
      {"push-limit", 0, 0, G_OPTION_ARG_INT, &push_limit, "Random push limit",
       "N"},
      {"no-pruning", 0, 0, G_OPTION_ARG_NONE, &no_pruning,
       "Disable deadlock pruning", NULL},
      {NULL}};
  g_autoptr(GOptionContext) context = g_option_context_new(NULL);
  g_option_context_add_main_entries(context, entries, NULL);
  GError *error = NULL;
  if (!g_option_context_parse(context, &argc, &argv, &error)) {
    g_printerr("%s\n", error->message);
    g_clear_error(&error);
    return 2;
  }
  PlayerKind kind = PLAYER_ASTAR;
  if (controller) {
    if (!strcmp(controller, "random"))
      kind = PLAYER_RANDOM;
    else if (!strcmp(controller, "greedy"))
      kind = PLAYER_GREEDY;
    else if (strcmp(controller, "astar"))
      goto invalid;
  }
  if (argc != 1 || runs < 1 || runs > 10000 || seed < 0 ||
      (guint64)seed + runs - 1 > G_MAXUINT32 || memory < 1 || memory > 4096 ||
      !isfinite(seconds) || seconds < 0 || push_limit < 1)
    goto invalid;
  GPtrArray *pack = file ? levels_load(file, &error) : levels_builtin();
  if (!pack) {
    g_printerr("%s\n", error->message);
    g_clear_error(&error);
    g_free(file);
    g_free(controller);
    return 2;
  }
  if (level < 1 || level > (int)pack->len) {
    g_ptr_array_unref(pack);
    goto invalid;
  }
  puts("game,level,name,initial,seed,controller,pruning,time_limit,memory_"
       "limit_bytes,push_limit,result,search_status,"
       "optimal,pushes,walking,sequence,nodes,elapsed_us,peak_memory_bytes");
  for (int run = 0; run < runs; run++) {
    Game g;
    game_init(&g, g_ptr_array_index(pack, level - 1));
    g_autofree char *map = game_map(&g, "|");
    GString *sequence = g_string_new(NULL);
    SearchLimits limits = {seconds, (gsize)memory * 1024 * 1024, 0,
                           !no_pruning};
    SearchResult result = {.status = SEARCH_LIMIT};
    const char *outcome = "incomplete";
    if (kind == PLAYER_RANDOM) {
      GRand *random = g_rand_new_with_seed((guint32)seed + run);
      Analysis analysis;
      game_analyze(g.level, &analysis);
      gint64 start = g_get_monotonic_time();
      while (!game_solved(&g) && g.pushes < (unsigned)push_limit) {
        if (game_deadlocked(&g, &analysis)) {
          outcome = "deadlocked";
          break;
        }
        MoveList moves = game_pushes(&g);
        if (!moves.count) {
          outcome = "deadlocked";
          break;
        }
        unsigned kept = 0;
        for (unsigned i = 0; i < moves.count; i++) {
          if (no_pruning) {
            moves.items[kept++] = moves.items[i];
            continue;
          }
          Game next = g;
          next.pos.player = game_neighbor(g.level, moves.items[i] / 4,
                                          (moves.items[i] % 4) ^ 1);
          game_step(&next, moves.items[i] % 4, NULL);
          if (!game_deadlocked(&next, &analysis))
            moves.items[kept++] = moves.items[i];
        }
        if (!kept) {
          outcome = "unsolvable";
          break;
        }
        GByteArray *path =
            game_push_path(&g, moves.items[g_rand_int_range(random, 0, kept)]);
        for (unsigned i = 0; i < path->len; i++) {
          gboolean pushed;
          game_step(&g, path->data[i], &pushed);
          g_string_append_c(sequence,
                            (pushed ? "UDLR" : "udlr")[path->data[i]]);
        }
        g_byte_array_unref(path);
      }
      result.elapsed_us = g_get_monotonic_time() - start;
      g_rand_free(random);
    } else {
      result = search_solve(&g, kind, &limits, NULL, (guint32)seed + run);
      if (result.status == SEARCH_SOLVED) {
        for (unsigned p = 0; p < result.path->len; p++) {
          GByteArray *path =
              game_push_path(&g, g_array_index(result.path, guint16, p));
          if (!path) {
            g_printerr("Invalid solver path\n");
            return 1;
          }
          for (unsigned i = 0; i < path->len; i++) {
            gboolean pushed;
            if (!game_step(&g, path->data[i], &pushed)) {
              g_printerr("Invalid solver movement\n");
              g_byte_array_unref(path);
              return 1;
            }
            g_string_append_c(sequence,
                              (pushed ? "UDLR" : "udlr")[path->data[i]]);
          }
          g_byte_array_unref(path);
        }
      } else if (result.status == SEARCH_UNSOLVABLE)
        outcome = "unsolvable";
    }
    if (game_solved(&g))
      outcome = "solved";
    printf("sokoban,%d,", level);
    csv(g.level->name);
    putchar(',');
    csv(map);
    printf(",%u,", (guint32)seed + run);
    csv(player_name(kind));
    printf(",%s,%.9g,%zu,%d,%s,%s,%s,%u,%u,", no_pruning ? "off" : "on",
           seconds, (gsize)memory * 1024 * 1024, push_limit, outcome,
           kind == PLAYER_RANDOM ? "not-applicable"
                                 : search_status_name(result.status),
           result.optimal ? "yes" : "no", g.pushes, g.walking);
    csv(sequence->str);
    printf(",%" G_GUINT64_FORMAT ",%" G_GINT64_FORMAT ",%zu\n", result.nodes,
           result.elapsed_us, result.peak_memory);
    search_result_clear(&result);
    g_string_free(sequence, TRUE);
  }
  g_ptr_array_unref(pack);
  g_free(file);
  g_free(controller);
  return 0;
invalid:
  g_printerr("Invalid arguments; see --help.\n");
  g_free(file);
  g_free(controller);
  return 2;
}
