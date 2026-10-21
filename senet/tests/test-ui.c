// SPDX-License-Identifier: GPL-3.0-or-later
#define SENET_UI_TEST
#include "../../tools/test-ui-panels.h"
#include "../src/main.c"
static void lifecycle(void) {
  AdwApplication *application = adw_application_new(
      "io.github.aigames.SenetTest", G_APPLICATION_NON_UNIQUE);
  GError *error = NULL;
  g_assert_true(
      g_application_register(G_APPLICATION(application), NULL, &error));
  activate(GTK_APPLICATION(application), NULL);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.kind[1]), 0);
  stop();
  gint64 until = g_get_monotonic_time() + 200000;
  while (g_get_monotonic_time() < until)
    g_main_context_iteration(NULL, FALSE);
  game_set_roll(&app.game, 1);
  play((GameMove){1});
  g_assert_cmpint(app.game.cells[2], ==, 1);
  g_assert_cmpint(app.game.turn, ==, 1);
  until = g_get_monotonic_time() + 450000;
  while (g_get_monotonic_time() < until)
    g_main_context_iteration(NULL, FALSE);
  g_assert_false(app.animating);
  g_assert_cmpint(gtk_widget_get_width(app.board), >, 400);
  g_assert_cmpint(gtk_widget_get_height(app.board), >=, 250);
  /* Water relocates atomically, including an occupied Rebirth house. */
  stop();
  app.game = (Game){
      .cells = {[26] = 1, [15] = 2}, .finished = {4, 4}, .turn = 1, .roll = 1};
  play((GameMove){26});
  g_assert_true(app.animating);
  g_assert_cmpint(app.game.cells[14], ==, 1);
  g_assert_cmpint(app.animation_dest, ==, 14);
  g_assert_cmpint(app.game.cells[27], ==, 0);
  until = g_get_monotonic_time() + 450000;
  while (g_get_monotonic_time() < until)
    g_main_context_iteration(NULL, FALSE);
  stop();
  ui_assert_stable_text_panels(app.board);
  restart(NULL, NULL);
  g_assert_cmpuint(app.game.cells[1], ==, 1);
  stop();
  gtk_window_destroy(app.window);
  app.window = NULL;
  g_object_unref(application);
  g_rand_free(app.dice);
  g_string_free(app.history, TRUE);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  adw_init();
  g_test_add_func("/senet/ui/lifecycle", lifecycle);
  return g_test_run();
}
