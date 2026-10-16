#include <glib/gstdio.h>
#define main application_main
#include "../src/main.c"
#undef main

static void drain_events(void)
{
    for (unsigned i = 0; i < 100 && g_main_context_pending(NULL); i++)
        g_main_context_iteration(NULL, FALSE);
}

static void wait_for_moves(App *app, unsigned minimum, gint64 timeout_us)
{
    gint64 end = g_get_monotonic_time() + timeout_us;
    while (app->game.moves < minimum && g_get_monotonic_time() < end) {
        drain_events();
        g_usleep(1000);
    }
    g_assert_cmpuint(app->game.moves, >=, minimum);
}

static void test_controls(void)
{
    App app = {0};
    app.delay = 3;
    app.random = g_rand_new_with_seed(42);
    app.history = g_string_new(NULL);
    GError *error = NULL;
    char *directory = g_dir_make_tmp("connect-4-ui-XXXXXX", &error);
    g_assert_no_error(error);
    app.history_path = g_build_filename(directory, "histórico-connect-4.md", NULL);
    app.application = GTK_APPLICATION(adw_application_new("io.github.aigames.Connect4.Test",
                                                         G_APPLICATION_NON_UNIQUE));
    g_assert_true(g_application_register(G_APPLICATION(app.application), NULL, &error));
    g_assert_no_error(error);
    activate(app.application, &app);
    drain_events();

    gtk_drop_down_set_selected(app.humans, 1);
    gtk_drop_down_set_selected(app.side, 1);
    begin_clicked(NULL, &app);
    g_assert_false(app.human_red);
    g_assert_true(app.human_yellow);
    g_assert_cmpuint(app.source, >, 0);
    g_assert_false(key_pressed(NULL, GDK_KEY_1, 0, 0, &app));
    guint source = app.source;
    gtk_range_set_value(GTK_RANGE(app.scale), .1);
    g_assert_cmpuint(app.source, !=, source);
    wait_for_moves(&app, 1, 1000000);
    g_assert_cmpuint(app.game.moves, ==, 1);
    g_assert_cmpuint(app.source, ==, 0);
    g_assert_true(human_turn(&app));
    configure_clicked(NULL, &app);
    g_assert_cmpuint(app.source, ==, 0);

    gtk_drop_down_set_selected(app.humans, 2);
    begin_clicked(NULL, &app);
    g_assert_true(app.human_red && app.human_yellow);
    g_assert_cmpuint(app.history->len, ==, 0);
    for (unsigned i = 0; i < 6; i++)
        g_assert_true(key_pressed(NULL, GDK_KEY_KP_1, 0, 0, &app));
    g_assert_cmpuint(app.game.moves, ==, 6);
    g_assert_cmpint(app.game.cells[5][0], ==, 1);
    g_assert_cmpint(app.game.cells[0][0], ==, -1);
    gsize length = app.history->len;
    key_pressed(NULL, GDK_KEY_KP_End, 0, 0, &app);
    g_assert_cmpuint(app.history->len, ==, length);
    g_assert_cmpuint(app.game.moves, ==, 6);
    g_assert_false(key_pressed(NULL, GDK_KEY_8, 0, 0, &app));

    configure_clicked(NULL, &app);
    begin_clicked(NULL, &app);
    const guint winning_keys[] = {1,7,2,7,3,6,4};
    for (unsigned i = 0; i < G_N_ELEMENTS(winning_keys); i++)
        key_pressed(NULL, GDK_KEY_0 + winning_keys[i], 0, 0, &app);
    g_assert_cmpint(game_result(&app.game), ==, 1);
    g_assert_cmpuint(game_winning_lines(&app.game, NULL), ==, 1);
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(app.status)), "Vermelho venceu"));
    g_assert_true(g_file_test(app.history_path, G_FILE_TEST_EXISTS));
    g_assert_false(key_pressed(NULL, GDK_KEY_1, 0, 0, &app));
    guint result_source = app.source;
    gtk_range_set_value(GTK_RANGE(app.scale), 0);
    g_assert_cmpuint(app.source, ==, result_source);
    gint64 start = g_get_monotonic_time(), deadline = start + 5000000;
    while (game_result(&app.game) && g_get_monotonic_time() < deadline) {
        drain_events();
        g_usleep(1000);
    }
    g_assert_cmpuint(app.game.moves, ==, 0);
    g_assert_cmpint(g_get_monotonic_time() - start, >=, 2800000);
    g_assert_cmpuint(app.history->len, ==, 0);
    g_assert_true(app.human_red && app.human_yellow);

    /* Draws never append; result still waits before restarting. */
    const unsigned draw[] = {6,6,3,4,5,5,0,4,1,2,3,1,1,1,5,5,6,2,6,3,3,
                            2,6,6,0,5,1,3,2,2,5,1,3,4,4,0,2,0,0,0,4,4};
    GStatBuf before, after;
    g_assert_cmpint(g_stat(app.history_path, &before), ==, 0);
    for (unsigned i = 0; i < G_N_ELEMENTS(draw); i++)
        play_column(&app, draw[i]);
    g_assert_cmpint(game_result(&app.game), ==, 2);
    g_assert_cmpint(g_stat(app.history_path, &after), ==, 0);
    g_assert_cmpint(before.st_size, ==, after.st_size);

    configure_clicked(NULL, &app);
    begin_clicked(NULL, &app);
    drain_events();
    BoardGeometry b = board_geometry(gtk_widget_get_width(app.board), gtk_widget_get_height(app.board));
    g_assert_cmpfloat(b.unit, >, 0);
    board_clicked(NULL, 1, -1, -1, &app);
    g_assert_cmpuint(app.game.moves, ==, 0);
    board_clicked(NULL, 1, b.left + 6.5 * b.unit, b.top + .5 * b.unit, &app);
    g_assert_cmpint(app.game.cells[5][6], ==, 1);
    board_clicked(NULL, 1, b.left + 6.5 * b.unit, b.top + 4.5 * b.unit, &app);
    g_assert_cmpint(app.game.cells[4][6], ==, -1);

    char *real_path = app.history_path;
    app.history_path = g_strdup(directory);
    g_queue_push_tail(&app.pending_reports, g_strdup("\nPending Lig-4 report\n"));
    flush_reports(&app);
    g_assert_cmpuint(app.pending_reports.length, ==, 1);
    g_assert_true(gtk_widget_get_visible(app.error_box));
    configure_clicked(NULL, &app);
    begin_clicked(NULL, &app);
    g_assert_cmpuint(app.pending_reports.length, ==, 1);
    g_free(app.history_path);
    app.history_path = real_path;
    retry_save(NULL, &app);
    g_assert_true(g_queue_is_empty(&app.pending_reports));
    g_assert_false(gtk_widget_get_visible(app.error_box));

    configure_clicked(NULL, &app);
    gtk_drop_down_set_selected(app.humans, 0);
    begin_clicked(NULL, &app);
    g_assert_false(app.human_red || app.human_yellow);
    g_assert_cmpuint(app.game.moves, ==, 0);
    deadline = g_get_monotonic_time() + 5000000;
    while (!game_result(&app.game) && g_get_monotonic_time() < deadline) {
        drain_events();
        g_usleep(1000);
    }
    g_assert_cmpint(game_result(&app.game), !=, 0);
    g_assert_cmpuint(app.game.moves, >=, 7);
    configure_clicked(NULL, &app);
    g_assert_false(app.playing);
    g_assert_cmpuint(app.source, ==, 0);
    begin_clicked(NULL, &app);
    g_assert_cmpuint(app.source, >, 0);
    gtk_window_destroy(app.window);
    drain_events();
    g_assert_null(app.window);
    g_assert_cmpuint(app.source, ==, 0);

    g_object_unref(app.application);
    g_string_free(app.history, TRUE);
    g_rand_free(app.random);
    g_remove(app.history_path);
    g_rmdir(directory);
    g_free(directory);
    g_free(app.history_path);
}

static void test_geometry(void)
{
    const double sizes[][2] = {{1200,500}, {400,900}, {700,645}, {70,80}};
    for (unsigned i = 0; i < G_N_ELEMENTS(sizes); i++) {
        BoardGeometry b = board_geometry(sizes[i][0], sizes[i][1]);
        g_assert_cmpfloat(b.left, >=, 0);
        g_assert_cmpfloat(b.top - .45 * b.unit, >=, -.000001);
        g_assert_cmpfloat(b.left + 7 * b.unit, <=, sizes[i][0] + .000001);
        g_assert_cmpfloat(b.top + 6 * b.unit, <=, sizes[i][1] + .000001);
    }
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ui/geometry", test_geometry);
    g_test_add_func("/ui/controllers-timers-history", test_controls);
    return g_test_run();
}
