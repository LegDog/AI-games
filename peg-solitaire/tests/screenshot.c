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
int main(int argc,char **argv)
{
    if (argc!=2) { g_printerr("Usage: screenshot OUTPUT_DIRECTORY\n"); return 2; }
    g_setenv("OLLAMA_LOG_FILE","/dev/null",TRUE); g_setenv("OLLAMA_ERROR_LOG_FILE","/dev/null",TRUE); gtk_init(); adw_init();
    g_autofree char *output=g_canonicalize_filename(argv[1],NULL); if (g_mkdir_with_parents(output,0755)<0) return 1;
    test_directory=g_dir_make_tmp("peg-preview-XXXXXX",NULL); if (!test_directory) return 1;
    g_autofree char *old=g_get_current_dir(); g_chdir(test_directory); App app={0}; ui_setup(&app,NULL);
    gboolean ok=capture(&app,output,"peg-solitaire-configuration.png");
    begin_clicked(NULL,&app); SearchLimits limits=search_defaults(); SearchResult plan=search_solve(&app.game,PLAYER_BACKTRACKING,&limits,NULL,0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app.delay),0);
    for (unsigned i=0;i<8;i++) start_animation(&app,plan.path[i]);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app.delay),2); start_animation(&app,plan.path[8]);
    app.paused=TRUE; animation_pause(&app.animation,app.animation.started+1500000); gtk_widget_queue_draw(app.board);
    ok &= capture(&app,output,"peg-solitaire-jump.png");
    configure_clicked(NULL,&app); gtk_drop_down_set_selected(GTK_DROP_DOWN(app.board_type),BOARD_TRIANGLE);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app.controller),PLAYER_ASTAR); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app.delay),0);
    begin_clicked(NULL,&app); settle_worker(&app); ok &= game_result(&app.game)==GAME_SOLVED;
    ok &= capture(&app,output,"peg-solitaire-triangle-solved.png");
    ui_teardown(&app,NULL); g_remove(GAME_HISTORY_FILE); g_chdir(old); g_rmdir(test_directory); g_free(test_directory); return ok ? 0 : 1;
}
