// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-fixture.h"
#include "../../tools/test-ui-panels.h"
#include "../../tools/test-ollama-controls.h"
#include "../../tools/test-ollama-integration.h"
#include <libsoup/soup.h>
static void human(void) {
  App *a = fixture();
  humans(a);
  g_assert_true(start_move(a, game_point("D4")));
  g_assert_cmpuint(a->game.history->len, ==, 2);
  g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(a->moves)), "D4"));
  g_assert_true(start_move(a, GO_PASS));
  g_assert_cmpuint(a->game.state.passes, ==, 1);
  undo_clicked(NULL, a);
  g_assert_true(a->paused);
  g_assert_cmpuint(a->game.state.passes, ==, 0);
  g_assert_cmpuint(a->game.history->len, ==, 2);
  pause_clicked(NULL, a);
  g_assert_true(start_move(a, GO_PASS));
  g_assert_true(start_move(a, GO_PASS));
  g_assert_true(game_finished(&a->game));
  g_assert_true(a->recorded);
  g_assert_nonnull(
      strstr(gtk_label_get_text(GTK_LABEL(a->status)), "Black wins"));
  restart_clicked(NULL, a);
  g_assert_cmpuint(a->game.history->len, ==, 1);
  g_assert_false(game_finished(&a->game));
  dispose(a);
}
static void captures_animation(void) {
  App *a = fixture();
  humans(a);
  place_fixture(a, WHITE, "A1");
  place_fixture(a, BLACK, "B1");
  if (a->game.state.turn != BLACK)
    game_play(&a->game, GO_PASS);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->delay), 1);
  g_assert_true(start_move(a, game_point("A2")));
  g_assert_cmpint(game_at(&a->game.state.board, 0), ==, WHITE);
  g_assert_cmpuint(a->pending_state.captures[BLACK], ==, 1);
  a->animation.started = g_get_monotonic_time() - 750000;
  pause_clicked(NULL, a);
  double t = animation_frame(&a->animation, G_MAXINT64).position;
  pump(30);
  g_assert_cmpfloat(animation_frame(&a->animation, G_MAXINT64).position, ==, t);
  pause_clicked(NULL, a);
  a->animation.started = g_get_monotonic_time() - 2000000;
  pump(30);
  g_assert_cmpint(game_at(&a->game.state.board, 0), ==, -1);
  g_assert_cmpuint(a->game.state.captures[BLACK], ==, 1);
  g_assert_true(a->game.state.removed[0] & 1);
  dispose(a);
}
static void bots(void) {
  App *a = fixture();
  for (unsigned c = 0; c < 2; c++) {
    gtk_drop_down_set_selected(GTK_DROP_DOWN(a->controllers[c]), PLAYER_MCTS);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->iterations[c]), 1);
  }
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->delay), 0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->action_limit), 4);
  begin_clicked(NULL, a);
  pause_clicked(NULL, a);
  g_test_message("Waiting for paused first decision: iterations=%u, seconds=%g",
                 read_options(a, BLACK).iterations,
                 read_options(a, BLACK).seconds);
  settle(a);
  g_assert_true(a->ready);
  g_assert_cmpuint(a->game.history->len, ==, 1);
  pause_clicked(NULL, a);
  gint64 end = g_get_monotonic_time() + 5000000;
  while (!a->incomplete && !game_finished(&a->game) &&
         g_get_monotonic_time() < end)
    pump(2);
  g_assert_true(a->incomplete || game_finished(&a->game));
  g_assert_true(a->recorded);
  configure_clicked(NULL, a);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->iterations[0]), 1000000);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->seconds), 0);
  begin_clicked(NULL, a);
  configure_clicked(NULL, a);
  humans(a);
  pump(60);
  g_assert_cmpuint(a->game.history->len, ==, 1);
  g_assert_false(a->failed);
  configure_clicked(NULL, a);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(a->controllers[0]), PLAYER_MCTS);
  gtk_spin_button_set_digits(GTK_SPIN_BUTTON(a->seconds), 12);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->seconds), 1e-6);
  begin_clicked(NULL, a);
  g_test_message("Waiting for limited decision: iterations=%u, seconds=%g",
                 read_options(a, BLACK).iterations,
                 read_options(a, BLACK).seconds);
  settle(a);
  g_assert_true(a->failed);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->iterations[0]), 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->seconds), 5);
  retry_clicked(NULL, a);
  pause_clicked(NULL, a);
  g_test_message("Waiting for retried decision: iterations=%u, seconds=%g",
                 read_options(a, BLACK).iterations,
                 read_options(a, BLACK).seconds);
  settle(a);
  g_assert_false(a->failed);
  g_assert_true(a->ready);
  dispose(a);
}
static void language_save(void) {
  App *a = fixture();
  humans(a);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(a->language), I18N_PORTUGUESE);
  g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(a->undo)), ==, "Desfazer");
  g_assert_cmpuint(a->game.history->len, ==, 1);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(a->language), I18N_ENGLISH);
  g_remove(GAME_HISTORY_FILE);
  g_assert_cmpint(g_mkdir(GAME_HISTORY_FILE, 0700), ==, 0);
  start_move(a, GO_PASS);
  start_move(a, GO_PASS);
  g_assert_true(game_finished(&a->game));
  g_assert_false(a->recorded);
  g_rmdir(GAME_HISTORY_FILE);
  retry_clicked(NULL, a);
  g_assert_true(a->recorded);
  dispose(a);
}
typedef struct {
  SoupServerMessage *pending;
  gboolean hold, bad;
} Mock;
static void serve(SoupServer *server, SoupServerMessage *message,
                  const char *path, GHashTable *query, gpointer data) {
  (void)server;
  (void)query;
  Mock *m = data;
  const char *body =
      !strcmp(path, "/api/tags") ? "{\"models\":[{\"name\":\"mock\"}]}"
      : m->bad                   ? "{\"done\":true,\"response\":\"bad\"}"
               : "{\"done\":true,\"response\":\"{\\\"move\\\":\\\"D4\\\"}\"}";
  soup_server_message_set_status(message, 200, NULL);
  soup_server_message_set_response(message, "application/json",
                                   SOUP_MEMORY_COPY, body, strlen(body));
  if (m->hold && !strcmp(path, "/api/generate")) {
    m->pending = g_object_ref(message);
    soup_server_message_pause(message);
  }
}
static void ollama_lifecycle(void) {
  Mock m = {0};
  SoupServer *server = soup_server_new(NULL, NULL);
  soup_server_add_handler(server, NULL, serve, &m, NULL);
  GError *error = NULL;
  if (!soup_server_listen_local(server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY,
                                &error)) {
    g_test_skip(error->message);
    g_clear_error(&error);
    g_object_unref(server);
    return;
  }
  GSList *uris = soup_server_get_uris(server);
  g_autofree char *url = g_uri_to_string(uris->data);
  g_slist_free_full(uris, (GDestroyNotify)g_uri_unref);
  App *a = fixture();
  gtk_drop_down_set_selected(GTK_DROP_DOWN(a->controllers[0]), PLAYER_OLLAMA);
  gtk_editable_set_text(GTK_EDITABLE(a->url), url);
  refresh_clicked(NULL, a);
  settle(a);
  g_assert_nonnull(
      gtk_drop_down_get_selected_item(GTK_DROP_DOWN(a->models[0])));
  m.hold = TRUE;
  begin_clicked(NULL, a);
  gint64 end = g_get_monotonic_time() + 2000000;
  while (!m.pending && g_get_monotonic_time() < end)
    pump(2);
  g_assert_nonnull(m.pending);
  configure_clicked(NULL, a);
  humans(a);
  soup_server_message_unpause(m.pending);
  g_clear_object(&m.pending);
  pump(80);
  g_assert_cmpuint(a->game.history->len, ==, 1);
  g_assert_false(a->failed);
  configure_clicked(NULL, a);
  m.hold = FALSE;
  m.bad = TRUE;
  gtk_drop_down_set_selected(GTK_DROP_DOWN(a->controllers[0]), PLAYER_OLLAMA);
  begin_clicked(NULL, a);
  settle(a);
  g_assert_true(a->failed);
  g_assert_cmpuint(a->game.history->len, ==, 1);
  ui_assert_ollama_transcript(a->board);
  dispose(a);
  soup_server_disconnect(server);
  g_object_unref(server);
}
static void stable_text_panels(void)
{
    App *a=fixture();
    if(a->tick){g_source_remove(a->tick);a->tick=0;}
    ui_assert_ollama_controls(a->board);dispose(a);
}
int main(int argc, char **argv) {
  g_setenv("OLLAMA_LOG_FILE", "/dev/null", TRUE);
  g_setenv("OLLAMA_ERROR_LOG_FILE", "/dev/null", TRUE);
  g_test_init(&argc, &argv, NULL);
  gtk_init();
  adw_init();
  g_autofree char *directory = g_dir_make_tmp("go-ui-XXXXXX", NULL);
  g_autofree char *old = g_get_current_dir();
  g_chdir(directory);
  g_test_add_func("/ui/fixed-text-panels", stable_text_panels);
  g_test_add_func("/ui/human-pass-undo", human);
  g_test_add_func("/ui/capture-animation", captures_animation);
  g_test_add_func("/ui/bots-pause-cancel-retry", bots);
  g_test_add_func("/ui/language-save-retry", language_save);
  g_test_add_func("/ui/ollama-stale-invalid", ollama_lifecycle);
  int result = g_test_run();
  g_remove(GAME_HISTORY_FILE);
  g_chdir(old);
  g_rmdir(directory);
  g_clear_object(&test_application);
  return result;
}
