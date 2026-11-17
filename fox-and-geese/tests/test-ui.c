// SPDX-License-Identifier: GPL-3.0-or-later
#define FOX_GEESE_UI_TEST
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
  double x, y;
  coordinates(p, &x, &y);
  double w = gtk_widget_get_width(app.board),
         h = gtk_widget_get_height(app.board);
  double scale = MIN(w / 500., h / 400.);
  clicked(NULL, 1, x * scale + (w - 500 * scale) / 2,
          y * scale + (h - 400 * scale) / 2, NULL);
}
static void lifecycle(void) {
  g_autoptr(AdwApplication) application = adw_application_new(
      "io.github.aigames.FoxAndGeeseTest", G_APPLICATION_NON_UNIQUE);
  g_assert_true(g_application_register(G_APPLICATION(application), NULL, NULL));
  activate(GTK_APPLICATION(application), NULL);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.kind[1]), 0);
  stop();
  pump(200);
  click_point(16);
  click_point(23);
  g_assert_true(app.animating);
  Game saved = app.game;
  click_point(24);
  g_assert_cmpmem(&saved, sizeof saved, &app.game, sizeof app.game);
  pump(350);
  stop();
  g_assert_false(app.animating);
  ui_assert_stable_text_panels(app.board);
  for (unsigned language = 1; language <= 4; language++) {
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app.language), language);
    g_assert_cmpmem(&saved, sizeof saved, &app.game, sizeof app.game);
  }
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.variant), 1);
  stop();
  g_assert_cmpint(app.game.variant, ==, GAME_SEVENTEEN);
  g_assert_cmpuint(game_count(&app.game, 2), ==, 17);
  gtk_window_destroy(app.window);
  g_string_free(app.history, TRUE);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  adw_init();
  g_test_add_func("/fox-geese/ui", lifecycle);
  return g_test_run();
}
