// SPDX-License-Identifier: GPL-3.0-or-later
#include "ollama-harness.h"
#include <string.h>
typedef struct {
  GPtrArray *bodies;
  GArray *timeouts;
  GCancellable *cancel;
  gboolean cancel_proposal;
  unsigned delay;
} Mock;
static GPrivate active = G_PRIVATE_INIT(NULL);
static JsonObject *transport(const char *url, const char *path,
                             JsonObject *body, guint timeout,
                             GCancellable *cancel, GError **error) {
  (void)url;
  (void)cancel;
  (void)error;
  Mock *m = g_private_get(&active);
  JsonNode *node = json_node_new(JSON_NODE_OBJECT);
  json_node_set_object(node, body);
  g_autofree char *raw = json_to_string(node, FALSE);
  json_node_free(node);
  g_autoptr(JsonParser) parser = json_parser_new();
  g_assert_true(json_parser_load_from_data(parser, raw, -1, NULL));
  g_ptr_array_add(m->bodies, json_object_ref(json_node_get_object(
                                 json_parser_get_root(parser))));
  g_array_append_val(m->timeouts, timeout);
  ollama_run_request(path, body);
  if (m->delay)
    g_usleep(m->delay * 1000);
  JsonObject *response = json_object_new();
  if (g_str_equal(path, "/api/show")) {
    JsonObject *thinking = json_object_new();
    JsonArray *values = json_array_new();
    json_array_add_boolean_element(values, FALSE);
    json_array_add_boolean_element(values, TRUE);
    json_object_set_array_member(thinking, "values", values);
    json_object_set_object_member(response, "thinking", thinking);
  } else {
    gboolean final = json_object_has_member(body, "format");
    json_object_set_boolean_member(response, "done", TRUE);
    json_object_set_string_member(
        response, "response",
        final ? "{\"move\":1}"
              : "<think>private</think>A short outward proposal.");
    json_object_set_string_member(response, "thinking", "also private");
    if (!final && m->cancel_proposal)
      g_cancellable_cancel(m->cancel);
  }
  ollama_run_response(path, response);
  return response;
}
static void scopes_and_budgets(void) {
  Mock m = {.bodies = g_ptr_array_new_with_free_func(
                (GDestroyNotify)json_object_unref),
            .timeouts = g_array_new(FALSE, FALSE, sizeof(guint)),
            .cancel = g_cancellable_new()};
  g_private_set(&active, &m);
  g_autoptr(JsonObject) schema = json_object_new();
  json_object_set_string_member(schema, "type", "object");
  OllamaRunStats stats;
  g_auto(OllamaRun) run = {0};
  ollama_run_begin(&run, &stats, TRUE);
  int reasoning;
  g_autoptr(JsonObject) result = ollama_harness_run(
      NULL, "mock", "Original board", schema, TRUE, g_get_monotonic_time(),
      1000, m.cancel, transport, &reasoning, NULL);
  g_assert_nonnull(result);
  g_assert_cmpint(reasoning, ==, 1);
  g_assert_cmpuint(m.bodies->len, ==, 3);
  g_assert_cmpuint(stats.generations, ==, 2);
  JsonObject *review = g_ptr_array_index(m.bodies, 2);
  g_assert_nonnull(strstr(json_object_get_string_member(review, "prompt"),
                          "Original board"));
  g_assert_null(
      strstr(json_object_get_string_member(review, "prompt"), "private"));
  g_assert_true(json_object_get_boolean_member(review, "think"));
  /* A nested operation restores its parent without leaking stage state. */
  {
    OllamaRunStats nested;
    g_auto(OllamaRun) child = {0};
    ollama_run_begin(&child, &nested, FALSE);
    g_assert_null(ollama_run_stage());
  }
  g_assert_cmpstr(ollama_run_stage(), ==, "Review");
  g_ptr_array_set_size(m.bodies, 0);
  g_array_set_size(m.timeouts, 0);
  m.delay = 15;
  GError *error = NULL;
  g_clear_pointer(&result, json_object_unref);
  result = ollama_harness_run(NULL, "mock", "Original board", schema, TRUE,
                              g_get_monotonic_time(), 20, m.cancel, transport,
                              &reasoning, &error);
  g_assert_null(result);
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
  g_clear_error(&error);
  g_assert_cmpuint(m.bodies->len, >=, 1);
  g_assert_cmpuint(m.bodies->len, <=, 2);
  if (m.bodies->len == 2)
    g_assert_cmpuint(g_array_index(m.timeouts, guint, 1), <,
                     g_array_index(m.timeouts, guint, 0));
  m.delay = 0;
  m.cancel_proposal = TRUE;
  g_ptr_array_set_size(m.bodies, 0);
  result = ollama_harness_run(NULL, "mock", "Original board", schema, FALSE,
                              g_get_monotonic_time(), 1000, m.cancel, transport,
                              &reasoning, &error);
  g_assert_null(result);
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error(&error);
  g_assert_cmpuint(m.bodies->len, ==, 1);
  g_cancellable_reset(m.cancel);
  m.cancel_proposal = FALSE;
  g_ptr_array_set_size(m.bodies, 0);
  result = ollama_harness_run(NULL, "mock", "Original board", schema, FALSE,
                              g_get_monotonic_time() - 100000, 1, m.cancel,
                              transport, &reasoning, &error);
  g_assert_null(result);
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
  g_clear_error(&error);
  g_assert_cmpuint(m.bodies->len, ==, 0);
  g_object_unref(m.cancel);
  g_ptr_array_unref(m.bodies);
  g_array_unref(m.timeouts);
  g_private_set(&active, NULL);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/harness/scopes-native-shared-budget-cancellation",
                  scopes_and_budgets);
  return g_test_run();
}
