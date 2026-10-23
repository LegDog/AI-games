// SPDX-License-Identifier: GPL-3.0-or-later
#include "ollama-server.h"
#include "ollama.h"
static void validation(void) {
  Game g;
  game_init(&g);
  GameMove move;
  GError *error = NULL;
  g_assert_true(ollama_parse_move(
      &g, "{\"done\":true,\"response\":\"{\\\"move\\\":\\\"A-B\\\"}\"}", &move,
      &error));
  g_assert_cmpint(move.from, ==, 0);
  g_assert_cmpint(move.to, ==, 1);
  const char *bad[] = {
      "{\"done\":true,\"response\":\"{\\\"move\\\":\\\"A-C\\\"}\"}",
      "{\"done\":true,\"response\":\"{\\\"move\\\":\\\"A-D\\\"}\"}",
      "{\"done\":true,\"response\":\"{\\\"move\\\":\\\"pass\\\"}\"}",
      "{\"done\":true,\"response\":\"{\\\"move\\\":\\\"E-C\\\"}\"}",
      "{\"done\":false,\"response\":\"{\\\"move\\\":\\\"A-B\\\"}\"}",
      "{\"done\":true,\"response\":\"{\\\"move\\\":\\\"A-B\\\",\\\"extra\\\":1}"
      "\"}",
      "[]",
      "not json"};
  for (unsigned i = 0; i < G_N_ELEMENTS(bad); i++) {
    g_assert_false(ollama_parse_move(&g, bad[i], &move, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
  }
  OllamaAnalysis analysis;
  ollama_analyze(&g, &analysis);
  g_autofree char *prompt = ollama_build_prompt(&g, &analysis);
  g_assert_nonnull(strstr(prompt, "four dogs"));
  g_assert_nonnull(strstr(prompt, "A-B"));
  g_assert_null(strstr(prompt, "Senet"));
  GCancellable *cancel = g_cancellable_new();
  g_cancellable_cancel(cancel);
  OllamaResult result;
  g_assert_false(ollama_choose_with_options(&g, "mock", NULL, 100, cancel, NULL,
                                            &result, &error));
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
  g_clear_error(&error);
  g_object_unref(cancel);
}
typedef struct {
  Fixture *fixture;
  gboolean done, ok;
  Game game;
  OllamaOptions options;
  OllamaResult result;
  GError *error;
} Call;
static gpointer request_worker(gpointer data) {
  Call *c = data;
  c->ok = ollama_choose_with_options(&c->game, "test:1b", c->fixture->url, 2000,
                                     NULL, &c->options, &c->result, &c->error);
  g_atomic_int_set((gint *)&c->done, TRUE);
  g_main_context_wakeup(NULL);
  return NULL;
}
static void run(Call *call) {
  call->done = FALSE;
  GThread *thread = g_thread_new("coriovalli-http", request_worker, call);
  while (!g_atomic_int_get((gint *)&call->done))
    g_main_context_iteration(NULL, TRUE);
  g_thread_join(thread);
}
static void protocol(Fixture *fixture, gconstpointer data) {
  (void)data;
  if (!fixture->url)
    return;
  fixture->auto_move = TRUE;
  Call call = {.fixture = fixture};
  game_init(&call.game);
  for (int mode = 0; mode < 3; mode++) {
    call.options.thinking = mode == 1;
    call.options.harness = mode == 2;
    run(&call);
    g_assert_no_error(call.error);
    g_assert_true(call.ok);
    g_assert_true(game_move_legal(&call.game, call.result.move));
    g_assert_cmpint(call.result.move.from, ==, 0);
  }
  call.options = (OllamaOptions){0};
  fixture->auto_move = FALSE;
  fixture->body = "{\"done\":true,\"response\":\"{\\\"move\\\":\\\"A-C\\\"}\"}";
  run(&call);
  g_assert_false(call.ok);
  g_assert_error(call.error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error(&call.error);
  fixture->status = 503;
  fixture->body = "{\"error\":\"unavailable\"}";
  run(&call);
  g_assert_false(call.ok);
  g_assert_error(call.error, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_clear_error(&call.error);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_setenv("OLLAMA_LOG_FILE", "/dev/null", TRUE);
  g_setenv("OLLAMA_ERROR_LOG_FILE", "/dev/null", TRUE);
  g_test_add_func("/coriovalli/ollama", validation);
  g_test_add("/coriovalli/ollama-http", Fixture, NULL, setup, protocol,
             teardown);
  return g_test_run();
}
