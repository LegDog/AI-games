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
    if(argc!=2){g_printerr("Usage: screenshot DIRECTORY\n");return 2;}
    g_setenv("OLLAMA_LOG_FILE","/dev/null",TRUE);g_setenv("OLLAMA_ERROR_LOG_FILE","/dev/null",TRUE);gtk_init();adw_init();
    g_autofree char *output=g_canonicalize_filename(argv[1],NULL);if(g_mkdir_with_parents(output,0755)<0)return 1;
    test_directory=g_dir_make_tmp("tavli-preview-XXXXXX",NULL);if(!test_directory)return 1;g_autofree char *old=g_get_current_dir();g_chdir(test_directory);
    App app={0};ui_setup(&app,NULL);gboolean ok=TRUE;
    for(Variant v=PORTES;v<VARIANT_COUNT;v++){gtk_drop_down_set_selected(GTK_DROP_DOWN(app.variant),v);g_autofree char *name=g_strdup_printf("tavli-%s.png",variant_name(v));ok&=capture(&app,output,name);}
    force_turn(&app,RUNNING,3,4);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app.delay),2);choose_human(&app,0);app.paused=TRUE;animation_pause(&app.animation,app.animation.started+1500000);ok&=capture(&app,output,"tavli-running-motion.png");
    new_configuration(NULL,&app);force_turn(&app,PLAKOTO,1,2);memset(app.game.point,0,sizeof app.game.point);app.game.point[8][0]=1;app.game.point[10][0]=14;app.game.point[7][1]=1;app.game.point[0][1]=14;app.draft=app.game;refresh_choices(&app);
    Step pin={ACTION_STEP,8,7,1};game_step(&app.draft,pin);app.draft_plan.length=1;app.draft_plan.steps[0]=pin;refresh_choices(&app);update(&app);ok&=capture(&app,output,"tavli-plakoto-pin.png");
    new_configuration(NULL,&app);gtk_drop_down_set_selected(GTK_DROP_DOWN(app.variant),BACKGAMMON);gtk_drop_down_set_selected(GTK_DROP_DOWN(app.session),SESSION_FIXED);humans(&app);game_init(&app.game,BACKGAMMON,SESSION_FIXED,5);app.game.phase=PRE_ROLL;app.playing=TRUE;game_control(&app.game,ACTION_DOUBLE);update(&app);ok&=capture(&app,output,"tavli-cube.png");
    ui_teardown(&app,NULL);g_remove(GAME_HISTORY_FILE);g_chdir(old);g_rmdir(test_directory);g_free(test_directory);return ok?0:1;
}
