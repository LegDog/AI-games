// SPDX-License-Identifier: GPL-3.0-or-later
#define CORIOVALLI_UI_TEST
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
  double width = gtk_widget_get_width(app.board),
         height = gtk_widget_get_height(app.board);
  double scale = MIN(width / 500., height / 400.);
  clicked(NULL, 1, px * scale + (width - 500 * scale) / 2,
          py * scale + (height - 400 * scale) / 2, NULL);
}
static void lifecycle(void) {
  AdwApplication *application = adw_application_new(
      "io.github.aigames.CoriovalliTest", G_APPLICATION_NON_UNIQUE);
  g_assert_true(g_application_register(G_APPLICATION(application), NULL, NULL));
  activate(GTK_APPLICATION(application), NULL);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.kind[1]), 0);
  stop();
  pump(200);
  click_point(0);
  g_assert_cmpint(app.selected, ==, 0);
  click_point(2);
  g_assert_cmpuint(app.game.actions, ==, 0);
  click_point(0);
  click_point(1);
  g_assert_true(app.animating);
  g_assert_cmpint(app.game.cells[1], ==, 1);
  g_assert_cmpuint(game_actor(&app.game), ==, 2);
  click_point(4);
  g_assert_cmpint(app.selected, ==, -1);
  pump(450);
  g_assert_false(app.animating);
  stop();
  ui_assert_stable_text_panels(app.board);
  app.game =
      (Game){.cells = {2, 1, 0, 0, 0, 1, 2, 1, 0, 1}, .turn = 1, .limit = 100};
  play((GameMove){7, 3});
  pump(450);
  stop();
  g_assert_true(app.game.round_over);
  next_clicked(NULL, NULL);
  g_assert_cmpuint(app.game.round, ==, 1);
  g_assert_cmpuint(game_actor(&app.game), ==, 2);
  g_assert_cmpint(app.game.cells[0], ==, 1);
  restart(NULL, NULL);
  stop();
  g_assert_cmpuint(app.game.round, ==, 0);
  /* A superseded worker must not apply a move after reset. */
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.kind[0]), 4);
  stop();
  advance(NULL);
  g_assert_true(app.busy);
  restart(NULL, NULL);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.kind[0]), 0);
  stop();
  pump(150);
  g_assert_cmpuint(app.game.actions, ==, 0);
  gtk_window_destroy(app.window);
  app.window = NULL;
  g_object_unref(application);
  g_string_free(app.history, TRUE);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  adw_init();
  g_test_add_func("/coriovalli/ui/lifecycle", lifecycle);
  return g_test_run();
}
