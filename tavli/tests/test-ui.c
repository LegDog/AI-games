// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-fixture.h"
#include "../../tools/test-ui-panels.h"
#include "../../tools/test-ollama-controls.h"
#include "../../tools/test-ollama-integration.h"
#include "ollama-server.h"
static void geometry(void)
{
    Game g;game_init(&g,PORTES,SESSION_SINGLE,5);
    for(unsigned q=0;q<24;q++)for(int p=0;p<2;p++){
        g.point[q][p]=15;double x,y;location(&g,p,q,&x,&y);
        g_assert_true(isfinite(x) && isfinite(y));g_assert_cmpfloat(x,>=,100);g_assert_cmpfloat(x,<=,915);
        g_assert_cmpfloat_with_epsilon(y,q<12?404:236,.001);
    }
    g.point[7][0]=1;g.point[7][1]=1;g.pinned[7]=1;double x,y;location(&g,0,7,&x,&y);g_assert_cmpfloat_with_epsilon(y,491,.001);
    game_init(&g,RUNNING,SESSION_SINGLE,5);
    location(&g,1,SOURCE_RESERVE,&x,&y);g_assert_cmpfloat(x,==,955);g_assert_cmpfloat(y,==,365);
    location(&g,1,DEST_OFF,&x,&y);g_assert_cmpfloat(x,==,955);g_assert_cmpfloat(y,==,80);
    game_init(&g,FEVGA,SESSION_SINGLE,5);
    location(&g,1,DEST_OFF,&x,&y);g_assert_cmpfloat(x,==,45);g_assert_cmpfloat(y,==,80);
}
static void animation_undo(App *app,gconstpointer data)
{
    (void)data;force_turn(app,RUNNING,1,2);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),1);g_assert_cmpuint(app->game.reserve[0],==,15);
    choose_human(app,0);g_assert_true(app->animation.active);g_assert_cmpuint(app->draft.reserve[0],==,15);
    app->animation.started=g_get_monotonic_time()-750000;pause_clicked(NULL,app);double pos=animation_frame(&app->animation,G_MAXINT64).position;pump(40);g_assert_cmpfloat(animation_frame(&app->animation,G_MAXINT64).position,==,pos);
    pause_clicked(NULL,app);app->animation.started=g_get_monotonic_time()-2000000;pump(40);g_assert_false(app->animation.active);g_assert_cmpuint(app->draft.reserve[0],==,14);g_assert_cmpuint(app->game.reserve[0],==,15);
    guint8 original_roll[2];memcpy(original_roll,app->game.roll,2);undo_clicked(NULL,app);g_assert_cmpuint(app->draft.reserve[0],==,15);g_assert_cmpmem(app->draft.roll,2,original_roll,2);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0);select_location(app,SOURCE_RESERVE);g_assert_cmpint(app->selected,==,SOURCE_RESERVE);select_location(app,23);g_assert_cmpuint(app->draft_plan.length,==,1);
    choose_human(app,0);g_assert_true(can_confirm(app));confirm_turn(app);g_assert_cmpuint(app->game.reserve[0],<,15);g_assert_cmpuint(app->game.turns,==,1);g_assert_cmpint(app->game.active,==,1);
    force_turn(app,RUNNING,1,1);choose_human(app,0);choose_human(app,0);g_assert_true(can_confirm(app));g_assert_cmpuint(app->draft_plan.length,==,2);
    new_configuration(NULL,app);g_assert_false(app->drafting);g_assert_false(app->animation.active);
}
static void cube_match(App *app,gconstpointer data)
{
    (void)data;humans(app);game_init(&app->game,BACKGAMMON,SESSION_FIXED,5);app->game.phase=PRE_ROLL;app->playing=TRUE;update(app);
    g_assert_true(gtk_widget_get_visible(app->offer));control_clicked(GTK_BUTTON(app->offer),app);g_assert_cmpint(app->game.actor,==,1);g_assert_true(gtk_widget_get_visible(app->take));
    control_clicked(GTK_BUTTON(app->take),app);g_assert_cmpint(app->game.actor,==,0);g_assert_cmpint(app->game.cube_owner,==,1);g_assert_cmpuint(app->game.cube,==,2);
    force_turn(app,RUNNING,1,1);app->game.match.session=SESSION_FIXED;app->game.match.target=2;memset(app->game.point,0,sizeof app->game.point);app->game.reserve[0]=0;app->game.off[0]=14;app->game.point[0][0]=1;app->draft=app->game;refresh_choices(app);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0);choose_human(app,0);g_assert_true(can_confirm(app));g_assert_false(app->saved);confirm_turn(app);g_assert_true(app->saved);g_assert_cmpuint(app->game.match.score[0],==,1);
    g_assert_true(gtk_widget_get_visible(app->next));next_clicked(NULL,app);g_assert_cmpuint(app->game.match.number,==,1);g_assert_cmpuint(app->game.match.score[0],==,1);g_assert_cmpuint(app->game.reserve[0],==,15);
}
static void save_retry(App *app,gconstpointer data)
{
    (void)data;force_turn(app,RUNNING,1,1);app->game.match.session=SESSION_FIXED;app->game.match.target=2;
    app->game.reserve[0]=0;app->game.off[0]=14;app->game.point[0][0]=1;app->draft=app->game;refresh_choices(app);
    g_remove(GAME_HISTORY_FILE);g_assert_cmpint(g_mkdir(GAME_HISTORY_FILE,0700),==,0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0);choose_human(app,0);confirm_turn(app);
    g_assert_true(app->save_failed);g_assert_false(app->saved);g_assert_false(gtk_widget_get_sensitive(app->next));
    g_assert_cmpint(g_rmdir(GAME_HISTORY_FILE),==,0);retry_clicked(NULL,app);g_assert_true(app->saved);g_assert_false(app->save_failed);g_assert_null(app->error);
    new_configuration(NULL,app);g_assert_false(app->save_failed);g_assert_false(gtk_widget_get_visible(app->retry));
}
static void bots_pause_cancel(App *app,gconstpointer data)
{
    (void)data;gtk_drop_down_set_selected(GTK_DROP_DOWN(app->variant),RUNNING);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->session),SESSION_SINGLE);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[0]),PLAYER_HEURISTIC);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[1]),PLAYER_MCTS);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->iterations[1]),8);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0);
    begin_clicked(NULL,app);pause_clicked(NULL,app);settle_worker(app);g_assert_true(app->ready);g_assert_cmpuint(app->game.turns,==,0);pause_clicked(NULL,app);
    gint64 end=g_get_monotonic_time()+20000000;while(!game_finished(&app->game) && !app->failed && g_get_monotonic_time()<end)pump(2);
    g_assert_false(app->failed);g_assert_true(game_finished(&app->game));g_assert_true(app->saved);g_assert_true(game_valid(&app->game));
    new_configuration(NULL,app);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[0]),PLAYER_EXPECTI);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[1]),PLAYER_EXPECTI);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->depth[0]),6);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->depth[1]),6);
    begin_clicked(NULL,app);new_configuration(NULL,app);settle_worker(app);g_assert_false(app->playing);g_assert_false(app->failed);
}
static void retry_language(App *app,gconstpointer data)
{
    (void)data;gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[0]),PLAYER_EXPECTI);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[1]),PLAYER_EXPECTI);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->seconds),1e-12);
    begin_clicked(NULL,app);settle_worker(app);g_assert_true(app->failed);guint8 dice[2];memcpy(dice,app->game.roll,2);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->seconds),5);for(int p=0;p<2;p++)gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->depth[p]),1);
    retry_clicked(NULL,app);pause_clicked(NULL,app);settle_worker(app);g_assert_false(app->failed);g_assert_true(app->ready);g_assert_cmpmem(dice,2,app->game.roll,2);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language),I18N_PORTUGUESE);g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(app->roll)),==,"Lançar dados");g_assert_true(app->ready);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language),I18N_ENGLISH);new_configuration(NULL,app);
}
static void ollama_lifecycle(App *app,gconstpointer data)
{
    (void)data;Fixture fixture={0};setup(&fixture,NULL);if(!fixture.url){teardown(&fixture,NULL);return;}g_free(app->url);app->url=g_strdup(fixture.url);
    fixture.body="{\"models\":[{\"name\":\"mock:1b\"}]}";refresh_clicked(NULL,app);settle_worker(app);fixture.auto_move=TRUE;fixture.delayed=TRUE;
    for(int p=0;p<2;p++)gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[p]),PLAYER_OLLAMA);
    begin_clicked(NULL,app);
    gint64 end=g_get_monotonic_time()+2000000;while(!fixture.paused->len && g_get_monotonic_time()<end)pump(1);g_assert_cmpuint(fixture.paused->len,==,1);
    new_configuration(NULL,app);humans(app);begin_clicked(NULL,app);settle_worker(app);g_assert_cmpuint(app->game.turns,==,0);g_assert_false(app->failed);
    for(unsigned i=0;i<fixture.paused->len;i++)soup_server_message_unpause(g_ptr_array_index(fixture.paused,i));
    g_ptr_array_set_size(fixture.paused,0);
    fixture.delayed=FALSE;fixture.auto_move=FALSE;fixture.body="{\"done\":true,\"response\":\"bad\"}";new_configuration(NULL,app);
    for(int p=0;p<2;p++)gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[p]),PLAYER_OLLAMA);
    begin_clicked(NULL,app);settle_worker(app);g_assert_true(app->failed);g_assert_cmpuint(app->game.turns,==,0);ui_assert_ollama_transcript(app->board);teardown(&fixture,NULL);
}
static void stable_text_panels(App *app,gconstpointer data)
{
    (void)data;ui_assert_ollama_controls(app->board);
}
int main(int argc,char **argv)
{
    g_setenv("OLLAMA_LOG_FILE","/dev/null",TRUE);g_setenv("OLLAMA_ERROR_LOG_FILE","/dev/null",TRUE);g_test_init(&argc,&argv,NULL);gtk_init();adw_init();test_directory=g_dir_make_tmp("tavli-ui-XXXXXX",NULL);g_assert_nonnull(test_directory);g_autofree char *old=g_get_current_dir();g_chdir(test_directory);
    g_test_add("/ui/fixed-text-panels",App,NULL,ui_setup,stable_text_panels,ui_teardown);
    g_test_add_func("/ui/stack-route-geometry",geometry);
    g_test_add("/ui/atomic-undo-dice",App,NULL,ui_setup,animation_undo,ui_teardown);g_test_add("/ui/cube-next-game",App,NULL,ui_setup,cube_match,ui_teardown);g_test_add("/ui/save-retry",App,NULL,ui_setup,save_retry,ui_teardown);g_test_add("/ui/bots-pause-cancel",App,NULL,ui_setup,bots_pause_cancel,ui_teardown);g_test_add("/ui/retry-language",App,NULL,ui_setup,retry_language,ui_teardown);g_test_add("/ui/ollama-stale-invalid",App,NULL,ui_setup,ollama_lifecycle,ui_teardown);
    int result=g_test_run();g_remove(GAME_HISTORY_FILE);g_remove("settings.ini");g_chdir(old);g_rmdir(test_directory);g_free(test_directory);return result;
}
