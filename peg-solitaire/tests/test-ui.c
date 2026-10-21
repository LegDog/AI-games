// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-fixture.h"
#include "../../tools/test-ui-panels.h"
#include "../../tools/test-ollama-controls.h"
#include "../../tools/test-ollama-integration.h"
#include "ollama-server.h"
static unsigned human_jump(App *app)
{
    MoveList legal=game_legal_moves(&app->game); g_assert_cmpuint(legal.count,>,0);
    Move move=game_board(app->game.board)->jumps[legal.items[0]];
    select_point(app,move.source); select_point(app,move.destination); return legal.items[0];
}
static void human_animation(App *app,gconstpointer data)
{
    (void)data;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),1); begin_clicked(NULL,app);
    guint64 initial=app->game.occupied; unsigned index=human_jump(app); Move move=game_board(app->game.board)->jumps[index];
    g_assert_true(app->animation.active); g_assert_cmpuint(app->game.occupied,==,initial);
    select_point(app,move.middle); g_assert_cmpint(app->pending,==,(int)index);
    animation_pause(&app->animation,app->animation.started+250000);
    AnimationFrame frame=animation_frame(&app->animation,G_MAXINT64); g_assert_cmpfloat(frame.position,==,0);
    gtk_window_set_default_size(GTK_WINDOW(app->window),1100,850); pump(30);
    restart_clicked(NULL,app); g_assert_false(app->animation.active); pump(30); g_assert_cmpuint(app->game.occupied,==,initial);
    human_jump(app); app->animation.started=g_get_monotonic_time()-750000; animation_pause(&app->animation,g_get_monotonic_time());
    g_assert_cmpfloat(animation_frame(&app->animation,G_MAXINT64).position,>,.4);
    configure_clicked(NULL,app); pump(30); g_assert_cmpuint(app->game.occupied,==,initial);
    begin_clicked(NULL,app); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0); index=human_jump(app);
    g_assert_cmpuint(app->game.moves,==,1); g_assert_cmpuint(game_count(app->game.occupied),==,31);
    move=game_board(app->game.board)->jumps[index];
    g_assert_false((app->game.occupied & POINT_BIT(move.source))!=0); g_assert_false((app->game.occupied & POINT_BIT(move.middle))!=0);
    g_assert_true((app->game.occupied & POINT_BIT(move.destination))!=0); pump(30); g_assert_cmpuint(app->game.moves,==,1);
    undo_clicked(NULL,app); g_assert_cmpuint(app->game.occupied,==,initial);
    MoveList legal=game_legal_moves(&app->game); move=game_board(app->game.board)->jumps[legal.items[0]];
    select_point(app,move.source); key_pressed(NULL,GDK_KEY_Escape,0,0,app); g_assert_cmpint(app->selected,==,-1);
    double scale,x,y; board_transform(app,&scale,&x,&y); const Board *b=game_board(app->game.board);
    click(NULL,1,x+b->x[move.source]*scale,y+b->y[move.source]*scale,app); g_assert_cmpint(app->selected,==,(int)move.source);
}
static void automatic(App *app,gconstpointer data)
{
    (void)data;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller),PLAYER_BACKTRACKING);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(app->center),TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),.1); begin_clicked(NULL,app); pause_clicked(NULL,app); settle_worker(app);
    g_assert_true(app->ready); g_assert_cmpuint(app->game.moves,==,0); g_assert_cmpuint(app->plan.length,==,31);
    pause_clicked(NULL,app); g_assert_true(app->animation.active); app->animation.started=g_get_monotonic_time()-75000; pause_clicked(NULL,app);
    AnimationFrame before=animation_frame(&app->animation,g_get_monotonic_time()); pump(100);
    g_assert_cmpfloat(animation_frame(&app->animation,g_get_monotonic_time()).position,==,before.position);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0); g_assert_cmpint(app->animation.duration,==,100000); pause_clicked(NULL,app);
    gint64 end=g_get_monotonic_time()+5000000;
    while (game_result(&app->game)==GAME_ONGOING && g_get_monotonic_time()<end) pump(5);
    g_assert_cmpint(game_result(&app->game),==,GAME_SOLVED); g_assert_cmpuint(app->game.moves,==,31); g_assert_true(app->saved);
    guint64 solved=app->game.occupied; pump(30); g_assert_cmpuint(app->game.occupied,==,solved);
    g_autofree char *history=NULL; g_assert_true(g_file_get_contents(GAME_HISTORY_FILE,&history,NULL,NULL));
    g_assert_nonnull(strstr(history,"goal: center")); g_assert_nonnull(strstr(history,"planned jumps: 31"));
    configure_clicked(NULL,app); gtk_drop_down_set_selected(GTK_DROP_DOWN(app->board_type),BOARD_TRIANGLE);
    g_assert_false(gtk_widget_get_visible(app->center)); gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller),PLAYER_ASTAR);
    begin_clicked(NULL,app); end=g_get_monotonic_time()+5000000;
    while (game_result(&app->game)==GAME_ONGOING && g_get_monotonic_time()<end) pump(5);
    g_assert_cmpint(game_result(&app->game),==,GAME_SOLVED); g_assert_cmpuint(app->game.moves,==,13);
}
static void retry_and_translation(App *app,gconstpointer data)
{
    (void)data;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller),PLAYER_BACKTRACKING);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->time_limit),1e-12); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0);
    begin_clicked(NULL,app); settle_worker(app); g_assert_true(app->failed); g_assert_cmpuint(app->game.moves,==,0);
    g_assert_true(gtk_widget_get_sensitive(app->time_limit)); g_assert_true(gtk_widget_get_visible(app->retry));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->time_limit),60); retry_clicked(NULL,app); settle_worker(app);
    g_assert_cmpint(game_result(&app->game),==,GAME_SOLVED); g_assert_false(app->failed);
    configure_clicked(NULL,app); gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller),PLAYER_HUMAN); begin_clicked(NULL,app);
    guint64 snapshot=app->game.occupied; MoveList legal=game_legal_moves(&app->game);
    unsigned selected=game_board(app->game.board)->jumps[legal.items[0]].source; select_point(app,selected);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language),I18N_PORTUGUESE);
    g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(app->undo)),==,"Desfazer");
    g_assert_cmpuint(app->game.occupied,==,snapshot); g_assert_cmpint(app->selected,==,(int)selected);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language),I18N_ENGLISH);
    configure_clicked(NULL,app); gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller),PLAYER_BACKTRACKING);
    begin_clicked(NULL,app); configure_clicked(NULL,app); settle_worker(app);
    g_assert_cmpuint(app->game.moves,==,0); g_assert_cmpuint(app->plan.length,==,0); g_assert_false(app->playing);
}
static void ollama_lifecycle(App *app,gconstpointer data)
{
    (void)data; Fixture fixture={0}; setup(&fixture,NULL); if (!fixture.url) { teardown(&fixture,NULL); return; }
    g_free(app->url); app->url=g_strdup(fixture.url); fixture.body="{\"models\":[{\"name\":\"mock:1b\"}]}";
    refresh_clicked(NULL,app); settle_worker(app); gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller),PLAYER_OLLAMA);
    fixture.auto_move=TRUE; fixture.delayed=TRUE; begin_clicked(NULL,app);
    gint64 end=g_get_monotonic_time()+2000000; while (!fixture.paused->len && g_get_monotonic_time()<end) pump(1);
    g_assert_cmpuint(fixture.paused->len,==,1); configure_clicked(NULL,app);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller),PLAYER_HUMAN); begin_clicked(NULL,app); settle_worker(app);
    g_assert_cmpuint(app->game.moves,==,0); g_assert_null(app->error);
    for (unsigned i=0;i<fixture.paused->len;i++) soup_server_message_unpause(g_ptr_array_index(fixture.paused,i));
    g_ptr_array_set_size(fixture.paused,0); fixture.delayed=FALSE;
    configure_clicked(NULL,app); gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller),PLAYER_OLLAMA);
    fixture.auto_move=FALSE; fixture.body="{\"done\":true,\"response\":\"bad move\"}";
    begin_clicked(NULL,app); settle_worker(app); g_assert_true(app->failed); g_assert_cmpuint(app->game.moves,==,0);
    ui_assert_ollama_transcript(app->board);teardown(&fixture,NULL);
}
static void stable_text_panels(App *app,gconstpointer data)
{
    (void)data;ui_assert_ollama_controls(app->board);
}
int main(int argc,char **argv)
{
    g_setenv("OLLAMA_LOG_FILE","/dev/null",TRUE); g_setenv("OLLAMA_ERROR_LOG_FILE","/dev/null",TRUE);
    g_test_init(&argc,&argv,NULL); gtk_init(); adw_init(); GError *error=NULL;
    test_directory=g_dir_make_tmp("peg-ui-XXXXXX",&error); g_assert_no_error(error); g_autofree char *old=g_get_current_dir(); g_chdir(test_directory);
    g_test_add("/ui/fixed-text-panels",App,NULL,ui_setup,stable_text_panels,ui_teardown);
    g_test_add("/ui/human-hop-capture-undo",App,NULL,ui_setup,human_animation,ui_teardown);
    g_test_add("/ui/plans-both-boards",App,NULL,ui_setup,automatic,ui_teardown);
    g_test_add("/ui/retry-language-cancellation",App,NULL,ui_setup,retry_and_translation,ui_teardown);
    g_test_add("/ui/ollama-cancellation-error",App,NULL,ui_setup,ollama_lifecycle,ui_teardown);
    int result=g_test_run(); g_remove(GAME_HISTORY_FILE); g_remove("settings.ini"); g_chdir(old); g_rmdir(test_directory); g_free(test_directory); return result;
}
