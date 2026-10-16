// SPDX-License-Identifier: GPL-3.0-or-later
/* Capture the actual application widgets; requires a real or virtual display. */
#define main application_main
#ifdef FOUR_IN_A_ROW
#include "../four-in-a-row/src/main.c"
#else
#include "../tic-tac-toe/src/main.c"
#endif
#undef main
#include <glib/gstdio.h>

static gboolean settle(gpointer data)
{
    g_main_loop_quit(data);
    return G_SOURCE_REMOVE;
}

static gboolean capture(App *app, const char *directory, const char *name)
{
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    g_timeout_add(500, settle, loop);
    g_main_loop_run(loop);
    GtkWidget *widget = GTK_WIDGET(app->window);
    int width = gtk_widget_get_width(widget), height = gtk_widget_get_height(widget);
    g_autoptr(GdkPaintable) paintable = gtk_widget_paintable_new(widget);
    GtkSnapshot *snapshot = gtk_snapshot_new();
    gdk_paintable_snapshot(paintable, snapshot, width, height);
    GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
    if (!node) return FALSE;
    graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, width, height);
    GskRenderer *renderer = gtk_native_get_renderer(GTK_NATIVE(widget));
    g_autoptr(GdkTexture) texture = gsk_renderer_render_texture(renderer, node, &bounds);
    gsk_render_node_unref(node);
    g_autofree char *path = g_build_filename(directory, name, NULL);
    return gdk_texture_save_to_png(texture, path);
}

int main(int argc, char **argv)
{
    if (argc != 2 || g_mkdir_with_parents(argv[1], 0755) < 0) {
        g_printerr("Usage: screenshot OUTPUT_DIRECTORY\n");
        return 2;
    }
    i18n_set_language(I18N_ENGLISH);
    App app = {0};
    app.delay = .5;
    app.random = g_rand_new_with_seed(42);
    app.history = g_string_new(NULL);
    app.application = GTK_APPLICATION(adw_application_new("io.github.aigames.Preview", G_APPLICATION_NON_UNIQUE));
    GError *error = NULL;
    if (!g_application_register(G_APPLICATION(app.application), NULL, &error)) {
        g_printerr("%s\n", error->message);
        g_clear_error(&error);
        return 1;
    }
    activate(app.application, &app);
    gboolean success = capture(&app, argv[1], GAME_CHOICES == 7 ? "four-in-a-row-configuration.png" : "tic-tac-toe-configuration.png");
    gtk_drop_down_set_selected(app.controllers[0], PLAYER_HUMAN);
    gtk_drop_down_set_selected(app.controllers[1], PLAYER_HUMAN);
    begin_clicked(NULL, &app);
#ifdef FOUR_IN_A_ROW
    const unsigned moves[] = {3, 2, 3, 4, 2, 1};
    for (unsigned i = 0; i < G_N_ELEMENTS(moves); i++) play_column(&app, moves[i]);
#else
    const unsigned moves[] = {4, 0, 2, 6};
    for (unsigned i = 0; i < G_N_ELEMENTS(moves); i++) play_cell(&app, moves[i]);
#endif
    success = capture(&app, argv[1], GAME_CHOICES == 7 ? "four-in-a-row.png" : "tic-tac-toe.png") && success;
    gtk_window_destroy(app.window);
    shutdown_jobs(&app);
    g_object_unref(app.application);
    g_rand_free(app.random);
    g_string_free(app.history, TRUE);
    return success ? 0 : 1;
}
