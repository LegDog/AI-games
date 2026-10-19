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
    (void)data;humans(app);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),1);begin_clicked(NULL,app);
    play_human(app,0);g_assert_cmpint(app->draft,==,0);g_assert_false(app->animation.active);g_assert_cmpuint(app->game.black,==,0);
    play_human(app,1);g_assert_cmpint(app->draft,==,1);key_pressed(NULL,GDK_KEY_Escape,0,0,app);g_assert_cmpint(app->draft,==,-1);
    play_human(app,0);rotate_human(app,0);g_assert_true(app->animation.active);g_assert_cmpuint(app->game.actions,==,0);
    app->animation.started=g_get_monotonic_time()-750000;pause_clicked(NULL,app);double position=animation_frame(&app->animation,G_MAXINT64).position;pump(30);
    g_assert_cmpfloat(animation_frame(&app->animation,G_MAXINT64).position,==,position);g_assert_cmpuint(app->game.black,==,0);
    configure_clicked(NULL,app);g_assert_false(app->animation.active);g_assert_cmpint(app->draft,==,-1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0);begin_clicked(NULL,app);play_human(app,0);rotate_human(app,0);
    g_assert_cmpuint(app->game.black,==,SQUARE_BIT(2));g_assert_cmpuint(app->actions,==,1);g_assert_cmpint(app->game.turn,==,-1);
    undo_clicked(NULL,app);g_assert_cmpuint(app->game.black,==,0);g_assert_true(app->paused);pause_clicked(NULL,app);
    double scale,x,y,cx,cy;board_transform(app,&scale,&x,&y);center(6,&cx,&cy);click(NULL,1,x+cx*scale,y+cy*scale,app);g_assert_cmpint(app->draft,==,6);
    key_pressed(NULL,GDK_KEY_A,0,GDK_SHIFT_MASK,app);g_assert_cmpuint(app->game.black,==,SQUARE_BIT(13));
    undo_clicked(NULL,app);pause_clicked(NULL,app);app->cursor=0;key_pressed(NULL,GDK_KEY_Right,0,0,app);key_pressed(NULL,GDK_KEY_Return,0,0,app);g_assert_cmpint(app->draft,==,1);
    restart_clicked(NULL,app);g_assert_cmpint(app->draft,==,-1);g_assert_cmpuint(app->game.actions,==,0);
    /* Placement-only wins never ask for a rotation and persist the final board. */
    app->game.black=15;app->game.white=SQUARE_BIT(30)|SQUARE_BIT(32)|SQUARE_BIT(34);play_human(app,4);
    g_assert_cmpint(game_result(&app->game),==,GAME_BLACK);g_assert_true(app->saved);g_assert_false(app->animation.active);
    restart_clicked(NULL,app);app->game.black=G_GUINT64_CONSTANT(0x5cfc34686);app->game.white=G_GUINT64_CONSTANT(0xa303cb979);app->game.actions=36;
    g_assert_cmpint(game_result(&app->game),==,GAME_DRAW);save_attempt(app);update(app);g_assert_true(app->saved);
    g_autofree char *history=NULL;g_assert_true(g_file_get_contents(GAME_HISTORY_FILE,&history,NULL,NULL));g_assert_nonnull(strstr(history,"result: draw"));g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(app->status)),"Draw!"));
}
static void automatic(App *app,gconstpointer data)
{
    (void)data;gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[0]),PLAYER_MINIMAX);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[1]),PLAYER_ALPHA_BETA);
    for(unsigned i=0;i<2;i++)gtk_drop_down_set_selected(GTK_DROP_DOWN(app->presets[i]),0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0);begin_clicked(NULL,app);pause_clicked(NULL,app);settle_worker(app);
    g_assert_true(app->ready);g_assert_cmpuint(app->game.actions,==,0);pause_clicked(NULL,app);
    gint64 end=g_get_monotonic_time()+10000000;while(!game_result(&app->game) && g_get_monotonic_time()<end)pump(2);
    g_assert_cmpint(game_result(&app->game),!=,0);g_assert_true(app->saved);g_assert_cmpuint(game_count(app->game.black|app->game.white),==,app->game.actions);
    g_autofree char *history=NULL;g_assert_true(g_file_get_contents(GAME_HISTORY_FILE,&history,NULL,NULL));g_assert_nonnull(strstr(history,"Minimax (plain)"));g_assert_nonnull(strstr(history,"Minimax (alpha-beta)"));g_assert_nonnull(strstr(history,"cutoffs:"));
    Game before=app->game;pump(30);g_assert_cmpmem(&before,sizeof before,&app->game,sizeof app->game);
}
static void retry_translation(App *app,gconstpointer data)
{
    (void)data;gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[0]),PLAYER_MINIMAX);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->seconds[0]),1e-12);
    begin_clicked(NULL,app);settle_worker(app);g_assert_true(app->failed);g_assert_cmpuint(app->game.actions,==,0);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->presets[0]),0);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->depth[1]),4);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->seconds[1]),7);retry_clicked(NULL,app);pause_clicked(NULL,app);settle_worker(app);g_assert_false(app->failed);g_assert_true(app->ready);g_assert_cmpuint(app->options[1].depth,==,4);g_assert_cmpfloat(app->options[1].seconds,==,7);
    g_assert_cmpuint(gtk_drop_down_get_selected(GTK_DROP_DOWN(app->presets[1])),==,2);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language),I18N_PORTUGUESE);g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(app->undo)),==,"Desfazer");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(app->status)),"Pretas"));
    g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->depth[1])),==,4);g_assert_cmpfloat(gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->seconds[1])),==,7);g_assert_cmpfloat(gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->seconds[0])),==,5);g_assert_cmpuint(app->game.actions,==,0);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language),I18N_ENGLISH);configure_clicked(NULL,app);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[0]),PLAYER_MINIMAX);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->depth[0]),6);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->seconds[0]),0);
    begin_clicked(NULL,app);configure_clicked(NULL,app);humans(app);begin_clicked(NULL,app);settle_worker(app);g_assert_cmpuint(app->game.actions,==,0);g_assert_false(app->ready);g_assert_false(app->failed);
}
static void stable_messages(App *app,gconstpointer data)
{
    (void)data;humans(app);begin_clicked(NULL,app);pump(50);int height=gtk_widget_get_height(app->board),width=gtk_widget_get_width(app->board);
    GString *long_message=g_string_new(NULL);for(unsigned i=0;i<100;i++)g_string_append(long_message,"A long diagnostic message that wraps and overflows the reserved text panel.\n");
    set_error(app,long_message->str);pump(80);g_assert_cmpint(gtk_widget_get_height(app->board),==,height);g_assert_cmpint(gtk_widget_get_width(app->board),==,width);
    GtkScrolledWindow *scroll=GTK_SCROLLED_WINDOW(gtk_widget_get_parent(gtk_widget_get_parent(gtk_widget_get_parent(app->status))));
    GtkAdjustment *adjustment=gtk_scrolled_window_get_vadjustment(scroll);g_assert_cmpfloat(gtk_adjustment_get_upper(adjustment),>,gtk_adjustment_get_page_size(adjustment));
    set_error(app,NULL);gtk_label_set_text(GTK_LABEL(app->status),long_message->str);pump(50);g_assert_cmpint(gtk_widget_get_height(app->board),==,height);g_string_free(long_message,TRUE);
}
static void ollama_lifecycle(App *app,gconstpointer data)
{
    (void)data;Fixture fixture={0};setup(&fixture,NULL);if(!fixture.url){teardown(&fixture,NULL);return;}
    g_free(app->url);app->url=g_strdup(fixture.url);fixture.body="{\"models\":[{\"name\":\"mock:1b\"}]}";refresh_clicked(NULL,app);settle_worker(app);
    g_assert_nonnull(gtk_drop_down_get_selected_item(GTK_DROP_DOWN(app->models[0])));humans(app);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[0]),PLAYER_OLLAMA);fixture.auto_move=TRUE;fixture.delayed=TRUE;begin_clicked(NULL,app);
    gint64 end=g_get_monotonic_time()+2000000;while(!fixture.paused->len && g_get_monotonic_time()<end)pump(1);g_assert_cmpuint(fixture.paused->len,==,1);
    configure_clicked(NULL,app);humans(app);begin_clicked(NULL,app);settle_worker(app);g_assert_cmpuint(app->game.actions,==,0);g_assert_null(app->error);
    for(unsigned i=0;i<fixture.paused->len;i++)soup_server_message_unpause(g_ptr_array_index(fixture.paused,i));
    g_ptr_array_set_size(fixture.paused,0);
    fixture.delayed=FALSE;fixture.auto_move=FALSE;fixture.body="{\"done\":true,\"response\":\"bad\"}";
    configure_clicked(NULL,app);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[0]),PLAYER_OLLAMA);begin_clicked(NULL,app);settle_worker(app);g_assert_true(app->failed);g_assert_cmpuint(app->game.actions,==,0);ui_assert_ollama_transcript(app->board);teardown(&fixture,NULL);
}
static void stable_text_panels(App *app,gconstpointer data)
{
    (void)data;ui_assert_stable_text_panels(app->board);
}
int main(int argc,char **argv)
{
    g_setenv("OLLAMA_LOG_FILE","/dev/null",TRUE);g_setenv("OLLAMA_ERROR_LOG_FILE","/dev/null",TRUE);g_test_init(&argc,&argv,NULL);gtk_init();adw_init();
    test_directory=g_dir_make_tmp("pentago-ui-XXXXXX",NULL);g_assert_nonnull(test_directory);g_autofree char *old=g_get_current_dir();g_chdir(test_directory);
    g_test_add("/ui/fixed-text-panels",App,NULL,ui_setup,stable_text_panels,ui_teardown);
    g_test_add("/ui/draft-atomic-rotation-undo",App,NULL,ui_setup,human_animation,ui_teardown);
    g_test_add("/ui/plain-alpha-beta",App,NULL,ui_setup,automatic,ui_teardown);
    g_test_add("/ui/retry-language-cancel",App,NULL,ui_setup,retry_translation,ui_teardown);
    g_test_add("/ui/stable-message-panel",App,NULL,ui_setup,stable_messages,ui_teardown);
    g_test_add("/ui/ollama-lifecycle",App,NULL,ui_setup,ollama_lifecycle,ui_teardown);int result=g_test_run();
    g_remove(GAME_HISTORY_FILE);g_remove("settings.ini");g_chdir(old);g_rmdir(test_directory);g_free(test_directory);return result;
}
