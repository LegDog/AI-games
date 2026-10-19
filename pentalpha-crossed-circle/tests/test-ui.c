// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-fixture.h"
#include "ollama-server.h"
static void human(App *app,gconstpointer data)
{
    (void)data;begin_clicked(NULL,app);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),1);
    Move move=game_moves[118];select_point(app,move.entry);select_point(app,move.destination);
    g_assert_true(app->animation.active);g_assert_cmpuint(app->game.occupied,==,0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0);g_assert_cmpint(app->animation.duration,==,1000000);
    app->animation.started=g_get_monotonic_time()-750000;app->paused=TRUE;animation_pause(&app->animation,g_get_monotonic_time());
    double position=animation_frame(&app->animation,G_MAXINT64).position;pump(50);
    g_assert_cmpfloat(animation_frame(&app->animation,G_MAXINT64).position,==,position);
    gtk_window_set_default_size(GTK_WINDOW(app->window),1100,850);pump(30);
    restart_clicked(NULL,app);pump(30);g_assert_cmpuint(app->game.occupied,==,0);g_assert_false(app->animation.active);
    select_point(app,move.entry);select_point(app,move.destination);g_assert_cmpuint(app->game.occupied,==,POINT_BIT(move.destination));
    undo_clicked(NULL,app);g_assert_cmpuint(app->game.occupied,==,0);
    select_point(app,move.entry);key_pressed(NULL,GDK_KEY_Escape,0,0,app);g_assert_cmpint(app->selected,==,-1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),1);start_animation(app,0);configure_clicked(NULL,app);
    pump(30);g_assert_cmpuint(app->game.moves,==,0);g_assert_false(app->playing);
}
static void automatic(App *app,gconstpointer data)
{
    (void)data;gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller),PLAYER_SEARCH);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->depth),2);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->memory_limit),16);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),.1);begin_clicked(NULL,app);pause_clicked(NULL,app);settle_worker(app);
    g_assert_true(app->ready);g_assert_cmpuint(app->game.moves,==,0);g_assert_cmpuint(app->last_search.completed_depth,==,2);
    pause_clicked(NULL,app);g_assert_true(app->animation.active);pause_clicked(NULL,app);
    double position=animation_frame(&app->animation,g_get_monotonic_time()).position;pump(50);
    g_assert_cmpfloat(animation_frame(&app->animation,g_get_monotonic_time()).position,==,position);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0);pause_clicked(NULL,app);
    gint64 end=g_get_monotonic_time()+10000000;
    while(!game_result(&app->game) && g_get_monotonic_time()<end)pump(2);
    g_assert_cmpint(game_result(&app->game),!=,0);g_assert_true(app->saved);
    g_autofree char *history=NULL;g_assert_true(g_file_get_contents(GAME_HISTORY_FILE,&history,NULL,NULL));
    g_assert_nonnull(strstr(history,"Search depth:"));g_assert_nonnull(strstr(history,"peak memory:"));g_assert_nonnull(strstr(history,"path p"));
}
static void retry_language(App *app,gconstpointer data)
{
    (void)data;gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller),PLAYER_SEARCH);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->time_limit),1e-12);begin_clicked(NULL,app);settle_worker(app);
    g_assert_true(app->failed);g_assert_cmpuint(app->game.moves,==,0);g_assert_true(gtk_widget_get_visible(app->retry));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->time_limit),60);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->depth),1);
    retry_clicked(NULL,app);pause_clicked(NULL,app);settle_worker(app);g_assert_false(app->failed);g_assert_true(app->ready);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language),I18N_PORTUGUESE);g_assert_cmpuint(app->game.moves,==,0);
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(app->status)),"Profundidade"));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language),I18N_ENGLISH);
    configure_clicked(NULL,app);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->depth),35);
    begin_clicked(NULL,app);configure_clicked(NULL,app);settle_worker(app);g_assert_cmpuint(app->game.moves,==,0);g_assert_false(app->failed);
}
static void ollama_lifecycle(App *app,gconstpointer data)
{
    (void)data;Fixture fixture={0};setup(&fixture,NULL);if(!fixture.url){teardown(&fixture,NULL);return;}
    g_free(app->url);app->url=g_strdup(fixture.url);fixture.body="{\"models\":[{\"name\":\"mock:1b\"}]}";
    refresh_clicked(NULL,app);settle_worker(app);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller),PLAYER_OLLAMA);
    fixture.auto_move=TRUE;fixture.delayed=TRUE;begin_clicked(NULL,app);
    gint64 end=g_get_monotonic_time()+2000000;while(!fixture.paused->len && g_get_monotonic_time()<end)pump(1);
    g_assert_cmpuint(fixture.paused->len,==,1);configure_clicked(NULL,app);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller),PLAYER_HUMAN);
    begin_clicked(NULL,app);settle_worker(app);g_assert_cmpuint(app->game.moves,==,0);g_assert_null(app->error);
    for(unsigned i=0;i<fixture.paused->len;i++)soup_server_message_unpause(g_ptr_array_index(fixture.paused,i));
    g_ptr_array_set_size(fixture.paused,0);fixture.delayed=FALSE;fixture.auto_move=FALSE;fixture.body="{\"done\":true,\"response\":\"bad\"}";
    configure_clicked(NULL,app);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller),PLAYER_OLLAMA);begin_clicked(NULL,app);settle_worker(app);
    g_assert_true(app->failed);g_assert_cmpuint(app->game.moves,==,0);teardown(&fixture,NULL);
}
#include "test-end-pause.inc"
#include "test-ui-variations.inc"
int main(int argc,char **argv)
{
    g_setenv("OLLAMA_LOG_FILE","/dev/null",TRUE);g_setenv("OLLAMA_ERROR_LOG_FILE","/dev/null",TRUE);
    g_test_init(&argc,&argv,NULL);gtk_init();adw_init();test_directory=g_dir_make_tmp("crossed-ui-XXXXXX",NULL);
    g_assert_nonnull(test_directory);g_autofree char *old=g_get_current_dir();g_chdir(test_directory);
    g_test_add("/ui/human-paths",App,NULL,ui_setup,human,ui_teardown);
    g_test_add("/ui/automatic",App,NULL,ui_setup,automatic,ui_teardown);
    g_test_add("/ui/retry-language",App,NULL,ui_setup,retry_language,ui_teardown);
    g_test_add("/ui/ollama-cancellation",App,NULL,ui_setup,ollama_lifecycle,ui_teardown);
    g_test_add("/ui/pause-at-end",App,NULL,ui_setup,end_pause,ui_teardown);
    g_test_add("/ui/graph-and-symmetry-variations",App,NULL,ui_setup,graph_variations,ui_teardown);
    int result=g_test_run();g_remove(GAME_HISTORY_FILE);g_remove("settings.ini");g_chdir(old);g_rmdir(test_directory);g_free(test_directory);return result;
}
