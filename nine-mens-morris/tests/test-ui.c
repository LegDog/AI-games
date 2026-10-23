// SPDX-License-Identifier: GPL-3.0-or-later
#define MORRIS_UI_TEST
#include "../../tools/test-ui-panels.h"
#include "../src/main.c"
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
      "io.github.aigames.MorrisTest", G_APPLICATION_NON_UNIQUE);
  g_assert_true(g_application_register(G_APPLICATION(application), NULL, NULL));
  activate(GTK_APPLICATION(application), NULL);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.kind[1]), 0);
  stop();
  pump(200);
  click_point(0);
  g_assert_true(app.animating);
  g_assert_cmpuint(game_cell(&app.game, 0), ==, 1);
  click_point(8);
  g_assert_cmpuint(app.game.actions, ==, 1);
  pump(350);
  stop();
  click_point(8);
  pump(350);
  stop();
  click_point(1);
  pump(350);
  stop();
  click_point(9);
  pump(350);
  stop();
  Game saved = app.game;
  click_point(2);
  g_assert_true(app.capturing);
  g_assert_false(app.animating);
  g_assert_cmpmem(&saved, sizeof saved, &app.game, sizeof app.game);
  click_point(20); /* Invalid capture cancels the draft without a turn. */
  g_assert_false(app.capturing);
  g_assert_cmpmem(&saved, sizeof saved, &app.game, sizeof app.game);
  click_point(2);
  g_assert_true(app.capturing);
  click_point(8);
  g_assert_true(app.animating);
  g_assert_false(app.capturing);
  g_assert_cmpuint(game_cell(&app.game, 8), ==, 0);
  g_assert_cmpuint(game_count(&app.game, 1), ==, 3);
  pump(350);
  stop();
  ui_assert_stable_text_panels(app.board);
  /* Live language changes preserve position and controller configuration. */
  saved = app.game;
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.language), 2);
  g_assert_nonnull(
      strstr(gtk_label_get_text(GTK_LABEL(app.status)), "Jogador"));
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.language), 1);
  g_assert_cmpmem(&saved, sizeof saved, &app.game, sizeof app.game);
  restart(NULL, NULL);
  stop();
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.kind[0]), 4);
  stop();
  advance(NULL);
  g_assert_true(app.busy);
  restart(NULL, NULL);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.kind[0]), 0);
  stop();
  pump(150);
  g_assert_cmpuint(app.game.actions, ==, 0);
  click_point(0);
  pump(350);
  stop();
  app.game = (Game){.pieces = {1u << 0 | 1u << 1 | 1u << 8,
                               1u << 4 | 1u << 12 | 1u << 20 | 1u << 22},
                    .turn = 1};
  app.selected = -1;
  update();
  click_point(8);
  click_point(18);
  g_assert_true(app.animating);
  g_assert_cmpuint(game_cell(&app.game, 18), ==, 1);
  pump(350);
  stop();
  pause_clicked(NULL, NULL);
  g_assert_true(app.paused);
  saved = app.game;
  click_point(4);
  g_assert_cmpmem(&saved, sizeof saved, &app.game, sizeof app.game);
  gtk_window_destroy(app.window);
  app.window = NULL;
  g_string_free(app.history, TRUE);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  adw_init();
  g_test_add_func("/morris/ui/lifecycle", lifecycle);
  return g_test_run();
}
