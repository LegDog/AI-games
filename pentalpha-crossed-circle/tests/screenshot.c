// SPDX-License-Identifier: GPL-3.0-or-later
/* Capture actual widgets, including a frozen frame during placement. */
#include "ui-fixture.h"
static gboolean capture(App *app, const char *directory, const char *name)
{
    pump(100);
    GtkWidget *widget = app->window;
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
    if (argc != 2) { g_printerr("Usage: screenshot OUTPUT_DIRECTORY\n"); return 2; }
    g_setenv("OLLAMA_LOG_FILE", "/dev/null", TRUE); g_setenv("OLLAMA_ERROR_LOG_FILE", "/dev/null", TRUE);
    gtk_init(); adw_init();
    g_autofree char *output = g_canonicalize_filename(argv[1], NULL);
    if (g_mkdir_with_parents(output, 0755) < 0) return 1;
    GError *error = NULL;
    test_directory = g_dir_make_tmp("pentalpha-crossed-circle-preview-XXXXXX", &error);
    if (!test_directory) { g_printerr("%s\n", error->message); g_error_free(error); return 1; }
    g_autofree char *old_directory = g_get_current_dir();
    g_chdir(test_directory);
    App app = {0}; ui_setup(&app, NULL);
    gboolean ok = capture(&app, output, "pentalpha-crossed-circle-configuration.png");
    begin_clicked(NULL,&app);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app.delay),0);
    SearchLimits limits={0,16*1024*1024,0};
    for(unsigned i=0;i<8;i++) {
        SearchResult result;search_evaluate(&app.game,3,&limits,NULL,&result);
        start_animation(&app,search_choose(&result,app.random));
    }
    SearchResult result;search_evaluate(&app.game,3,&limits,NULL,&result);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app.delay),2);start_animation(&app,search_choose(&result,app.random));
    app.paused=TRUE;animation_pause(&app.animation,app.animation.started+1500000);
    ok &= capture(&app,output,"pentalpha-crossed-circle-placement.png");
    restart_clicked(NULL,&app);start_animation(&app,118);
    app.paused=TRUE;animation_pause(&app.animation,app.animation.started+1500000);
    ok &= capture(&app,output,"pentalpha-crossed-circle-circle.png");
    configure_clicked(NULL,&app);gtk_drop_down_set_selected(GTK_DROP_DOWN(app.controller),PLAYER_GF2);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app.delay),0);begin_clicked(NULL,&app);
    gint64 end=g_get_monotonic_time()+5000000;
    while(!game_result(&app.game) && g_get_monotonic_time()<end)pump(2);
    ok &= game_result(&app.game)==2 && app.game.moves==34;
    ok &= capture(&app,output,"pentalpha-crossed-circle-gf2-optimum.png");
    ui_teardown(&app, NULL);
    g_remove(GAME_HISTORY_FILE); g_chdir(old_directory); g_rmdir(test_directory); g_free(test_directory);
    return ok ? 0 : 1;
}
