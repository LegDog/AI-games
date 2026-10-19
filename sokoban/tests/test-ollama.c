// SPDX-License-Identifier: GPL-3.0-or-later
#include "ollama.h"
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
static void protocol(void) {
  GPtrArray *pack = levels_builtin();
  Game g;
  game_init(&g, g_ptr_array_index(pack, 0));
  OllamaAnalysis a;
  OllamaOptions options = {0};
  GError *error = NULL;
  g_assert_true(ollama_analyze(&g, &options, NULL, &a, &error));
  g_assert_cmpuint(a.allowed.count, ==, 1);
  g_autofree char *prompt = ollama_build_prompt(&g, &a);
  g_assert_nonnull(strstr(prompt, "4,2 right"));
  g_assert_nonnull(strstr(prompt, "never pull"));
  int move;
  g_assert_true(
      ollama_parse_move(&g,
                        "{\"done\":true,\"response\":\"{\\\"x\\\":4,\\\"y\\\":"
                        "2,\\\"direction\\\":\\\"right\\\"}\"}",
                        &move, &error));
  g_assert_cmpint(move, ==, a.allowed.items[0]);
  const char *bad[] = {"{}", "[]", "{\"done\":false,\"response\":\"{}\"}",
                       "{\"done\":true,\"response\":\"{\\\"x\\\":4,\\\"y\\\":2,"
                       "\\\"direction\\\":\\\"left\\\"}\"}",
                       "{\"done\":true,\"response\":\"{\\\"x\\\":4.0,\\\"y\\\":"
                       "2,\\\"direction\\\":\\\"right\\\"}\"}"};
  for (unsigned i = 0; i < G_N_ELEMENTS(bad); i++) {
    g_assert_false(ollama_parse_move(&g, bad[i], &move, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
  }
  g_auto(GStrv) models = ollama_parse_models(
      "{\"models\":[{\"name\":\"z\"},{\"name\":\"a\"},{\"name\":\"z\"}]}",
      &error);
  g_assert_no_error(error);
  g_assert_cmpstr(models[0], ==, "a");
  g_assert_cmpstr(models[1], ==, "z");
  g_assert_null(models[2]);
  GCancellable *cancel = g_cancellable_new();
  g_cancellable_cancel(cancel);
  OllamaResult result;
  g_assert_false(ollama_choose_with_options(&g, "mock", NULL, 100, cancel,
                                            &options, &result, &error));
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error(&error);
  g_object_unref(cancel);
  g_ptr_array_unref(pack);
}
typedef struct {
  SoupServer *server;
  GMainLoop *loop;
  GThread *thread;
  char *url;
  unsigned generations;
  gboolean thinking;
} Fixture;
static void serve(SoupServer *server, SoupServerMessage *message,
                  const char *path, GHashTable *query, gpointer data) {
  (void)server;
  (void)query;
  Fixture *f = data;
  const char *body = "{\"models\":[{\"name\":\"mock\"}]}";
  if (!strcmp(path, "/api/show"))
    body = "{\"thinking\":{\"values\":[true,false]}}";
  if (!strcmp(path, "/api/generate")) {
    f->generations++;
    SoupMessageBody *sent = soup_server_message_get_request_body(message);
    JsonParser *parser = json_parser_new();
    g_assert_true(
        json_parser_load_from_data(parser, sent->data, sent->length, NULL));
    JsonObject *obj = json_node_get_object(json_parser_get_root(parser));
    g_assert_false(json_object_get_boolean_member(obj, "stream"));
    g_assert_nonnull(json_object_get_object_member(obj, "format"));
    g_assert_nonnull(
        strstr(json_object_get_string_member(obj, "prompt"), "4,2 right"));
    f->thinking = json_object_has_member(obj, "think") &&
                  json_object_get_boolean_member(obj, "think");
    g_object_unref(parser);
    body = "{\"done\":true,\"response\":\"{\\\"x\\\":4,\\\"y\\\":2,"
           "\\\"direction\\\":\\\"right\\\"}\",\"thinking\":\"box analysis\"}";
  }
  soup_server_message_set_status(message, 200, NULL);
  soup_server_message_set_response(message, "application/json",
                                   SOUP_MEMORY_COPY, body, strlen(body));
}
static gpointer server_thread(gpointer data) {
  Fixture *f = data;
  g_main_context_push_thread_default(g_main_loop_get_context(f->loop));
  g_main_loop_run(f->loop);
  g_main_context_pop_thread_default(g_main_loop_get_context(f->loop));
  return NULL;
}
static void http(void) {
  Fixture f = {0};
  GMainContext *context = g_main_context_new();
  g_main_context_push_thread_default(context);
  f.loop = g_main_loop_new(context, FALSE);
  f.server = soup_server_new(NULL, NULL);
  soup_server_add_handler(f.server, NULL, serve, &f, NULL);
  GError *error = NULL;
  if (!soup_server_listen_local(f.server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY,
                                &error)) {
    g_test_skip(error->message);
    g_clear_error(&error);
    g_object_unref(f.server);
    g_main_loop_unref(f.loop);
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    return;
  }
  GSList *uris = soup_server_get_uris(f.server);
  f.url = g_uri_to_string(uris->data);
  g_slist_free_full(uris, (GDestroyNotify)g_uri_unref);
  g_main_context_pop_thread_default(context);
  f.thread = g_thread_new("mock", server_thread, &f);
  g_auto(GStrv) models = ollama_list_models(f.url, 1000, NULL, &error);
  g_assert_no_error(error);
  g_assert_cmpstr(models[0], ==, "mock");
  GPtrArray *pack = levels_builtin();
  Game g;
  game_init(&g, g_ptr_array_index(pack, 0));
  OllamaOptions options = {.thinking = TRUE};
  OllamaResult result;
  g_assert_true(ollama_choose_with_options(&g, "mock", f.url, 1000, NULL,
                                           &options, &result, &error));
  g_assert_no_error(error);
  g_assert_true(f.thinking);
  g_assert_cmpuint(f.generations, ==, 1);
  g_assert_cmpint(result.reasoning, ==, OLLAMA_REASONING_NATIVE);
  g_main_loop_quit(f.loop);
  g_thread_join(f.thread);
  soup_server_disconnect(f.server);
  g_object_unref(f.server);
  g_main_loop_unref(f.loop);
  g_main_context_unref(context);
  g_free(f.url);
  g_ptr_array_unref(pack);
}
int main(int argc, char **argv) {
  g_setenv("OLLAMA_LOG_FILE", "/dev/null", TRUE);
  g_setenv("OLLAMA_ERROR_LOG_FILE", "/dev/null", TRUE);
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/ollama/protocol", protocol);
  g_test_add_func("/ollama/http", http);
  return g_test_run();
}
