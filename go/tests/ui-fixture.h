// SPDX-License-Identifier: GPL-3.0-or-later
#define GO_TEST
#include "../src/main.c"
#include <glib/gstdio.h>
static void pump(unsigned ms) {
  gint64 end = g_get_monotonic_time() + ms * 1000;
  do {
    while (g_main_context_iteration(NULL, FALSE)) {
    }
    g_usleep(1000);
  } while (g_get_monotonic_time() < end);
}
G_GNUC_UNUSED static void settle(App *a) {
  gint64 end = g_get_monotonic_time() + 5000000;
  while (a->task && g_get_monotonic_time() < end)
    pump(2);
  g_assert_null(a->task);
}
static GtkApplication *test_application;
static App *fixture(void) {
  if (!test_application) {
    test_application = GTK_APPLICATION(adw_application_new(
        "io.github.aigames.Go.Test", G_APPLICATION_NON_UNIQUE));
    g_assert_true(
        g_application_register(G_APPLICATION(test_application), NULL, NULL));
  }
  App *a = create_app(test_application);
  gtk_window_present(GTK_WINDOW(a->window));
  pump(30);
  return a;
}
static void dispose(App *a) {
  close_requested(GTK_WINDOW(a->window), a);
  gtk_window_destroy(GTK_WINDOW(a->window));
  pump(30);
}
static void humans(App *a) {
  for (unsigned c = 0; c < 2; c++)
    gtk_drop_down_set_selected(GTK_DROP_DOWN(a->controllers[c]), PLAYER_HUMAN);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->delay), 0);
  begin_clicked(NULL, a);
}
G_GNUC_UNUSED static void place_fixture(App *a, unsigned color,
                                        const char *point) {
  if (a->game.state.turn != color)
    g_assert_true(game_play(&a->game, GO_PASS));
  g_assert_true(game_play(&a->game, game_point(point)));
}
