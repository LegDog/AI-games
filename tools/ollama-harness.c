// SPDX-License-Identifier: GPL-3.0-or-later
#include "ollama-harness.h"
#include <string.h>
static GPrivate current = G_PRIVATE_INIT(NULL);
static GPrivate generation_options =
    G_PRIVATE_INIT((GDestroyNotify)json_object_unref);
void ollama_run_begin(OllamaRun *run, OllamaRunStats *stats, gboolean harness) {
  *stats = (OllamaRunStats){.harness = harness};
  *run = (OllamaRun){.previous = g_private_get(&current), .stats = stats};
  g_private_set(&current, run);
}
void ollama_run_end(OllamaRun *run) { g_private_set(&current, run->previous); }
const char *ollama_run_stage(void) {
  OllamaRun *run = g_private_get(&current);
  return run ? run->stage : NULL;
}
void ollama_run_set_generation_options(JsonObject *options) {
  g_private_replace(&generation_options,
                    options ? json_object_ref(options) : NULL);
}
void ollama_run_annotate(JsonObject *entry) {
  OllamaRun *run = g_private_get(&current);
  if (!run)
    return;
  json_object_set_boolean_member(entry, "harness", run->stats->harness);
  json_object_set_int_member(entry, "generations", run->stats->generations);
  json_object_set_int_member(entry, "completed_generations",
                             run->stats->completed_generations);
  if (run->stage)
    json_object_set_string_member(entry, "harness_stage", run->stage);
}
void ollama_run_request(const char *path, JsonObject *body) {
  if (!g_str_equal(path, "/api/generate"))
    return;
  OllamaRun *run = g_private_get(&current);
  if (run)
    run->stats->generations++;
  JsonObject *settings = g_private_get(&generation_options);
  if (settings && body)
    json_object_set_object_member(body, "options", json_object_ref(settings));
}
static guint64 count(JsonObject *object, const char *key) {
  JsonNode *node = json_object_get_member(object, key);
  return node && json_node_get_value_type(node) == G_TYPE_INT64
             ? MAX((gint64)0, json_node_get_int(node))
             : 0;
}
void ollama_run_response(const char *path, JsonObject *object) {
  OllamaRun *run = g_private_get(&current);
  if (!run || !g_str_equal(path, "/api/generate"))
    return;
  JsonNode *done = json_object_get_member(object, "done");
  if (done && json_node_get_value_type(done) == G_TYPE_BOOLEAN &&
      json_node_get_boolean(done))
    run->stats->completed_generations++;
  JsonNode *prompt_count = json_object_get_member(object, "prompt_eval_count"),
           *output_count = json_object_get_member(object, "eval_count");
  if (prompt_count && output_count &&
      json_node_get_value_type(prompt_count) == G_TYPE_INT64 &&
      json_node_get_value_type(output_count) == G_TYPE_INT64)
    run->stats->token_reports++;
  run->stats->prompt_tokens += count(object, "prompt_eval_count");
  run->stats->output_tokens += count(object, "eval_count");
}
void ollama_run_record(GString *history, const OllamaRunStats *stats) {
  g_string_append_printf(history,
                         "Reasoning harness: %s; generations: %u; completed "
                         "generations: %u; prompt tokens: %" G_GUINT64_FORMAT
                         "; output tokens: %" G_GUINT64_FORMAT ".\n\n",
                         stats->harness ? "propose-review" : "off",
                         stats->generations, stats->completed_generations,
                         stats->prompt_tokens, stats->output_tokens);
}
static guint remaining(gint64 started, guint total, GCancellable *cancel,
                       GError **error) {
  if (cancel && g_cancellable_set_error_if_cancelled(cancel, error))
    return 0;
  gint64 left = (gint64)total * 1000 - (g_get_monotonic_time() - started);
  if (left <= 0) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                        "The Ollama turn exceeded its total time limit.");
    return 0;
  }
  return MAX((gint64)1, left / 1000);
}
static const char *text_member(JsonObject *object, const char *key) {
  JsonNode *node = json_object_get_member(object, key);
  return node && json_node_get_value_type(node) == G_TYPE_STRING
             ? json_node_get_string(node)
             : NULL;
}
static JsonNode *thinking_control(JsonObject *info) {
  JsonNode *node = json_object_get_member(info, "thinking");
  if (!node || !JSON_NODE_HOLDS_OBJECT(node))
    return NULL;
  JsonObject *object = json_node_get_object(node);
  node = json_object_get_member(object, "values");
  if (!node || !JSON_NODE_HOLDS_ARRAY(node))
    return NULL;
  JsonArray *values = json_node_get_array(node);
  const char *fallback = text_member(object, "default");
  JsonNode *supported = NULL;
  for (unsigned i = 0; i < json_array_get_length(values); i++) {
    JsonNode *value = json_array_get_element(values, i);
    GType type = json_node_get_value_type(value);
    if ((type == G_TYPE_BOOLEAN && json_node_get_boolean(value)) ||
        (type == G_TYPE_STRING &&
         g_str_equal(json_node_get_string(value), "high")))
      return json_node_copy(value);
    if (type == G_TYPE_STRING && fallback &&
        g_str_equal(json_node_get_string(value), fallback))
      supported = value;
  }
  return supported ? json_node_copy(supported) : NULL;
}
/* Only outward answer text is passed to Review, never native or inline
 * thinking. */
