// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-fixture.h"
#include "ollama-server.h"
static void human_animation(App *app, gconstpointer data)
{
    (void) data;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay), 1);
    begin_clicked(NULL, app);
    select_point(app, game_moves[0].entry); g_assert_cmpint(app->selected, ==, (int) game_moves[0].entry);
    select_point(app, game_moves[0].destination); g_assert_true(app->animation.active);
    g_assert_cmpuint(app->game.moves, ==, 0);
    select_point(app, 1); g_assert_cmpint(app->pending, ==, 0);
    animation_pause(&app->animation, app->animation.started + 250000);
    AnimationFrame frame = animation_frame(&app->animation, G_MAXINT64);
    g_assert_cmpfloat(frame.position, ==, 0);
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1100, 850); pump(30);
    g_assert_cmpuint(app->game.moves, ==, 0);
    double scale, x, y; board_transform(app, &scale, &x, &y);
    g_assert_cmpfloat(scale, >, 0);
    restart_clicked(NULL, app); g_assert_false(app->animation.active); pump(30);
    g_assert_cmpuint(app->game.moves, ==, 0);
    select_point(app, game_moves[0].entry); select_point(app, game_moves[0].destination);
    app->animation.started = g_get_monotonic_time() - 750000;
    animation_pause(&app->animation, g_get_monotonic_time());
    frame = animation_frame(&app->animation, G_MAXINT64);
    g_assert_cmpfloat(frame.position, >, .4); g_assert_cmpfloat(frame.position, <, .7);
    configure_clicked(NULL, app); g_assert_false(app->animation.active); pump(30);
    g_assert_cmpuint(app->game.moves, ==, 0);
    begin_clicked(NULL, app); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay), 0);
    select_point(app, game_moves[0].entry); select_point(app, game_moves[0].destination);
    g_assert_cmpuint(app->game.moves, ==, 1); g_assert_false(app->animation.active); pump(30);
    g_assert_cmpuint(app->game.moves, ==, 1);
    undo_clicked(NULL, app); g_assert_cmpuint(app->game.moves, ==, 0); g_assert_cmpuint(app->game.occupied, ==, 0);
    select_point(app, game_moves[0].entry); key_pressed(NULL, GDK_KEY_Escape, 0, 0, app); g_assert_cmpint(app->selected, ==, -1);
    /* Pointer coordinates use the same centered square after resize. */
    board_transform(app, &scale, &x, &y);
    click(NULL, 1, x + board_points[game_moves[0].entry].x * scale, y + board_points[game_moves[0].entry].y * scale, app);
    g_assert_cmpint(app->selected, ==, (int) game_moves[0].entry);
}
static void automatic(App *app, gconstpointer data)
{
    (void) data;
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller), PLAYER_SEARCH);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay), .1);
    begin_clicked(NULL, app); pause_clicked(NULL, app);
    settle_worker(app);
    g_assert_true(app->ready); g_assert_cmpuint(app->game.moves, ==, 0);
    pause_clicked(NULL, app); g_assert_true(app->animation.active);
    app->animation.started = g_get_monotonic_time() - 75000;
    pause_clicked(NULL, app);
    AnimationFrame before = animation_frame(&app->animation, g_get_monotonic_time());
    pump(100);
    AnimationFrame after = animation_frame(&app->animation, g_get_monotonic_time());
    g_assert_cmpfloat(before.position, ==, after.position); g_assert_cmpuint(app->game.moves, ==, 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay), 0);
    g_assert_cmpint(app->animation.duration, ==, 100000); /* Current delay is frozen. */
    pause_clicked(NULL, app);
    gint64 end = g_get_monotonic_time() + 5000000;
    while (!game_result(&app->game) && g_get_monotonic_time() < end) pump(5);
    g_assert_cmpint(game_result(&app->game), ==, 1); g_assert_cmpuint(app->game.moves, ==, GAME_MAX_DEPTH);
    pump(100); g_assert_cmpuint(app->game.moves, ==, GAME_MAX_DEPTH); g_assert_true(app->saved);
    g_autofree char *history = NULL;
    g_assert_true(g_file_get_contents(GAME_HISTORY_FILE, &history, NULL, NULL));
    g_assert_nonnull(strstr(history, "result: solved")); g_assert_nonnull(strstr(history, "Search depth:"));
    restart_clicked(NULL, app);
    configure_clicked(NULL, app); settle_worker(app);
    g_assert_cmpuint(app->game.moves, ==, 0); g_assert_false(app->playing);
}
static void translations(App *app, gconstpointer data)
{
    (void) data;
    begin_clicked(NULL, app); select_point(app, game_moves[0].entry);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language), I18N_PORTUGUESE);
    g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(app->undo)), ==, "Desfazer");
    g_assert_cmpint(app->selected, ==, (int) game_moves[0].entry); g_assert_cmpuint(app->game.moves, ==, 0);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language), I18N_ENGLISH);
    g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(app->undo)), ==, "Undo");
}
static void ollama_lifecycle(App *app, gconstpointer data)
{
    (void) data;
    Fixture fixture = {0}; setup(&fixture, NULL); if (!fixture.url) { teardown(&fixture, NULL); return; }
    g_free(app->url); app->url = g_strdup(fixture.url);
    fixture.body = "{\"models\":[{\"name\":\"mock:1b\"}]}";
    refresh_clicked(NULL, app); settle_worker(app);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller), PLAYER_OLLAMA);
    /* Cancel while exact assistance is still computing, before any model call. */
    gtk_check_button_set_active(GTK_CHECK_BUTTON(app->assisted), TRUE);
    unsigned generation_calls = fixture.generate_calls;
    begin_clicked(NULL, app); pump(10);
    g_assert_nonnull(app->task);
    configure_clicked(NULL, app);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller), PLAYER_HUMAN);
    begin_clicked(NULL, app); settle_worker(app);
    g_assert_cmpuint(fixture.generate_calls, ==, generation_calls);
    g_assert_cmpuint(app->game.moves, ==, 0); g_assert_null(app->error);
    configure_clicked(NULL, app);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller), PLAYER_OLLAMA);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(app->assisted), FALSE);
    fixture.auto_move = TRUE; fixture.delayed = TRUE;
    begin_clicked(NULL, app);
    gint64 end = g_get_monotonic_time() + 2000000;
    while (!fixture.paused->len && g_get_monotonic_time() < end) pump(1);
    g_assert_cmpuint(fixture.paused->len, ==, 1);
    configure_clicked(NULL, app);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller), PLAYER_HUMAN);
    begin_clicked(NULL, app); settle_worker(app);
    g_assert_cmpuint(app->game.moves, ==, 0); g_assert_null(app->error);
    for (unsigned i = 0; i < fixture.paused->len; i++) soup_server_message_unpause(g_ptr_array_index(fixture.paused, i));
    g_ptr_array_set_size(fixture.paused, 0); fixture.delayed = FALSE;
    configure_clicked(NULL, app);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller), PLAYER_OLLAMA);
    fixture.auto_move = FALSE; fixture.body = "{\"done\":true,\"response\":\"bad move\"}";
    begin_clicked(NULL, app); settle_worker(app);
    g_assert_true(app->failed); g_assert_nonnull(app->error); g_assert_cmpuint(app->game.moves, ==, 0);
    teardown(&fixture, NULL);
}
#include "test-end-pause.inc"
int main(int argc, char **argv)
{
    g_setenv("OLLAMA_LOG_FILE", "/dev/null", TRUE); g_setenv("OLLAMA_ERROR_LOG_FILE", "/dev/null", TRUE);
    g_test_init(&argc, &argv, NULL); gtk_init(); adw_init();
    GError *error = NULL;
    test_directory = g_dir_make_tmp("heptalpha-ui-XXXXXX", &error); g_assert_no_error(error);
    g_autofree char *old_directory = g_get_current_dir(); g_assert_cmpint(g_chdir(test_directory), ==, 0);
    g_test_add("/ui/human-animation-reset-undo", App, NULL, ui_setup, human_animation, ui_teardown);
    g_test_add("/ui/search-pause-solve", App, NULL, ui_setup, automatic, ui_teardown);
    g_test_add("/ui/live-translations", App, NULL, ui_setup, translations, ui_teardown);
    g_test_add("/ui/ollama-cancellation-error", App, NULL, ui_setup, ollama_lifecycle, ui_teardown);
    g_test_add("/ui/pause-at-end",App,NULL,ui_setup,end_pause,ui_teardown);
    int status = g_test_run();
    g_remove(GAME_HISTORY_FILE); g_remove("settings.ini"); g_chdir(old_directory); g_rmdir(test_directory); g_free(test_directory);
    return status;
}
