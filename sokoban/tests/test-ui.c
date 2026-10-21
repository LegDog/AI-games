// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-fixture.h"
#include "../../tools/test-ui-panels.h"
#include "../../tools/test-ollama-controls.h"
#include "../../tools/test-ollama-integration.h"
#include <libsoup/soup.h>
static void human(void) {
  App *a = fixture();
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->delay), 0);
  begin_clicked(NULL, a);
  g_assert_true(a->playing);
  g_assert_true(start_step(a, RIGHT));
  g_assert_cmpuint(a->game.walking, ==, 1);
  g_assert_true(start_step(a, RIGHT));
  g_assert_true(game_solved(&a->game));
  g_assert_true(a->terminal);
  g_assert_true(a->recorded);
  undo_clicked(NULL, a);
  g_assert_false(a->terminal);
  g_assert_cmpuint(a->game.pushes, ==, 0);
  g_assert_cmpuint(a->game.walking, ==, 1);
  restart_clicked(NULL, a);
  g_assert_cmpuint(a->game.walking, ==, 0);
  g_assert_cmpuint(a->undo->len, ==, 0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->delay), 1);
  start_step(a, RIGHT);
  g_assert_cmpuint(a->game.walking, ==, 0);
  a->animation.started = g_get_monotonic_time() - 750000;
  pause_clicked(NULL, a);
  double t = animation_frame(&a->animation, G_MAXINT64).position;
  pump(30);
  g_assert_cmpfloat(animation_frame(&a->animation, G_MAXINT64).position, ==, t);
  pause_clicked(NULL, a);
  a->animation.started = g_get_monotonic_time() - 2000000;
  pump(30);
  g_assert_cmpuint(a->game.walking, ==, 1);
  dispose(a);
}
static void bots(void) {
  App *a = fixture();
  gtk_drop_down_set_selected(GTK_DROP_DOWN(a->controller), PLAYER_ASTAR);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->delay), 0);
  begin_clicked(NULL, a);
  pause_clicked(NULL, a);
  settle(a);
  g_assert_true(a->paused);
  g_assert_nonnull(a->plan.path);
  g_assert_cmpuint(a->game.pushes, ==, 0);
  pause_clicked(NULL, a);
  gint64 end = g_get_monotonic_time() + 3000000;
  while (!a->terminal && g_get_monotonic_time() < end)
    pump(2);
  g_assert_true(game_solved(&a->game));
  g_assert_true(a->plan.optimal);
  configure_clicked(NULL, a);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(a->level), 7);
  begin_clicked(NULL, a);
  configure_clicked(NULL, a);
  pump(80);
  g_assert_false(a->playing);
  g_assert_null(a->task);
  g_assert_null(a->plan.path);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(a->level), 0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->seconds), 1e-12);
  begin_clicked(NULL, a);
  settle(a);
  g_assert_true(a->failed);
  g_assert_false(a->terminal);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->seconds), 5);
  retry_clicked(NULL, a);
  settle(a);
  end = g_get_monotonic_time() + 3000000;
  while (!a->terminal && g_get_monotonic_time() < end)
    pump(2);
  g_assert_true(game_solved(&a->game));
  dispose(a);
}
static void imports(void) {
  App *a = fixture();
  Level *old = g_ptr_array_index(a->pack, 0);
  g_file_set_contents("invalid.xsb", "bad", -1, NULL);
  g_assert_false(import_path(a, "invalid.xsb"));
  g_assert_true(old == g_ptr_array_index(a->pack, 0));
  g_file_set_contents("good.xsb", "; Custom\n######\n#@ $.#\n######", -1, NULL);
  g_assert_true(import_path(a, "good.xsb"));
  g_assert_cmpuint(a->pack->len, ==, 1);
  g_assert_cmpstr(a->game.level->name, ==, "Custom");
  gtk_drop_down_set_selected(GTK_DROP_DOWN(a->language), I18N_PORTUGUESE);
  g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(a->undo_button)), ==,
                  "Desfazer");
  gtk_drop_down_set_selected(GTK_DROP_DOWN(a->language), I18N_ENGLISH);
  dispose(a);
  g_remove("invalid.xsb");
  g_remove("good.xsb");
}
typedef struct {
  SoupServerMessage *pending;
  gboolean hold, bad;
} Mock;
static void mock_serve(SoupServer *server, SoupServerMessage *message,
                       const char *path, GHashTable *query, gpointer data) {
  (void)server;
  (void)query;
  Mock *mock = data;
  const char *body =
      !strcmp(path, "/api/tags") ? "{\"models\":[{\"name\":\"mock\"}]}"
      : mock->bad                ? "{\"done\":true,\"response\":\"bad\"}"
                  : "{\"done\":true,\"response\":\"{\\\"x\\\":4,\\\"y\\\":2,"
                    "\\\"direction\\\":\\\"right\\\"}\"}";
  soup_server_message_set_status(message, 200, NULL);
  soup_server_message_set_response(message, "application/json",
                                   SOUP_MEMORY_COPY, body, strlen(body));
  if (mock->hold && !strcmp(path, "/api/generate")) {
    mock->pending = g_object_ref(message);
    soup_server_message_pause(message);
  }
}
static void ollama_lifecycle(void) {
  Mock mock = {0};
  SoupServer *server = soup_server_new(NULL, NULL);
  soup_server_add_handler(server, NULL, mock_serve, &mock, NULL);
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
  gtk_drop_down_set_selected(GTK_DROP_DOWN(a->controller), PLAYER_OLLAMA);
  gtk_editable_set_text(GTK_EDITABLE(a->url), url);
  refresh_clicked(NULL, a);
  settle(a);
  g_assert_nonnull(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(a->model)));
  mock.hold = TRUE;
  begin_clicked(NULL, a);
  gint64 end = g_get_monotonic_time() + 2000000;
  while (!mock.pending && g_get_monotonic_time() < end)
    pump(2);
  g_assert_nonnull(mock.pending);
  configure_clicked(NULL, a);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(a->controller), PLAYER_HUMAN);
  begin_clicked(NULL, a);
  soup_server_message_unpause(mock.pending);
  g_clear_object(&mock.pending);
  pump(100);
  g_assert_false(a->failed);
  g_assert_cmpuint(a->game.pushes, ==, 0);
  g_assert_cmpuint(a->game.walking, ==, 0);
  configure_clicked(NULL, a);
  mock.hold = FALSE;
  mock.bad = TRUE;
  gtk_drop_down_set_selected(GTK_DROP_DOWN(a->controller), PLAYER_OLLAMA);
  begin_clicked(NULL, a);
  settle(a);
  g_assert_true(a->failed);
  g_assert_cmpuint(a->game.pushes, ==, 0);
  ui_assert_ollama_transcript(a->board);
  dispose(a);
  soup_server_disconnect(server);
  g_object_unref(server);
}
static void save_retry(void) {
  App *a = fixture();
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->delay), 0);
  begin_clicked(NULL, a);
  g_remove(GAME_HISTORY_FILE);
  g_assert_cmpint(g_mkdir(GAME_HISTORY_FILE, 0700), ==, 0);
  start_step(a, RIGHT);
  start_step(a, RIGHT);
  g_assert_true(a->terminal);
  g_assert_false(a->recorded);
  g_assert_true(gtk_widget_get_visible(a->retry));
  g_rmdir(GAME_HISTORY_FILE);
  retry_clicked(NULL, a);
  g_assert_true(a->recorded);
  dispose(a);
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
  g_autofree char *directory = g_dir_make_tmp("sokoban-ui-XXXXXX", NULL);
  g_autofree char *old = g_get_current_dir();
  g_chdir(directory);
  g_test_add_func("/ui/fixed-text-panels", stable_text_panels);
  g_test_add_func("/ui/human-animation-undo", human);
  g_test_add_func("/ui/bots-pause-cancel-retry", bots);
  g_test_add_func("/ui/import-language", imports);
  g_test_add_func("/ui/ollama-stale-invalid", ollama_lifecycle);
  g_test_add_func("/ui/save-retry", save_retry);
  int result = g_test_run();
  g_remove(GAME_HISTORY_FILE);
  g_chdir(old);
  g_rmdir(directory);
  g_clear_object(&test_application);
  return result;
}
