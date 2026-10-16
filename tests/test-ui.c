/* Exercise actual controllers and timers with a registered GTK application. */
#include <glib/gstdio.h>
#define main application_main
#include "../src/main.c"
#undef main

static void drain_events(void)
{
    for (unsigned i = 0; i < 100 && g_main_context_pending(NULL); i++)
        g_main_context_iteration(NULL, FALSE);
}

static void test_controls(void)
{
    App app = {0};
    app.delay = 3;
    app.random = g_rand_new_with_seed(42);
    app.history = g_string_new(NULL);
    GError *error = NULL;
    char *directory = g_dir_make_tmp("jogo-da-velha-ui-XXXXXX", &error);
    g_assert_no_error(error);
    app.history_path = g_build_filename(directory, "histórico.md", NULL);
    app.application = GTK_APPLICATION(adw_application_new("io.github.aigames.JogoDaVelha.Test",
                                                         G_APPLICATION_NON_UNIQUE));
    g_assert_true(g_application_register(G_APPLICATION(app.application), NULL, &error));
    g_assert_no_error(error);
    activate(app.application, &app);
    drain_events();

    /* Human as O: X is automatic, keyboard cannot bypass its delay. */
    gtk_drop_down_set_selected(app.humans, 1);
    gtk_drop_down_set_selected(app.side, 1);
    begin_clicked(NULL, &app);
    g_assert_false(app.human_x);
    g_assert_true(app.human_o);
    g_assert_cmpuint(app.source, >, 0);
    g_assert_false(key_pressed(NULL, GDK_KEY_7, 0, 0, &app));
    g_assert_cmpuint(app.game.moves, ==, 0);
    guint source = app.source;
    gtk_range_set_value(GTK_RANGE(app.scale), .1);
    g_assert_cmpfloat(app.delay, ==, .1);
    g_assert_cmpuint(app.source, !=, source);
    gint64 due = g_get_monotonic_time() + 500000;
    while (app.game.moves == 0 && g_get_monotonic_time() < due)
        g_main_context_iteration(NULL, TRUE);
    g_assert_cmpuint(app.game.moves, ==, 1);
    g_assert_cmpuint(app.source, ==, 0);
    g_assert_true(human_turn(&app));

    /* Both humans: exact numeric-keypad layout and ignored occupied cells. */
    configure_clicked(NULL, &app);
    gtk_drop_down_set_selected(app.humans, 2);
    begin_clicked(NULL, &app);
    g_assert_true(app.human_x && app.human_o);
    g_assert_cmpuint(app.history->len, ==, 0);
    g_assert_true(key_pressed(NULL, GDK_KEY_KP_7, 0, 0, &app));
    g_assert_cmpint(app.game.cells[0][0], ==, 1);
    g_assert_true(key_pressed(NULL, GDK_KEY_7, 0, 0, &app));
    g_assert_cmpuint(app.game.moves, ==, 1);
    key_pressed(NULL, GDK_KEY_KP_4, 0, 0, &app);
    key_pressed(NULL, GDK_KEY_8, 0, 0, &app);
    key_pressed(NULL, GDK_KEY_KP_5, 0, 0, &app);
    key_pressed(NULL, GDK_KEY_KP_Page_Up, 0, 0, &app);
    g_assert_cmpint(game_result(&app.game), ==, 1);
    g_assert_cmpuint(app.source, >, 0);
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(app.status)), "X venceu"));
    g_assert_true(g_file_test(app.history_path, G_FILE_TEST_EXISTS));
    g_assert_false(key_pressed(NULL, GDK_KEY_1, 0, 0, &app));
    guint result_source = app.source;
    gtk_range_set_value(GTK_RANGE(app.scale), 0);
    g_assert_cmpuint(app.source, ==, result_source); /* end delay is always 3 s */
    gint64 end_started = g_get_monotonic_time();
    while (game_result(&app.game))
        g_main_context_iteration(NULL, TRUE);
    g_assert_cmpint(g_get_monotonic_time() - end_started, >=, 2800000);
    g_assert_cmpuint(app.game.moves, ==, 0);
    g_assert_cmpuint(app.history->len, ==, 0);
    g_assert_true(app.human_x && app.human_o);

    /* Draw is discarded. */
    const guint draw_keys[] = {7,8,9,5,4,6,2,1,3};
    GStatBuf before, after;
    g_assert_cmpint(g_stat(app.history_path, &before), ==, 0);
    for (unsigned i = 0; i < G_N_ELEMENTS(draw_keys); i++)
        key_pressed(NULL, GDK_KEY_0 + draw_keys[i], 0, 0, &app);
    g_assert_cmpint(game_result(&app.game), ==, 2);
    g_assert_cmpint(g_stat(app.history_path, &after), ==, 0);
    g_assert_cmpint(before.st_size, ==, after.st_size);

    /* Actual board geometry determines mouse hit testing, including margins. */
    configure_clicked(NULL, &app);
    begin_clicked(NULL, &app);
    drain_events();
    double width = gtk_widget_get_width(app.board), height = gtk_widget_get_height(app.board);
    double size = MIN(width, height);
    g_assert_cmpfloat(size, >, 0);
    board_clicked(NULL, 1, -1, -1, &app);
    g_assert_cmpuint(app.game.moves, ==, 0);
    board_clicked(NULL, 1, width / 2, height / 2, &app);
    g_assert_cmpint(app.game.cells[1][1], ==, 1);

    /* Error queue survives reset and retry writes it once. */
    char *real_path = app.history_path;
    app.history_path = g_strdup(directory); /* opening a directory fails */
    g_queue_push_tail(&app.pending_reports, g_strdup("\nPending report\n"));
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

    /* Zero-delay automation yields to the loop and works with cell 0 occupied. */
    configure_clicked(NULL, &app);
    gtk_drop_down_set_selected(app.humans, 0);
    begin_clicked(NULL, &app);
    g_assert_false(app.human_x || app.human_o);
    g_assert_cmpuint(app.game.moves, ==, 0);
    while (!game_result(&app.game))
        g_main_context_iteration(NULL, TRUE);
    g_assert_cmpuint(app.game.moves, >=, 5);
    configure_clicked(NULL, &app);
    g_assert_cmpuint(app.source, ==, 0);
    g_assert_false(app.playing);
    begin_clicked(NULL, &app);
    g_assert_cmpuint(app.source, >, 0);
    gtk_window_destroy(app.window);
    drain_events();
    g_assert_cmpuint(app.source, ==, 0);
    g_assert_null(app.window);

    g_object_unref(app.application);
    g_string_free(app.history, TRUE);
    g_rand_free(app.random);
    g_remove(app.history_path);
    g_rmdir(directory);
    g_free(directory);
    g_free(app.history_path);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ui/controllers-timers-history", test_controls);
    return g_test_run();
}
