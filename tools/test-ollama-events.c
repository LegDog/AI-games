// SPDX-License-Identifier: GPL-3.0-or-later
#include "ollama-events.h"
#include <string.h>
typedef struct {
  GArray *kinds;
  GPtrArray *texts, *ids;
} Capture;
static void capture(OllamaMessageKind kind, const char *id, const char *text,
                    gpointer data) {
  Capture *c = data;
  g_array_append_val(c->kinds, kind);
  g_ptr_array_add(c->texts, g_strdup(text));
  g_ptr_array_add(c->ids, g_strdup(id));
}
static Capture make_capture(void) {
  return (Capture){g_array_new(FALSE, FALSE, sizeof(OllamaMessageKind)),
                   g_ptr_array_new_with_free_func(g_free),
                   g_ptr_array_new_with_free_func(g_free)};
}
static void clear_capture(Capture *c) {
  g_array_unref(c->kinds);
  g_ptr_array_unref(c->texts);
  g_ptr_array_unref(c->ids);
}
static void response(GCancellable *operation, const char *id, gboolean analysis,
                     const char *json) {
  GBytes *bytes = g_bytes_new(json, strlen(json));
  ollama_event_response(operation, id, "/api/generate", analysis, bytes);
  g_bytes_unref(bytes);
}
static void extraction(void) {
  Capture c = make_capture();
  g_autoptr(GCancellable) operation = g_cancellable_new();
  ollama_observe(operation, capture, &c, NULL);
  g_autoptr(JsonParser) parser = json_parser_new();
  g_assert_true(json_parser_load_from_data(
      parser,
      "{\"prompt\":\"board\\nchoose\",\"system\":\"rules\",\"stream\":false,"
      "\"format\":{\"type\":\"object\"}}",
      -1, NULL));
  JsonObject *body = json_node_get_object(json_parser_get_root(parser));
  g_autofree char *before = json_to_string(json_parser_get_root(parser), FALSE);
  ollama_event_request(operation, "one", "/api/show", body);
  g_assert_cmpuint(c.texts->len, ==, 0);
  ollama_event_request(operation, "one", "/api/generate", body);
  g_assert_cmpuint(c.texts->len, ==, 1);
  g_assert_nonnull(strstr(g_ptr_array_index(c.texts, 0), "board\nchoose"));
  g_autofree char *after = json_to_string(json_parser_get_root(parser), FALSE);
  g_assert_cmpstr(before, ==, after);
  response(operation, "one", FALSE,
           "{\"thinking\":\"Inspect the board.\",\"response\":\"<think>Inspect "
           "the board.</think>{\\\"move\\\":\\\"a1\\\"}\",\"done\":true}");
  g_assert_cmpuint(c.texts->len, ==, 3);
  g_assert_cmpint(g_array_index(c.kinds, OllamaMessageKind, 1), ==,
                  OLLAMA_THINKING);
  g_assert_cmpstr(g_ptr_array_index(c.texts, 1), ==, "Inspect the board.");
  g_assert_cmpint(g_array_index(c.kinds, OllamaMessageKind, 2), ==,
                  OLLAMA_RECEIVED);
  g_assert_nonnull(strstr(g_ptr_array_index(c.texts, 2), "\n"));
  g_assert_null(strstr(g_ptr_array_index(c.texts, 2), "<think>"));
  for (unsigned i = 0; i < 3; i++)
    g_assert_cmpstr(g_ptr_array_index(c.ids, i), ==, "one");
  response(operation, "two", TRUE,
           "{\"response\":\"Analysis stage prose\",\"done\":true}");
  g_assert_cmpint(g_array_index(c.kinds, OllamaMessageKind, 3), ==,
                  OLLAMA_THINKING);
  response(operation, "three", FALSE,
           "{\"response\":\"before<think>reasoning</think>after\"}");
  g_assert_cmpstr(g_ptr_array_index(c.texts, 5), ==, "beforeafter");
  response(operation, "four", FALSE, "broken JSON <b>literal</b>");
  g_assert_cmpstr(g_ptr_array_index(c.texts, 6), ==,
                  "broken JSON <b>literal</b>");
  response(operation, "five", FALSE,
           "{\"response\":\"<think>unfinished reasoning\",\"done\":false}");
  g_assert_cmpint(g_array_index(c.kinds, OllamaMessageKind, 7), ==,
                  OLLAMA_THINKING);
  GError *error =
      g_error_new_literal(G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "Timed out");
  ollama_event_error(operation, error);
  g_error_free(error);
  g_assert_cmpint(g_array_index(c.kinds, OllamaMessageKind, 8), ==,
                  OLLAMA_FAILURE);
  clear_capture(&c);
}
static void isolation(void) {
  Capture a = make_capture(), b = make_capture();
  g_autoptr(GCancellable) one = g_cancellable_new(), two = g_cancellable_new();
  ollama_observe(one, capture, &a, NULL);
  ollama_observe(two, capture, &b, NULL);
  response(one, "a", FALSE, "{\"response\":\"first\"}");
  response(two, "b", FALSE, "{\"response\":\"second\"}");
  response(NULL, "none", FALSE, "not observed");
  g_assert_cmpuint(a.texts->len, ==, 1);
  g_assert_cmpuint(b.texts->len, ==, 1);
  g_assert_cmpstr(g_ptr_array_index(a.texts, 0), ==, "first");
  g_assert_cmpstr(g_ptr_array_index(b.texts, 0), ==, "second");
  clear_capture(&a);
  clear_capture(&b);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/ollama-events/extraction-payload-preservation", extraction);
  g_test_add_func("/ollama-events/operation-isolation", isolation);
  return g_test_run();
}
