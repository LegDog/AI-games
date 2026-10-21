// SPDX-License-Identifier: GPL-3.0-or-later
#include "harness-fixture.h"
#include "ollama.h"
#include <libsoup/soup.h>
#include <string.h>
typedef struct {
  SoupServer *server;
  char *url;
  GPtrArray *requests;
  Game game;
  OllamaOptions options;
  OllamaResult result;
  const char *model;
  GCancellable *cancel;
  GError *error;
  gboolean ok;
  gint finished;
  guint timeout;
  unsigned proposal_kind;
  gboolean invalid_final, last;
  unsigned sent, received, thinking;
  gboolean cancel_after_proposal;
} Case;
static char *serialize(JsonObject *object) {
  JsonNode *n = json_node_new(JSON_NODE_OBJECT);
  json_node_set_object(n, object);
  char *text = json_to_string(n, FALSE);
  json_node_free(n);
  return text;
}
static void handle(SoupServer *server, SoupServerMessage *message,
                   const char *path, GHashTable *query, gpointer data) {
  (void)server;
  (void)query;
  Case *c = data;
  SoupMessageBody *raw = soup_server_message_get_request_body(message);
  g_autoptr(JsonParser) parser = json_parser_new();
  g_assert_true(
      json_parser_load_from_data(parser, raw->data, raw->length, NULL));
  JsonObject *body = json_node_get_object(json_parser_get_root(parser));
  if (g_str_equal(path, "/api/show")) {
    soup_server_message_set_status(message, 200, NULL);
    soup_server_message_set_response(
        message, "application/json", SOUP_MEMORY_STATIC,
        "{\"thinking\":{\"values\":[false,true],\"default\":false}}",
        sizeof("{\"thinking\":{\"values\":[false,true],\"default\":false}}") -
            1);
    return;
  }
  g_assert_cmpstr(path, ==, "/api/generate");
  g_assert_false(json_object_get_boolean_member(body, "stream"));
  g_ptr_array_add(c->requests, json_object_ref(body));
  g_autofree char *answer = NULL;
  if (!json_object_has_member(body, "format")) {
    if (c->proposal_kind == 1)
      answer = g_strdup("");
    else if (c->proposal_kind == 2)
      answer = g_strnfill(16385, 'x');
    else if (c->proposal_kind == 3)
      answer = g_strdup("<think>PRIVATE_INLINE</think>");
    else
      answer = g_strdup("<think>PRIVATE_INLINE</think>PROPOSAL: choose a "
                        "listed move after checking threats.");
  } else if (c->invalid_final)
    answer = g_strdup("{}");
  else {
    answer = harness_fixture_answer(&c->game, c->last);
    if (!answer) {
      JsonObject *schema = json_object_get_object_member(body, "format"),
                 *properties =
                     json_object_get_object_member(schema, "properties");
      g_autoptr(JsonObject) move = json_object_new();
      GList *members = json_object_get_members(properties);
      for (GList *p = members; p; p = p->next) {
        JsonObject *property =
            json_object_get_object_member(properties, p->data);
        JsonArray *values = json_object_get_array_member(property, "enum");
        unsigned i = c->last ? json_array_get_length(values) - 1 : 0;
        json_object_set_member(
            move, p->data, json_node_copy(json_array_get_element(values, i)));
      }
      g_list_free(members);
      answer = serialize(move);
    }
  }
  g_autoptr(JsonObject) response = json_object_new();
  json_object_set_boolean_member(response, "done", c->proposal_kind != 4);
  json_object_set_string_member(response, "response", answer);
  json_object_set_string_member(response, "thinking", "PRIVATE_NATIVE");
  json_object_set_int_member(response, "prompt_eval_count", 7);
  json_object_set_int_member(response, "eval_count", 5);
  g_autofree char *text = serialize(response);
  soup_server_message_set_status(message, 200, NULL);
  soup_server_message_set_response(message, "application/json",
                                   SOUP_MEMORY_COPY, text, strlen(text));
}
static void observe(OllamaMessageKind kind, const char *id, const char *text,
                    gpointer data) {
  (void)id;
  Case *c = data;
  if (kind == OLLAMA_SENT) {
    c->sent++;
    g_assert_true(g_str_has_prefix(text, "Propose\n") ||
                  g_str_has_prefix(text, "Review\n"));
  }
  if (kind == OLLAMA_THINKING)
    c->thinking++;
  if (kind == OLLAMA_RECEIVED) {
    c->received++;
    if (c->cancel_after_proposal && g_str_has_prefix(text, "Propose\n"))
      g_cancellable_cancel(c->cancel);
  }
}
static gpointer worker(gpointer data) {
  Case *c = data;
  c->ok =
      ollama_choose_with_options(&c->game, c->model ? c->model : "mock", c->url,
                                 c->timeout ? c->timeout : 3000, c->cancel,
                                 &c->options, &c->result, &c->error);
  g_atomic_int_set(&c->finished, TRUE);
  g_main_context_wakeup(NULL);
  return NULL;
}
static void run(Case *c) {
  c->finished = FALSE;
  GThread *thread = g_thread_new("harness-adapter", worker, c);
  while (!g_atomic_int_get(&c->finished))
    g_main_context_iteration(NULL, TRUE);
  g_thread_join(thread);
}
static void setup(Case *c, gconstpointer data) {
  (void)data;
  c->server = soup_server_new(NULL, NULL);
  soup_server_add_handler(c->server, NULL, handle, c, NULL);
  g_assert_true(soup_server_listen_local(
      c->server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY, &c->error));
  g_assert_no_error(c->error);
  GSList *uris = soup_server_get_uris(c->server);
  c->url = g_uri_to_string(uris->data);
  g_slist_free_full(uris, (GDestroyNotify)g_uri_unref);
  if (g_str_has_suffix(c->url, "/"))
    c->url[strlen(c->url) - 1] = 0;
  c->requests =
      g_ptr_array_new_with_free_func((GDestroyNotify)json_object_unref);
  c->cancel = g_cancellable_new();
  ollama_observe(c->cancel, observe, c, NULL);
  c->options.harness = TRUE;
}
static void teardown(Case *c, gconstpointer data) {
  (void)data;
  g_clear_error(&c->error);
  soup_server_disconnect(c->server);
  g_object_unref(c->server);
  g_object_unref(c->cancel);
  g_ptr_array_unref(c->requests);
  g_free(c->url);
}
static void protocol(Case *c, gconstpointer data) {
  (void)data;
  for (unsigned fixture = 0; fixture < 3; fixture++) {
    harness_fixture_init(&c->game, fixture);
    unsigned before = c->requests->len;
    c->options.thinking = fixture > 0;
    c->model = fixture == 2 ? "cogito:3b" : "mock";
    c->last = fixture == 2;
#ifdef HARNESS_ASSISTED
    c->options.assisted =
        TRUE; /* API must normalize contradictory caller input. */
#endif
    run(c);
    if (c->error)
      g_test_message("fixture %u: %s", fixture, c->error->message);
    g_assert_true(c->ok);
    g_assert_no_error(c->error);
    g_assert_true(harness_fixture_valid(&c->game, &c->result));
    g_assert_true(c->result.run.harness);
    unsigned count = c->requests->len - before;
    g_assert_cmpuint(count, >=, 2);
    g_assert_cmpuint(count % 2, ==, 0);
    g_assert_cmpuint(c->result.run.generations, ==, count);
    g_assert_cmpuint(c->result.run.completed_generations, ==, count);
    g_assert_cmpuint(c->result.run.prompt_tokens, ==, count * 7);
    g_assert_cmpuint(c->result.run.output_tokens, ==, count * 5);
    for (unsigned i = before; i < c->requests->len; i += 2) {
      JsonObject *proposal = g_ptr_array_index(c->requests, i),
                 *review = g_ptr_array_index(c->requests, i + 1);
      g_assert_false(json_object_has_member(proposal, "format"));
      g_assert_true(json_object_has_member(review, "format"));
      const char *p = json_object_get_string_member(proposal, "prompt"),
                 *r = json_object_get_string_member(review, "prompt");
      const char *suffix = strstr(p, "\nPropose a move");
      g_assert_nonnull(suffix);
      gsize prefix = (gsize)(suffix - p);
      g_assert_cmpmem(p, prefix, r, prefix);
      g_assert_nonnull(strstr(r, "PROPOSAL:"));
      g_assert_null(strstr(r, "PRIVATE_NATIVE"));
      g_assert_null(strstr(r, "PRIVATE_INLINE"));
      g_assert_null(strstr(p, "engine-verified"));
      if (fixture == 1)
        g_assert_true(json_object_get_boolean_member(proposal, "think"));
      if (fixture == 2)
        g_assert_nonnull(
            strstr(json_object_get_string_member(proposal, "system"),
                   "Enable deep thinking subroutine."));
    }
    (void)harness_fixture_metric(&c->game, &c->result);
    harness_fixture_clear(&c->game);
  }
  g_assert_cmpuint(c->sent, ==, c->requests->len);
  g_assert_cmpuint(c->received, ==, c->requests->len);
  g_assert_cmpuint(c->thinking, >, 0);
}
static void failures(Case *c, gconstpointer data) {
  (void)data;
  harness_fixture_init(&c->game, 0);
  for (unsigned kind = 1; kind <= 4; kind++) {
    c->proposal_kind = kind;
    unsigned before = c->requests->len;
    run(c);
    g_assert_false(c->ok);
    g_assert_nonnull(c->error);
    g_clear_error(&c->error);
    g_assert_cmpuint(c->requests->len, ==, before + 1);
  }
  c->proposal_kind = 0;
  c->invalid_final = TRUE;
  run(c);
  g_assert_false(c->ok);
  g_assert_nonnull(c->error);
  g_clear_error(&c->error);
  c->invalid_final = FALSE;
  c->cancel_after_proposal = TRUE;
  unsigned before = c->requests->len;
  run(c);
  g_assert_false(c->ok);
  g_assert_error(c->error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error(&c->error);
  g_assert_cmpuint(c->requests->len, ==, before + 1);
  harness_fixture_clear(&c->game);
}
int main(int argc, char **argv) {
  g_setenv("OLLAMA_LOG_FILE", "/dev/null", TRUE);
  g_setenv("OLLAMA_ERROR_LOG_FILE", "/dev/null", TRUE);
  g_test_init(&argc, &argv, NULL);
  g_test_add("/harness-game/protocol-fixtures-thinking-cogito", Case, NULL,
             setup, protocol, teardown);
  g_test_add("/harness-game/failures-no-fallback", Case, NULL, setup, failures,
             teardown);
  return g_test_run();
}
