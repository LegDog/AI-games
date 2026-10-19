// SPDX-License-Identifier: GPL-3.0-or-later
#define SOKOBAN_TEST
#include "../src/main.c"
#include <glib/gstdio.h>
static void pump(unsigned milliseconds) {
  gint64 end = g_get_monotonic_time() + milliseconds * 1000;
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
        "io.github.aigames.Sokoban.Test", G_APPLICATION_NON_UNIQUE));
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
  pump(20);
}
