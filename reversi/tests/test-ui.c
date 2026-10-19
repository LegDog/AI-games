// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-fixture.h"
#include "../../tools/test-ui-panels.h"
#include "../../tools/test-ollama-integration.h"
#include "ollama-server.h"
static void humans(App *app)
{
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[0]),PLAYER_HUMAN);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[1]),PLAYER_HUMAN);
}
static void human_animation(App *app,gconstpointer data)
{
    (void)data;humans(app);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),1);begin_clicked(NULL,app);Game before=app->game;
    play_human(app,19);g_assert_true(app->animation.active);g_assert_cmpuint(app->flips,==,SQUARE_BIT(27));g_assert_cmpuint(app->game.black,==,before.black);g_assert_cmpuint(app->game.white,==,before.white);
    app->animation.started=g_get_monotonic_time()-750000;pause_clicked(NULL,app);double position=animation_frame(&app->animation,G_MAXINT64).position;pump(50);
    g_assert_cmpfloat(animation_frame(&app->animation,G_MAXINT64).position,==,position);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0);g_assert_cmpint(app->animation.duration,==,1000000);
    configure_clicked(NULL,app);pump(20);g_assert_cmpuint(app->game.moves,==,0);g_assert_false(app->animation.active);
    begin_clicked(NULL,app);play_human(app,19);g_assert_cmpuint(game_count(app->game.black),==,4);g_assert_cmpuint(game_count(app->game.white),==,1);
    undo_clicked(NULL,app);g_assert_cmpuint(app->game.black,==,before.black);g_assert_true(app->paused);pause_clicked(NULL,app);
    gtk_window_set_default_size(GTK_WINDOW(app->window),1100,850);pump(20);double scale,x,y;board_transform(app,&scale,&x,&y);
    click(NULL,1,x+(72+3*64)*scale,y+(72+2*64)*scale,app);g_assert_cmpuint(app->game.moves,==,1);
    undo_clicked(NULL,app);pause_clicked(NULL,app);app->cursor=19;key_pressed(NULL,GDK_KEY_Right,0,0,app);g_assert_cmpint(app->cursor,==,20);key_pressed(NULL,GDK_KEY_Left,0,0,app);key_pressed(NULL,GDK_KEY_Return,0,0,app);g_assert_cmpuint(app->game.moves,==,1);
}
static void automatic(App *app,gconstpointer data)
{
    (void)data;gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[0]),PLAYER_MINIMAX);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[1]),PLAYER_MCTS);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->depth[0]),2);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->iterations[1]),40);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0);
    begin_clicked(NULL,app);pause_clicked(NULL,app);settle_worker(app);g_assert_true(app->ready);g_assert_cmpuint(app->game.moves,==,0);pause_clicked(NULL,app);
    gint64 end=g_get_monotonic_time()+10000000;while(!game_result(&app->game) && g_get_monotonic_time()<end)pump(2);
    g_assert_cmpint(game_result(&app->game),!=,0);g_assert_true(app->saved);g_assert_cmpuint(game_count(app->game.black|app->game.white),==,4+app->game.moves);
    g_autofree char *history=NULL;g_assert_true(g_file_get_contents(GAME_HISTORY_FILE,&history,NULL,NULL));g_assert_nonnull(strstr(history,"Monte Carlo tree search"));g_assert_nonnull(strstr(history,"nodes:"));
    guint64 black=app->game.black,white=app->game.white;pump(30);g_assert_cmpuint(app->game.black,==,black);g_assert_cmpuint(app->game.white,==,white);
}
static void forced_pass(App *app,gconstpointer data)
{
    (void)data;humans(app);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0);begin_clicked(NULL,app);
    app->game=(Game){.black=SQUARE_BIT(1),.white=SQUARE_BIT(0),.turn=1};schedule(app);
    g_assert_cmpint(app->game.turn,==,-1);g_assert_cmpuint(app->game.passes,==,1);g_assert_cmpuint(app->actions,==,1);g_assert_cmpint(app->last_pass,==,1);
    g_assert_nonnull(strstr(app->history->str,"Forced pass"));play_human(app,2);g_assert_cmpint(game_result(&app->game),==,-1);g_assert_cmpuint(game_count(app->game.white),==,3);g_assert_true(app->saved);
}
static void retry_translation(App *app,gconstpointer data)
{
    (void)data;gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[0]),PLAYER_MINIMAX);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->seconds),1e-12);
    begin_clicked(NULL,app);settle_worker(app);g_assert_true(app->failed);g_assert_cmpuint(app->game.moves,==,0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->seconds),5);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->depth[0]),1);retry_clicked(NULL,app);pause_clicked(NULL,app);settle_worker(app);
    g_assert_false(app->failed);g_assert_true(app->ready);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language),I18N_PORTUGUESE);
    g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(app->undo)),==,"Desfazer");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(app->status)),"Pretas"));g_assert_cmpuint(app->game.moves,==,0);
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
    (void)data;ui_assert_stable_text_panels(app->board);
}
int main(int argc,char **argv)
{
    g_setenv("OLLAMA_LOG_FILE","/dev/null",TRUE);g_setenv("OLLAMA_ERROR_LOG_FILE","/dev/null",TRUE);g_test_init(&argc,&argv,NULL);gtk_init();adw_init();
    test_directory=g_dir_make_tmp("reversi-ui-XXXXXX",NULL);g_assert_nonnull(test_directory);g_autofree char *old=g_get_current_dir();g_chdir(test_directory);
    g_test_add("/ui/fixed-text-panels",App,NULL,ui_setup,stable_text_panels,ui_teardown);
    g_test_add("/ui/atomic-flip-undo",App,NULL,ui_setup,human_animation,ui_teardown);g_test_add("/ui/minimax-mcts",App,NULL,ui_setup,automatic,ui_teardown);
    g_test_add("/ui/forced-pass",App,NULL,ui_setup,forced_pass,ui_teardown);g_test_add("/ui/retry-language-cancel",App,NULL,ui_setup,retry_translation,ui_teardown);
    g_test_add("/ui/ollama-lifecycle",App,NULL,ui_setup,ollama_lifecycle,ui_teardown);int result=g_test_run();
    g_remove(GAME_HISTORY_FILE);g_remove("settings.ini");g_chdir(old);g_rmdir(test_directory);g_free(test_directory);return result;
}
