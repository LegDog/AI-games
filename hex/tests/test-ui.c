// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-fixture.h"
#include "../../tools/test-ui-panels.h"
#include "../../tools/test-ollama-controls.h"
#include "../../tools/test-ollama-integration.h"
#include "ollama-server.h"
static void geometry(void)
{
    cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,2,2);cairo_t *cr=cairo_create(surface);
    hexagon(cr,100,100,30);cairo_path_t *path=cairo_copy_path(cr);
    g_assert_cmpint(path->data[0].header.type,==,CAIRO_PATH_MOVE_TO);
    g_assert_cmpfloat_with_epsilon(path->data[1].point.x,100+30*cos(-G_PI/6),.01);
    g_assert_cmpfloat_with_epsilon(path->data[1].point.y,85,.01);
    cairo_path_destroy(path);cairo_destroy(cr);cairo_surface_destroy(surface);
}
static void humans(App *app)
{
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[0]),PLAYER_HUMAN);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[1]),PLAYER_HUMAN);
}
static void human_animation(App *app,gconstpointer data)
{
    (void)data;humans(app);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),1);begin_clicked(NULL,app);
    play_human(app,15);g_assert_true(app->animation.active);g_assert_cmpuint(app->game.red,==,0);g_assert_cmpuint(app->game.actions,==,0);
    app->animation.started=g_get_monotonic_time()-750000;pause_clicked(NULL,app);double position=animation_frame(&app->animation,G_MAXINT64).position;pump(50);
    g_assert_cmpfloat(animation_frame(&app->animation,G_MAXINT64).position,==,position);
    configure_clicked(NULL,app);g_assert_cmpuint(app->game.moves,==,0);g_assert_false(app->animation.active);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0);begin_clicked(NULL,app);play_human(app,15);
    g_assert_cmpuint(app->game.red,==,SQUARE_BIT(15));g_assert_true(gtk_widget_get_visible(app->swap_move));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),1);swap_clicked(NULL,app);
    g_assert_true(app->animation.active);g_assert_cmpuint(app->game.red,==,SQUARE_BIT(15));g_assert_cmpuint(app->game.blue,==,0);
    app->animation.started=g_get_monotonic_time()-2000000;pump(40);g_assert_false(app->animation.active);
    g_assert_cmpuint(app->game.red,==,0);g_assert_cmpuint(app->game.blue,==,SQUARE_BIT(9));g_assert_cmpuint(app->actions,==,2);
    undo_clicked(NULL,app);g_assert_cmpuint(app->game.red,==,SQUARE_BIT(15));g_assert_cmpuint(app->game.blue,==,0);g_assert_true(app->paused);
    g_assert_true(game_legal_moves(&app->game)&SQUARE_BIT(GAME_SWAP));undo_clicked(NULL,app);g_assert_cmpuint(app->game.moves,==,0);pause_clicked(NULL,app);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0);double scale,x,y,cx,cy;board_transform(app,&scale,&x,&y);center(app,15,&cx,&cy);
    click(NULL,1,x+cx*scale,y+cy*scale,app);g_assert_cmpuint(app->game.red,==,SQUARE_BIT(15));
    undo_clicked(NULL,app);pause_clicked(NULL,app);app->cursor=0;key_pressed(NULL,GDK_KEY_Right,0,0,app);g_assert_cmpint(app->cursor,==,1);
    key_pressed(NULL,GDK_KEY_Return,0,0,app);g_assert_cmpuint(app->game.red,==,SQUARE_BIT(1));
    configure_clicked(NULL,app);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->size_choice),0);gtk_check_button_set_active(GTK_CHECK_BUTTON(app->swap_rule),FALSE);begin_clicked(NULL,app);
    g_assert_cmpuint(app->game.size,==,3);play_human(app,0);g_assert_false(gtk_widget_get_visible(app->swap_move));g_assert_false(game_legal_moves(&app->game)&SQUARE_BIT(GAME_SWAP));
}
static void automatic(App *app,gconstpointer data)
{
    (void)data;gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[0]),PLAYER_MINIMAX);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[1]),PLAYER_MCTS);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->depth[0]),2);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->iterations[1]),40);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0);
    begin_clicked(NULL,app);pause_clicked(NULL,app);settle_worker(app);g_assert_true(app->ready);g_assert_cmpuint(app->game.moves,==,0);pause_clicked(NULL,app);
    gint64 end=g_get_monotonic_time()+10000000;while(!game_result(&app->game) && g_get_monotonic_time()<end)pump(2);
    g_assert_cmpint(game_result(&app->game),!=,0);g_assert_true(app->saved);g_assert_cmpuint(game_count(app->game.red|app->game.blue),==,app->game.moves);
    g_autofree char *history=NULL;g_assert_true(g_file_get_contents(GAME_HISTORY_FILE,&history,NULL,NULL));g_assert_nonnull(strstr(history,"Monte Carlo tree search"));g_assert_nonnull(strstr(history,"nodes:"));
    guint64 black=app->game.red,white=app->game.blue;pump(30);g_assert_cmpuint(app->game.red,==,black);g_assert_cmpuint(app->game.blue,==,white);
}
static void retry_translation(App *app,gconstpointer data)
{
    (void)data;gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[0]),PLAYER_MINIMAX);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->seconds),1e-12);
    begin_clicked(NULL,app);settle_worker(app);g_assert_true(app->failed);g_assert_cmpuint(app->game.moves,==,0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->seconds),5);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->depth[0]),1);retry_clicked(NULL,app);pause_clicked(NULL,app);settle_worker(app);
    g_assert_false(app->failed);g_assert_true(app->ready);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language),I18N_PORTUGUESE);
    g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(app->undo)),==,"Desfazer");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(app->status)),"Vermelhas"));g_assert_cmpuint(app->game.moves,==,0);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language),I18N_ENGLISH);configure_clicked(NULL,app);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[0]),PLAYER_MCTS);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->iterations[0]),1000000);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->seconds),0);
    begin_clicked(NULL,app);configure_clicked(NULL,app);settle_worker(app);g_assert_cmpuint(app->game.moves,==,0);g_assert_false(app->failed);
}
static void ollama_lifecycle(App *app,gconstpointer data)
{
    (void)data;Fixture fixture={0};setup(&fixture,NULL);if(!fixture.url){teardown(&fixture,NULL);return;}
    g_free(app->url);app->url=g_strdup(fixture.url);fixture.body="{\"models\":[{\"name\":\"mock:1b\"}]}";refresh_clicked(NULL,app);settle_worker(app);
    g_assert_nonnull(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(app->models[0])));g_assert_nonnull(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(app->models[1])));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[0]),PLAYER_OLLAMA);fixture.auto_move=TRUE;fixture.delayed=TRUE;begin_clicked(NULL,app);
    gint64 end=g_get_monotonic_time()+2000000;while(!fixture.paused->len && g_get_monotonic_time()<end)pump(1);g_assert_cmpuint(fixture.paused->len,==,1);
    configure_clicked(NULL,app);humans(app);begin_clicked(NULL,app);settle_worker(app);g_assert_cmpuint(app->game.moves,==,0);g_assert_null(app->error);
    for(unsigned i=0;i<fixture.paused->len;i++)soup_server_message_unpause(g_ptr_array_index(fixture.paused,i));
    g_ptr_array_set_size(fixture.paused,0);
    fixture.delayed=FALSE;fixture.auto_move=FALSE;fixture.body="{\"done\":true,\"response\":\"bad\"}";
    configure_clicked(NULL,app);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[0]),PLAYER_OLLAMA);begin_clicked(NULL,app);settle_worker(app);g_assert_true(app->failed);g_assert_cmpuint(app->game.moves,==,0);ui_assert_ollama_transcript(app->board);teardown(&fixture,NULL);
}
static void stable_text_panels(App *app,gconstpointer data)
{
    (void)data;ui_assert_ollama_controls(app->board);
}
int main(int argc,char **argv)
{
    g_setenv("OLLAMA_LOG_FILE","/dev/null",TRUE);g_setenv("OLLAMA_ERROR_LOG_FILE","/dev/null",TRUE);g_test_init(&argc,&argv,NULL);gtk_init();adw_init();
    test_directory=g_dir_make_tmp("hex-ui-XXXXXX",NULL);g_assert_nonnull(test_directory);g_autofree char *old=g_get_current_dir();g_chdir(test_directory);
    g_test_add("/ui/fixed-text-panels",App,NULL,ui_setup,stable_text_panels,ui_teardown);
    g_test_add_func("/ui/hexagon-geometry",geometry);
    g_test_add("/ui/atomic-place-swap-undo",App,NULL,ui_setup,human_animation,ui_teardown);g_test_add("/ui/minimax-mcts",App,NULL,ui_setup,automatic,ui_teardown);
    g_test_add("/ui/retry-language-cancel",App,NULL,ui_setup,retry_translation,ui_teardown);
    g_test_add("/ui/ollama-lifecycle",App,NULL,ui_setup,ollama_lifecycle,ui_teardown);int result=g_test_run();
    g_remove(GAME_HISTORY_FILE);g_remove("settings.ini");g_chdir(old);g_rmdir(test_directory);g_free(test_directory);return result;
}
