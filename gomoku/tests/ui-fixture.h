// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#define main gomoku_application_main
#include "../src/main.c"
#undef main
static char *test_directory;
static void pump(unsigned milliseconds) {
  gint64 end = g_get_monotonic_time() + milliseconds * 1000;
  do {
    while (g_main_context_iteration(NULL, FALSE)) {
    }
    g_usleep(1000);
  } while (g_get_monotonic_time() < end);
}
static void settle_worker(App *app) {
  gint64 end = g_get_monotonic_time() + 5000000;
  while (app->task && g_get_monotonic_time() < end)
    pump(1);
  g_assert_null(app->task);
}
static void ui_setup(App *app, gconstpointer data) {
  (void)data;
  app->pending = -1;
  app->cursor = 0;
  app->random = g_rand_new_with_seed(42);
  app->history = g_string_new(NULL);
  game_init(&app->game);
  app->url = g_strdup("http://127.0.0.1:1");
  app->preferences = g_build_filename(test_directory, "settings.ini", NULL);
  i18n_set_language(I18N_ENGLISH);
  static unsigned id = 0;
  g_autofree char *name =
      g_strdup_printf("io.github.aigames.Gomoku.Test%u", ++id);
  app->application =
      GTK_APPLICATION(adw_application_new(name, G_APPLICATION_NON_UNIQUE));
  GError *error = NULL;
  g_assert_true(
      g_application_register(G_APPLICATION(app->application), NULL, &error));
  g_assert_no_error(error);
  activate(app->application, app);
  pump(30);
}
static void ui_teardown(App *app, gconstpointer data) {
  (void)data;
  app->closing = TRUE;
  cancel_work(app);
  settle_worker(app);
  if (app->window)
    gtk_window_destroy(GTK_WINDOW(app->window));
  app->window = NULL;
  g_object_unref(app->application);
  g_rand_free(app->random);
  g_string_free(app->history, TRUE);
  g_free(app->url);
  g_free(app->preferences);
  g_free(app->error);
  g_free(app->pending_details);
  for (unsigned i = 0; i < 2; i++)
    g_free(app->selected_models[i]);
  pump(10);
}