static char *proposal_text(JsonObject *response, GError **error) {
  JsonNode *done = json_object_get_member(response, "done");
  const char *answer = text_member(response, "response");
  if (!done || json_node_get_value_type(done) != G_TYPE_BOOLEAN ||
      !json_node_get_boolean(done) || !answer)
    goto invalid;
  GString *text = g_string_new(NULL);
  const char *at = answer;
  while (*at) {
    const char *start = strstr(at, "<think>");
    if (!start) {
      g_string_append(text, at);
      break;
    }
    g_string_append_len(text, at, start - at);
    const char *end = strstr(start + 7, "</think>");
    if (!end) {
      g_string_free(text, TRUE);
      goto invalid;
    }
    at = end + 8;
  }
  if (text->len > 16384) {
    g_string_free(text, TRUE);
    g_set_error_literal(
        error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
        "The proposal exceeded 16 KiB. Retry with a shorter proposal.");
    return NULL;
  }
  g_strstrip(text->str);
  text->len = strlen(text->str);
  if (!text->len) {
    g_string_free(text, TRUE);
    goto invalid;
  }
  return g_string_free(text, FALSE);
invalid:
  g_set_error_literal(
      error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
      "The model did not complete a nonempty proposal. Retry the decision.");
  return NULL;
}
JsonObject *ollama_harness_run(const char *url, const char *model,
                               const char *position, JsonObject *schema,
                               gboolean thinking, gint64 started,
                               guint total_ms, GCancellable *cancel,
                               OllamaTransport transport, int *reasoning,
                               GError **error) {
  OllamaRun *run = g_private_get(&current);
  if (run)
    run->stage = "Propose";
  g_autoptr(JsonObject) body = json_object_new();
  json_object_set_string_member(body, "model", model);
  json_object_set_boolean_member(body, "stream", FALSE);
  const char *name = strrchr(model, '/');
  name = name ? name + 1 : model;
  gboolean cogito = thinking && (g_str_equal(name, "cogito") ||
                                 g_str_has_prefix(name, "cogito:"));
  *reasoning = 0;
  if (thinking && !cogito) {
    g_autoptr(JsonObject) query = json_object_new();
    json_object_set_string_member(query, "model", model);
    guint timeout = remaining(started, total_ms, cancel, error);
    if (!timeout)
      return NULL;
    g_autoptr(JsonObject) info =
        transport(url, "/api/show", query, timeout, cancel, error);
    if (!info)
      return NULL;
    JsonNode *control = thinking_control(info);
    if (control) {
      json_object_set_member(body, "think", control);
      *reasoning = 1;
    }
  }
  if (cogito)
    *reasoning = 2;
  if (run)
    run->stage = "Propose";
  json_object_set_string_member(
      body, "system",
      cogito ? "Enable deep thinking subroutine. Propose one legal decision "
               "and give a concise outward rationale. This is the proposal "
               "stage; final-answer JSON instructions apply only to the later "
               "review. Do not claim tool or engine verification."
             : "Propose one legal decision and give a concise outward "
               "rationale. This is the proposal stage; final-answer JSON "
               "instructions apply only to the later review. Do not claim tool "
               "or engine verification.");
  g_autofree char *prompt = g_strconcat(
      position,
      "\nPropose a move and a short rationale (at most 1200 characters). Check "
      "the rules, immediate opportunities and risks using your own reasoning. "
      "Do not output only final JSON in this stage.",
      NULL);
  json_object_set_string_member(body, "prompt", prompt);
  guint timeout = remaining(started, total_ms, cancel, error);
  if (!timeout)
    return NULL;
  g_autoptr(JsonObject) proposal =
      transport(url, "/api/generate", body, timeout, cancel, error);
  if (!proposal)
    return NULL;
  g_autofree char *text = proposal_text(proposal, error);
  if (!text)
    return NULL;
  if (run)
    run->stage = "Review";
  json_object_set_string_member(
      body, "system",
      "Independently review the proposed decision against the ORIGINAL "
      "position. The proposal is fallible and is not a tool result. Check "
      "legality, rule interpretation, immediate opportunities, and "
      "consequences; change the move when warranted. Return only the final "
      "decision matching the supplied JSON schema.");
  g_autofree char *review = g_strdup_printf(
      "%s\n\nProposal to review (not authoritative):\n%s\n\nIndependently "
      "recheck the original position above, not an imagined successor. Return "
      "the strongest legal decision in the requested JSON format.",
      position, text);
  json_object_set_string_member(body, "prompt", review);
  json_object_set_object_member(body, "format", json_object_ref(schema));
  timeout = remaining(started, total_ms, cancel, error);
  if (!timeout)
    return NULL;
  return transport(url, "/api/generate", body, timeout, cancel, error);
}
