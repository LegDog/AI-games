// SPDX-License-Identifier: GPL-3.0-or-later
#include "ollama.h"
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
static void protocol(void) {
  Game g;
  game_init(&g, 7.5);
  GError *error = NULL;
  OllamaOptions options = {0};
  OllamaAnalysis analysis;
  g_assert_true(ollama_analyze(&g, &options, NULL, &analysis, &error));
  g_assert_cmpuint(analysis.allowed.count, ==, 82);
  g_autofree char *prompt = ollama_build_prompt(&g, &analysis);
  g_assert_nonnull(strstr(prompt, "positional") ? strstr(prompt, "positional")
                                                : strstr(prompt, "Positional"));
  g_assert_nonnull(strstr(prompt, "7.5"));
  g_assert_nonnull(strstr(prompt, "pass"));
  int move;
  g_assert_true(ollama_parse_move(
      &g, "{\"done\":true,\"response\":\"{\\\"move\\\":\\\"A1\\\"}\"}", &move,
      &error));
  g_assert_cmpint(move, ==, 0);
  g_assert_true(ollama_parse_move(
      &g, "{\"done\":true,\"response\":\"{\\\"move\\\":\\\"pass\\\"}\"}", &move,
      &error));
  g_assert_cmpint(move, ==, GO_PASS);
  g_assert_true(game_play(&g, 0));
  const char *bad[] = {
      "[]",
      "{}",
      "{\"done\":false,\"response\":\"{\\\"move\\\":\\\"D4\\\"}\"}",
      "{\"done\":true,\"response\":\"{\\\"move\\\":\\\"A1\\\"}\"}",
      "{\"done\":true,\"response\":\"{\\\"move\\\":\\\"I9\\\"}\"}",
      "{\"done\":true,\"response\":\"{\\\"move\\\":\\\"pass\\\",\\\"extra\\\":"
      "1}\"}"};
  for (unsigned i = 0; i < G_N_ELEMENTS(bad); i++) {
    g_assert_false(ollama_parse_move(&g, bad[i], &move, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
  }
  GCancellable *cancel = g_cancellable_new();
  g_cancellable_cancel(cancel);
  OllamaResult result;
  g_assert_false(ollama_choose_with_options(&g, "mock", NULL, 100, cancel,
                                            &options, &result, &error));
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error(&error);
  g_object_unref(cancel);
  game_clear(&g);
}
typedef struct {
  SoupServer *server;
  GMainLoop *loop;
  gboolean native;
  unsigned generations;
} Mock;
static void serve(SoupServer *server, SoupServerMessage *message,
                  const char *path, GHashTable *query, gpointer data) {
  (void)server;
  (void)query;
  Mock *m = data;
  const char *body = "{\"models\":[{\"name\":\"mock\"}]}";
  if (!strcmp(path, "/api/tags"))
    g_usleep(20000);
  if (!strcmp(path, "/api/show"))
    body = "{\"thinking\":{\"values\":[true,false]}}";
  if (!strcmp(path, "/api/generate")) {
    m->generations++;
    SoupMessageBody *sent = soup_server_message_get_request_body(message);
    JsonParser *parser = json_parser_new();
    g_assert_true(
        json_parser_load_from_data(parser, sent->data, sent->length, NULL));
    JsonObject *object = json_node_get_object(json_parser_get_root(parser));
    g_assert_false(json_object_get_boolean_member(object, "stream"));
    JsonObject *format = json_object_get_object_member(object, "format");
    g_assert_nonnull(format);
    m->native = json_object_has_member(object, "think") &&
                json_object_get_boolean_member(object, "think");
    g_assert_nonnull(strstr(json_object_get_string_member(object, "prompt"),
                            "No automatic dead-stone removal"));
    g_object_unref(parser);
    body = "{\"done\":true,\"response\":\"{\\\"move\\\":\\\"D4\\\"}\","
           "\"thinking\":\"position reasoning\"}";
  }
  soup_server_message_set_status(message, 200, NULL);
  soup_server_message_set_response(message, "application/json",
                                   SOUP_MEMORY_COPY, body, strlen(body));
}
static gpointer server_thread(gpointer data) {
  Mock *m = data;
  g_main_context_push_thread_default(g_main_loop_get_context(m->loop));
  g_main_loop_run(m->loop);
  g_main_context_pop_thread_default(g_main_loop_get_context(m->loop));
  return NULL;
}
static void http(void) {
  Mock m = {0};
  GMainContext *context = g_main_context_new();
  g_main_context_push_thread_default(context);
  m.loop = g_main_loop_new(context, FALSE);
  m.server = soup_server_new(NULL, NULL);
  soup_server_add_handler(m.server, NULL, serve, &m, NULL);
  GError *error = NULL;
  if (!soup_server_listen_local(m.server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY,
                                &error)) {
    g_test_skip(error->message);
    g_clear_error(&error);
    g_object_unref(m.server);
    g_main_loop_unref(m.loop);
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    return;
  }
  GSList *uris = soup_server_get_uris(m.server);
  g_autofree char *url = g_uri_to_string(uris->data);
  g_slist_free_full(uris, (GDestroyNotify)g_uri_unref);
  g_main_context_pop_thread_default(context);
  GThread *thread = g_thread_new("mock", server_thread, &m);
  g_auto(GStrv) models = ollama_list_models(url, 2000, NULL, &error);
  g_assert_no_error(error);
  g_assert_cmpstr(models[0], ==, "mock");
  Game g;
  game_init(&g, 7.5);
  OllamaOptions options = {.thinking = TRUE};
  OllamaResult result;
  gboolean ok = ollama_choose_with_options(&g, "mock", url, 2000, NULL,
                                           &options, &result, &error);
  g_assert_no_error(error);
  g_assert_true(ok);
  g_assert_cmpint(result.move, ==, game_point("D4"));
  g_assert_cmpint(result.reasoning, ==, OLLAMA_REASONING_NATIVE);
  char **timed = ollama_list_models(url, 1, NULL, &error);
  g_assert_null(timed);
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
  g_clear_error(&error);
  g_main_loop_quit(m.loop);
  g_thread_join(thread);
  g_assert_true(m.native);
  g_assert_cmpuint(m.generations, ==, 1);
  soup_server_disconnect(m.server);
  g_object_unref(m.server);
  g_main_loop_unref(m.loop);
  g_main_context_unref(context);
  game_clear(&g);
}
int main(int argc, char **argv) {
  g_setenv("OLLAMA_LOG_FILE", "/dev/null", TRUE);
  g_setenv("OLLAMA_ERROR_LOG_FILE", "/dev/null", TRUE);
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/ollama/protocol", protocol);
  g_test_add_func("/ollama/http", http);
  return g_test_run();
}
