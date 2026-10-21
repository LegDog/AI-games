// SPDX-License-Identifier: GPL-3.0-or-later
/* Opt-in model evaluation. No evaluation scores or expected answers enter
 * prompts. */
#include "harness-fixture.h"
#include "ollama.h"
#include <math.h>
#include <stdio.h>
static void put_string(JsonObject *row, const char *key, const char *value) {
  json_object_set_string_member(row, key, value ? value : "");
}
static void print_row(JsonObject *row) {
  JsonNode *node = json_node_new(JSON_NODE_OBJECT);
  json_node_set_object(node, row);
  g_autofree char *text = json_to_string(node, FALSE);
  puts(text);
  fflush(stdout);
  json_node_free(node);
}
int main(int argc, char **argv) {
  char *model = NULL, *url = NULL, *revision = NULL;
  gboolean thinking = FALSE, list = FALSE;
  gint seed = 42, timeout = 60000, fixture = -1;
  double temperature = 0;
  GOptionEntry args[] = {
      {"model", 0, 0, G_OPTION_ARG_STRING, &model,
       "Installed Ollama model (required unless --list)", "NAME"},
      {"url", 0, 0, G_OPTION_ARG_STRING, &url,
       "Ollama service URL (otherwise OLLAMA_URL/default)", "URL"},
      {"thinking", 0, 0, G_OPTION_ARG_NONE, &thinking,
       "Request supported thinking in both modes", NULL},
      {"seed", 0, 0, G_OPTION_ARG_INT, &seed, "Ollama generation seed", "N"},
      {"temperature", 0, 0, G_OPTION_ARG_DOUBLE, &temperature,
       "Shared generation temperature", "N"},
      {"timeout-ms", 0, 0, G_OPTION_ARG_INT, &timeout,
       "Total deadline for each mode's decision", "MS"},
      {"fixture", 0, 0, G_OPTION_ARG_INT, &fixture,
       "Only fixture 0, 1, or 2; default all", "N"},
      {"revision", 0, 0, G_OPTION_ARG_STRING, &revision,
       "Code revision to record", "SHA"},
      {"list", 0, 0, G_OPTION_ARG_NONE, &list,
       "List fixtures without making requests", NULL},
      {NULL}};
  g_autoptr(GOptionContext) context = g_option_context_new(
      "— compare direct and Propose/Review Ollama decisions; JSONL on stdout");
  g_option_context_add_main_entries(context, args, NULL);
  GError *error = NULL;
  gboolean parsed = g_option_context_parse(context, &argc, &argv, &error);
  if (!parsed || argc != 1 || (!list && (!model || !*model)) || timeout < 1 ||
      fixture < -1 || fixture > 2 || seed < 0 || !isfinite(temperature) ||
      temperature < 0 || temperature > 2) {
    g_printerr("%s\n",
               error ? error->message
                     : "Invalid arguments; use --help and specify --model.");
    g_clear_error(&error);
    g_free(model);
    g_free(url);
    g_free(revision);
    return 2;
  }
  g_autoptr(JsonObject) generation = json_object_new();
  json_object_set_int_member(generation, "seed", seed);
  json_object_set_double_member(generation, "temperature", temperature);
  ollama_run_set_generation_options(generation);
  const char *address = url ? url : g_getenv("OLLAMA_URL");
  unsigned failed = 0;
  for (unsigned index = 0; index < 3; index++) {
    if (fixture >= 0 && index != (unsigned)fixture)
      continue;
    Game game;
    harness_fixture_init(&game, index);
    if (list) {
      g_autoptr(JsonObject) row = json_object_new();
      put_string(row, "game", GAME_ID);
      json_object_set_int_member(row, "fixture", index);
      print_row(row);
      harness_fixture_clear(&game);
      continue;
    }
    for (unsigned mode = 0; mode < 2; mode++) {
      OllamaOptions options = {.thinking = thinking, .harness = mode != 0};
      OllamaResult result;
      g_autoptr(GCancellable) cancel = g_cancellable_new();
      gboolean ok = ollama_choose_with_options(
          &game, model, address, timeout, cancel, &options, &result, &error);
      gboolean legal = ok && harness_fixture_valid(&game, &result);
      g_autofree char *choice =
          legal ? harness_fixture_choice(&game, &result) : NULL;
      if (!legal)
        failed++;
      g_autoptr(JsonObject) row = json_object_new();
      put_string(row, "game", GAME_ID);
      json_object_set_int_member(row, "fixture", index);
      put_string(row, "mode", mode ? "propose-review" : "direct");
      put_string(row, "model", model);
      put_string(row, "url", address ? address : OLLAMA_DEFAULT_URL);
      put_string(row, "revision", revision ? revision : "unknown");
      json_object_set_boolean_member(row, "thinking", thinking);
      json_object_set_int_member(row, "seed", seed);
      json_object_set_double_member(row, "temperature", temperature);
      json_object_set_int_member(row, "timeout_ms", timeout);
      json_object_set_boolean_member(row, "ok", ok);
      json_object_set_boolean_member(row, "legal", legal);
      json_object_set_int_member(row, "elapsed_us", result.elapsed_us);
      json_object_set_int_member(row, "generations", result.run.generations);
      json_object_set_int_member(row, "completed_generations",
                                 result.run.completed_generations);
      json_object_set_int_member(row, "prompt_tokens",
                                 result.run.prompt_tokens);
      json_object_set_int_member(row, "output_tokens",
                                 result.run.output_tokens);
      json_object_set_boolean_member(row, "token_counts_available",
                                     result.run.completed_generations > 0 &&
                                         result.run.token_reports ==
                                             result.run.completed_generations);
      g_autoptr(GString) decision = g_string_new(NULL);
      if (ok)
        ollama_record(decision, model, &result);
      put_string(row, "details", decision->str);
      put_string(row, "choice", choice);
      int metric = legal ? harness_fixture_metric(&game, &result) : -1;
      if (metric >= 0)
        json_object_set_boolean_member(row, "immediate_success", metric != 0);
      else
        json_object_set_null_member(row, "immediate_success");
      put_string(row, "error", error ? error->message : NULL);
      print_row(row);
      g_clear_error(&error);
    }
    harness_fixture_clear(&game);
  }
  ollama_run_set_generation_options(NULL);
  g_free(model);
  g_free(url);
  g_free(revision);
  return failed ? 1 : 0;
}
