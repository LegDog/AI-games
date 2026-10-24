// SPDX-License-Identifier: GPL-3.0-or-later
#define CHECKERS_UI_TEST
#include "../../tools/test-ollama-integration.h"
#include "../../tools/test-ui-panels.h"
#include "../src/main.c"
#include "ollama-server.h"
static void pump(unsigned ms) {
  gint64 until = g_get_monotonic_time() + ms * 1000;
  while (g_get_monotonic_time() < until) {
    while (g_main_context_iteration(NULL, FALSE)) {
    }
    g_usleep(1000);
  }
}
static void click_point(int p) {
  double px, py;
  coordinates(p, &px, &py);
  double w = gtk_widget_get_width(app.board),
         h = gtk_widget_get_height(app.board);
  double scale = MIN(w / 500., h / 400.);
  clicked(NULL, 1, px * scale + (w - 500 * scale) / 2,
          py * scale + (h - 400 * scale) / 2, NULL);
}
static void lifecycle(void) {
  g_autoptr(AdwApplication) application = adw_application_new(
      "io.github.aigames.CheckersTest", G_APPLICATION_NON_UNIQUE);
  g_assert_true(g_application_register(G_APPLICATION(application), NULL, NULL));
  activate(GTK_APPLICATION(application), NULL);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.kind[1]), 0);
  stop();
  pump(200);
  g_assert_cmpint(app.game.variant, ==, GAME_ENGLISH);
  click_point(20); /* b6, Dark's opening */
  click_point(16); /* a5 */
  g_assert_true(app.animating);
  Game saved = app.game;
  click_point(9);
  click_point(13);
  g_assert_cmpmem(&saved, sizeof saved, &app.game, sizeof app.game);
  pump(350);
  stop();
  g_assert_false(app.animating);
  /* Brazilian chains retain blocking victims in the preview. */
  app.game = (Game){.pieces = {1u << 0, (1u << 4) | (1u << 13) | (1u << 31)},
                    .turn = 1,
                    .variant = GAME_BRAZILIAN};
  cancel_chain(NULL, NULL);
  saved = app.game;
  click_point(0);
  click_point(9);
  g_assert_true(app.capturing);
  g_assert_cmpuint(app.draft.length, ==, 1);
  g_assert_cmpmem(&saved, sizeof saved, &app.game, sizeof app.game);
  g_assert_cmpuint(game_cell(&app.preview, 4), ==, 2);
  g_assert_cmpuint(app.preview.captured, ==, 1u << 4);
  click_point(23);
  g_assert_true(app.capturing);
  cancel_chain(NULL, NULL);
  g_assert_cmpmem(&saved, sizeof saved, &app.game, sizeof app.game);
  click_point(0);
  click_point(9);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(app.depth[0]), 3);
  stop();
  g_assert_false(app.capturing);
  g_assert_cmpmem(&saved, sizeof saved, &app.game, sizeof app.game);
  click_point(0);
  click_point(9);
  click_point(18);
  g_assert_true(app.animating);
  g_assert_false(app.capturing);
  g_assert_cmpuint(app.game.actions, ==, 1);
  g_assert_cmpuint(game_count(&app.game, 2), ==, 1);
  pump(340);
  stop();
  g_assert_true(app.animating);
  g_assert_cmpuint(app.animation_step, ==, 1);
  g_assert_cmpuint(game_cell(&app.previous, 9), ==, 1);
  g_assert_cmpuint(game_cell(&app.previous, 4), ==, 2);
  pump(340);
  stop();
  g_assert_false(app.animating);
  g_assert_cmpuint(app.previous.captured, ==, 0);
  /* Promotion appears only when the committed turn finishes. */
  app.game = (Game){.pieces = {1u << 20, (1u << 25) | (1u << 31)}, .turn = 1};
  cancel_chain(NULL, NULL);
  click_point(20);
  click_point(29);
  g_assert_true(app.animating);
  g_assert_true(game_is_king(&app.game, 29));
  pump(350);
  stop();
  g_assert_false(app.animating);
  ui_assert_stable_text_panels(app.board);
  saved = app.game;
  for (unsigned language = 1; language <= 4; language++) {
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app.language), language);
    g_assert_cmpmem(&saved, sizeof saved, &app.game, sizeof app.game);
  }
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.language), 2);
  g_assert_nonnull(
      strstr(gtk_label_get_text(GTK_LABEL(app.status)), "Jogador"));
  g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(app.cancel_draft)), ==,
                  "Cancelar sequência");
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.language), 1);
  g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(app.cancel_draft)), ==,
                  "Cancel chain");
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.variant), 1);
  stop();
  g_assert_cmpint(app.game.variant, ==, GAME_BRAZILIAN);
  g_assert_cmpuint(app.game.actions, ==, 0);
  g_assert_cmpuint(game_count(&app.game, 1), ==, 12);
  g_assert_nonnull(strstr(app.history->str, "Variant: brazilian"));
  click_point(9);
  click_point(13);
  g_assert_true(app.animating);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.variant), 0);
  stop();
  g_assert_false(app.animating);
  g_assert_cmpuint(app.tick, ==, 0);
  g_assert_cmpuint(app.game.actions, ==, 0);
  /* Superseded search must not touch a restarted board. */
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.kind[1]), 4);
  stop();
  advance(NULL);
  g_assert_true(app.busy);
  restart(NULL, NULL);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.kind[1]), 0);
  stop();
  pump(150);
  g_assert_cmpuint(app.game.actions, ==, 0);
  pause_clicked(NULL, NULL);
  g_assert_true(app.paused);
  saved = app.game;
  click_point(9);
  click_point(13);
  g_assert_cmpmem(&saved, sizeof saved, &app.game, sizeof app.game);
  pause_clicked(NULL, NULL);
  stop();
  gtk_window_destroy(app.window);
  app.window = NULL;
  g_string_free(app.history, TRUE);
}
/* Exercise the real worker + transcript and reject a response after reset. */
static void http_lifecycle(Fixture *fixture, gconstpointer data) {
  (void)data;
  if (!fixture->url)
    return;
  g_autoptr(AdwApplication) application = adw_application_new(
      "io.github.aigames.CheckersHTTPTest", G_APPLICATION_NON_UNIQUE);
  g_assert_true(g_application_register(G_APPLICATION(application), NULL, NULL));
  app = (App){0};
  activate(GTK_APPLICATION(application), NULL);
  g_setenv("OLLAMA_URL", fixture->url, TRUE);
  fixture->auto_move = TRUE;
  gtk_editable_set_text(GTK_EDITABLE(app.model[1]), "test:1b");
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.kind[1]), 5);
  stop();
  advance(NULL);
  gint64 until = g_get_monotonic_time() + 3000000;
  while (app.busy && g_get_monotonic_time() < until)
    pump(10);
  g_assert_false(app.busy);
  g_assert_cmpuint(app.game.actions, ==, 1);
  pump(350);
  stop();
  ui_assert_ollama_transcript(app.board);
  restart(NULL, NULL);
  stop();
  fixture->delayed = TRUE;
  advance(NULL);
  until = g_get_monotonic_time() + 2000000;
  while (!fixture->paused->len && g_get_monotonic_time() < until)
    pump(10);
  g_assert_cmpuint(fixture->paused->len, >, 0);
  restart(NULL, NULL);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.kind[1]), 0);
  stop();
  for (unsigned i = 0; i < fixture->paused->len; i++)
    soup_server_message_unpause(g_ptr_array_index(fixture->paused, i));
  g_ptr_array_set_size(fixture->paused, 0);
  pump(150);
  g_assert_cmpuint(app.game.actions, ==, 0);
  g_unsetenv("OLLAMA_URL");
  gtk_window_destroy(app.window);
  app.window = NULL;
  g_string_free(app.history, TRUE);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_setenv("OLLAMA_LOG_FILE", "/dev/null", TRUE);
  g_setenv("OLLAMA_ERROR_LOG_FILE", "/dev/null", TRUE);
  g_autofree char *config = g_dir_make_tmp("checkers-ui-config-XXXXXX", NULL);
  g_setenv("XDG_CONFIG_HOME", config, TRUE);
  adw_init();
  g_test_add_func("/checkers/ui/lifecycle", lifecycle);
  g_test_add("/checkers/ui/http", Fixture, NULL, setup, http_lifecycle,
             teardown);
  return g_test_run();
}
