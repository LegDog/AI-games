// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#define main pentalpha_application_main
#include "../src/main.c"
#undef main
#include <glib/gstdio.h>
static char *test_directory;
static void pump(unsigned milliseconds)
{
    gint64 end = g_get_monotonic_time() + milliseconds * 1000;
    do {
        while (g_main_context_iteration(NULL, FALSE)) {}
        g_usleep(1000);
    } while (g_get_monotonic_time() < end);
}
static void settle_worker(App *app)
{
    gint64 end = g_get_monotonic_time() + 5000000;
    while (app->task && g_get_monotonic_time() < end) pump(1);
    g_assert_null(app->task);
}
static void ui_setup(App *app, gconstpointer data)
{
    (void) data;
    app->selected = app->pending = -1;
    app->random = g_rand_new_with_seed(42);
    app->url = g_strdup("http://127.0.0.1:1");
    app->preferences = g_build_filename(test_directory, "settings.ini", NULL);
    i18n_set_language(I18N_ENGLISH);
    static unsigned application_id = 0;
    g_autofree char *id = g_strdup_printf("io.github.aigames.Pentalpha.Test%u", ++application_id);
    app->application = GTK_APPLICATION(adw_application_new(id, G_APPLICATION_NON_UNIQUE));
    GError *error = NULL;
    g_assert_true(g_application_register(G_APPLICATION(app->application), NULL, &error)); g_assert_no_error(error);
    activate(app->application, app);
    cancel_work(app); settle_worker(app); set_error(app, NULL); pump(50);
    g_assert_nonnull(app->background);
}
static void ui_teardown(App *app, gconstpointer data)
{
    (void) data;
    app->closing = TRUE;
    cancel_work(app); settle_worker(app);
    if (app->window) gtk_window_destroy(GTK_WINDOW(app->window));
    app->window = NULL;
    for (unsigned i = 0; i < 9; i++) g_free(app->details[i]);
    g_clear_pointer(&app->background, cairo_surface_destroy);
    g_object_unref(app->application); g_rand_free(app->random);
    g_free(app->url); g_free(app->preferences); g_free(app->selected_model); g_free(app->error); g_free(app->pending_details);
    pump(10);
}
