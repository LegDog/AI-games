// SPDX-License-Identifier: GPL-3.0-or-later
#include <glib/gstdio.h>
#define main application_main
#include "../src/main.c"
#undef main
#include "ollama-server.h"

static void drain_events(void)
{
    for (unsigned i = 0; i < 100 && g_main_context_pending(NULL); i++)
        g_main_context_iteration(NULL, FALSE);
}

static void wait_job(App *app)
{
    gint64 deadline = g_get_monotonic_time() + 5000000;
    while (app->task && g_get_monotonic_time() < deadline) {
        drain_events();
        g_usleep(1000);
    }
    g_assert_null(app->task);
}

static void wait_moves(App *app, unsigned count)
{
    gint64 deadline = g_get_monotonic_time() + 5000000;
    while (app->game.moves < count && g_get_monotonic_time() < deadline) {
        drain_events();
        g_usleep(1000);
    }
    g_assert_cmpuint(app->game.moves, ==, count);
}

static void select_players(App *app, PlayerKind first, PlayerKind second)
{
    configure_clicked(NULL, app);
    gtk_drop_down_set_selected(app->controllers[0], first);
    gtk_drop_down_set_selected(app->controllers[1], second);
    gtk_range_set_value(GTK_RANGE(app->depth_scales[0]), 2);
    gtk_range_set_value(GTK_RANGE(app->depth_scales[1]), 2);
    begin_clicked(NULL, app);
}

static void test_controls(void)
{
    App app = {0};
    app.delay = 3;
    app.random = g_rand_new_with_seed(42);
    app.history = g_string_new(NULL);
    GError *error = NULL;
    char *directory = g_dir_make_tmp("tic-tac-toe-ui-XXXXXX", &error);
    g_assert_no_error(error);
    app.history_path = g_build_filename(directory, GAME_HISTORY_FILE, NULL);
    app.application = GTK_APPLICATION(adw_application_new("io.github.aigames.TicTacToe.Test", G_APPLICATION_NON_UNIQUE));
    g_assert_true(g_application_register(G_APPLICATION(app.application), NULL, &error));
    g_assert_no_error(error);
    activate(app.application, &app);
    drain_events();
    g_assert_cmpuint(gtk_drop_down_get_selected(app.controllers[0]), ==, PLAYER_HUMAN);
    g_assert_cmpuint(gtk_drop_down_get_selected(app.controllers[1]), ==, PLAYER_POSITIONAL);
    for (unsigned i = 0; i < 2; i++) {
        g_assert_false(gtk_widget_get_visible(app.depth_rows[i]));
        g_assert_cmpfloat(gtk_range_get_value(GTK_RANGE(app.depth_scales[i])), ==, DEFAULT_MINIMAX_DEPTH);
    }
    for (unsigned first = 0; first < 3; first++) {
        for (unsigned second = 0; second < 3; second++) {
            select_players(&app, first, second);
            g_assert_cmpint(app.players[0].kind, ==, first);
            g_assert_cmpint(app.players[1].kind, ==, second);
            g_assert_cmpint(human_turn(&app), ==, first == PLAYER_HUMAN);
            g_assert_cmpint(gtk_widget_get_visible(app.depth_rows[0]), ==, first == PLAYER_MINIMAX);
            g_assert_cmpint(gtk_widget_get_visible(app.depth_rows[1]), ==, second == PLAYER_MINIMAX);
            configure_clicked(NULL, &app);
            wait_job(&app);
        }
    }

    /* Calculation completes during the delay. A delay change keeps its result. */
    select_players(&app, PLAYER_MINIMAX, PLAYER_HUMAN);
    guint64 generation = app.generation;
    wait_job(&app);
    g_assert_true(app.ready);
    g_assert_cmpuint(app.game.moves, ==, 0);
    g_assert_true(app.prepared_minimax.opening_random);
    g_assert_false(key_pressed(NULL, GDK_KEY_1, 0, 0, &app));
    gtk_range_set_value(GTK_RANGE(app.scale), .1);
    g_assert_cmpuint(app.generation, ==, generation);
    g_assert_true(app.ready);
    wait_moves(&app, 1);
    g_assert_true(human_turn(&app));

    /* A real minimax search after a human move uses the selected depth. */
    select_players(&app, PLAYER_HUMAN, PLAYER_MINIMAX);
    gtk_range_set_value(GTK_RANGE(app.scale), 0);
    key_pressed(NULL, GDK_KEY_KP_1, 0, 0, &app);
    g_assert_cmpuint(app.game.moves, ==, 1);
    g_assert_false(key_pressed(NULL, GDK_KEY_2, 0, 0, &app));
    GTask *same_task = app.task;
    guint64 same_generation = app.generation;
    gtk_range_set_value(GTK_RANGE(app.scale), .1);
    gtk_range_set_value(GTK_RANGE(app.scale), 0);
    g_assert_true(app.task == same_task);
    g_assert_cmpuint(app.generation, ==, same_generation);
    wait_moves(&app, 2);
    g_assert_true(human_turn(&app));
    g_assert_nonnull(strstr(app.history->str, "(Minimax)"));
    g_assert_nonnull(strstr(app.history->str, "concluída: 2"));
    g_assert_nonnull(strstr(app.history->str, "Valor minimax"));

    /* Cancel a search, start another session before its callback, then ensure
     * only the new empty-board result is applied. One worker at a time. */
    select_players(&app, PLAYER_HUMAN, PLAYER_MINIMAX);
    gtk_range_set_value(GTK_RANGE(app.depth_scales[1]), GAME_MAX_DEPTH);
    app.players[1].depth = GAME_MAX_DEPTH;
    key_pressed(NULL, GDK_KEY_1, 0, 0, &app);
    GTask *old_task = app.task;
    g_assert_nonnull(old_task);
    select_players(&app, PLAYER_MINIMAX, PLAYER_HUMAN);
    g_assert_true(app.task == old_task);
    g_assert_cmpuint(app.job->generation, !=, app.generation);
    wait_moves(&app, 1);
    g_assert_true(human_turn(&app));
    g_assert_nonnull(strstr(app.history->str, "Abertura aleatória"));
    g_assert_null(strstr(app.history->str, "Jogada 2"));

    /* Human victory, result delay and restart keep the selected controllers. */
    select_players(&app, PLAYER_HUMAN, PLAYER_HUMAN);
    const guint keys[] = {7,4,8,5,9};
    for (unsigned i = 0; i < G_N_ELEMENTS(keys); i++)
        key_pressed(NULL, GDK_KEY_0 + keys[i], 0, 0, &app);
    g_assert_cmpint(game_result(&app.game), ==, 1);
    g_assert_true(g_file_test(app.history_path, G_FILE_TEST_EXISTS));
    guint result_source = app.source;
    gtk_range_set_value(GTK_RANGE(app.scale), .1);
    g_assert_cmpuint(app.source, ==, result_source);
    gint64 start = g_get_monotonic_time(), end = start + 5000000;
    while (game_result(&app.game) && g_get_monotonic_time() < end) {
        drain_events();
        g_usleep(1000);
    }
    g_assert_cmpuint(app.game.moves, ==, 0);
    g_assert_cmpuint(app.history->len, ==, 0);
    g_assert_cmpint(g_get_monotonic_time() - start, >=, 2800000);
    g_assert_cmpint(app.players[0].kind, ==, PLAYER_HUMAN);
    g_assert_cmpint(app.players[1].kind, ==, PLAYER_HUMAN);

    const unsigned draw[] = {0,1,2,4,3,5,7,6,8};
    GStatBuf before, after;
    g_assert_cmpint(g_stat(app.history_path, &before), ==, 0);
    for (unsigned i = 0; i < G_N_ELEMENTS(draw); i++)
        play_cell(&app, draw[i]);
    g_assert_cmpint(game_result(&app.game), ==, 2);
    g_assert_cmpint(g_stat(app.history_path, &after), ==, 0);
    g_assert_cmpint(before.st_size, ==, after.st_size);

    select_players(&app, PLAYER_HUMAN, PLAYER_HUMAN);
    drain_events();
    board_clicked(NULL, 1, -1, -1, &app);
    g_assert_cmpuint(app.game.moves, ==, 0);
    board_clicked(NULL, 1, gtk_widget_get_width(app.board) / 2., gtk_widget_get_height(app.board) / 2., &app);
    g_assert_cmpint(app.game.cells[1][1], ==, 1);

    char *real_path = app.history_path;
    app.history_path = g_strdup(directory);
    g_queue_push_tail(&app.pending_reports, g_strdup("\nPending report\n"));
    flush_reports(&app);
    g_assert_cmpuint(app.pending_reports.length, ==, 1);
    g_assert_true(gtk_widget_get_visible(app.error_box));
    select_players(&app, PLAYER_HUMAN, PLAYER_HUMAN);
    g_assert_cmpuint(app.pending_reports.length, ==, 1);
    g_free(app.history_path);
    app.history_path = real_path;
    retry_save(NULL, &app);
    g_assert_true(g_queue_is_empty(&app.pending_reports));
    g_assert_false(gtk_widget_get_visible(app.error_box));

    select_players(&app, PLAYER_HUMAN, PLAYER_MINIMAX);
    app.players[1].depth = GAME_MAX_DEPTH;
    key_pressed(NULL, GDK_KEY_1, 0, 0, &app);
    g_assert_nonnull(app.task);
    gtk_window_destroy(app.window);
    shutdown_jobs(&app);
    g_assert_null(app.window);
    g_assert_null(app.task);
    g_assert_cmpuint(app.source, ==, 0);
    g_assert_cmpuint(app.progress_source, ==, 0);
    g_object_unref(app.application);
    g_string_free(app.history, TRUE);
    g_rand_free(app.random);
    g_remove(app.history_path);
    g_rmdir(directory);
    g_free(directory);
    g_free(app.history_path);
}

typedef struct { gboolean done, expected_cancel; } WorkerTest;
static void worker_test_done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void) source;
    WorkerTest *test = data;
    GError *error = NULL;
    gboolean ok = g_task_propagate_boolean(G_TASK(result), &error);
    if (test->expected_cancel) {
        g_assert_false(ok);
        g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    } else {
        g_assert_true(ok);
        g_assert_no_error(error);
        TurnJob *job = g_task_get_task_data(G_TASK(result));
        g_assert_cmpint(job->chosen, >=, 0);
        g_assert_cmpuint(job->minimax.completed_depth, ==, 2);
        g_assert_cmpuint(job->game.moves, ==, 1); /* worker never edits snapshot */
        g_assert_cmpint(g_atomic_int_get(&job->progress), ==, 2);
    }
    g_clear_error(&error);
    test->done = TRUE;
}

/* GTask workers and cancellation can be checked without a graphical display. */
static void test_worker(void)
{
    for (unsigned cancel = 0; cancel < 2; cancel++) {
        TurnJob *job = g_new0(TurnJob, 1);
        game_init(&job->game);
        game_play(&job->game, 0);
        job->player = (PlayerConfig) {.kind = PLAYER_MINIMAX, .depth = 2};
        job->seed = 73;
        GCancellable *cancellable = g_cancellable_new();
        if (cancel) g_cancellable_cancel(cancellable);
        WorkerTest test = {.expected_cancel = cancel};
        GTask *task = g_task_new(NULL, cancellable, worker_test_done, &test);
        g_task_set_task_data(task, job, g_free);
        g_task_set_return_on_cancel(task, FALSE);
        g_task_run_in_thread(task, turn_worker);
        while (!test.done)
            g_main_context_iteration(NULL, TRUE);
        g_object_unref(task);
        g_object_unref(cancellable);
    }
}



static void test_stale_completion(void)
{
    for (unsigned cancel = 0; cancel < 2; cancel++) {
        App app = {0};
        game_init(&app.game);
        app.history = g_string_new("untouched");
        app.generation = 2;
        TurnJob *job = g_new0(TurnJob, 1);
        job->game = app.game;
        game_play(&job->game, 0);
        job->player = (PlayerConfig) {.kind = PLAYER_MINIMAX, .depth = 2};
        job->generation = 1;
        app.job = job;
        app.search_cancel = g_cancellable_new();
        app.task = g_task_new(NULL, app.search_cancel, turn_completed, &app);
        g_task_set_task_data(app.task, job, g_free);
        g_task_set_return_on_cancel(app.task, FALSE);
        g_task_run_in_thread(app.task, turn_worker);
        if (cancel) g_cancellable_cancel(app.search_cancel);
        wait_job(&app);
        g_assert_cmpuint(app.game.moves, ==, 0);
        g_assert_cmpstr(app.history->str, ==, "untouched");
        g_assert_false(app.ready);
        g_assert_null(app.job);
        g_assert_null(app.search_cancel);
        shutdown_jobs(&app);
        g_string_free(app.history, TRUE);
    }
}

static void wait_models(App *app)
{
    gint64 deadline = g_get_monotonic_time() + 5000000;
    while (app->models_task && g_get_monotonic_time() < deadline) {
        drain_events();
        g_usleep(1000);
    }
    g_assert_null(app->models_task);
}

static void test_ollama_controls(void)
{
    Fixture server = {0};
    setup(&server, NULL);
    if (!server.url) { teardown(&server, NULL); return; }
    server.body = "{\"models\":[{\"name\":\"z:1b\"},{\"name\":\"a:1b\"}]}";
    server.auto_move = TRUE;
    App app = {0};
    app.ollama_url = g_strdup(server.url);
    app.delay = 3;
    app.random = g_rand_new_with_seed(15);
    app.history = g_string_new(NULL);
    GError *error = NULL;
    char *directory = g_dir_make_tmp("ollama-ui-XXXXXX", &error);
    g_assert_no_error(error);
    app.history_path = g_build_filename(directory, "history.md", NULL);
    app.application = GTK_APPLICATION(adw_application_new("io.github.aigames.Ollama.Test", G_APPLICATION_NON_UNIQUE));
    g_assert_true(g_application_register(G_APPLICATION(app.application), NULL, &error));
    g_assert_no_error(error);
    activate(app.application, &app);
    gtk_drop_down_set_selected(app.controllers[0], PLAYER_OLLAMA);
    g_assert_false(gtk_widget_get_sensitive(app.begin_button));
    g_assert_nonnull(app.models_task);
    wait_models(&app);
    g_assert_true(app.models_valid);
    g_assert_true(gtk_widget_get_sensitive(app.begin_button));
    g_assert_cmpstr(selected_model(&app, 0), ==, "a:1b");
    gtk_drop_down_set_selected(app.models[1], 1);
    refresh_models_clicked(NULL, &app);
    wait_models(&app);
    g_assert_cmpstr(selected_model(&app, 0), ==, "a:1b");
    g_assert_cmpstr(selected_model(&app, 1), ==, "z:1b");
    for (unsigned i = 0; i < 2; i++) {
        g_assert_false(gtk_check_button_get_active(app.assist_checks[i]));
        g_assert_true(gtk_check_button_get_active(app.thinking_checks[i]));
    }
    gtk_check_button_set_active(app.assist_checks[0], TRUE);
    gtk_check_button_set_active(app.thinking_checks[0], FALSE);
    for (unsigned first = 0; first < 4; first++) {
        for (unsigned second = 0; second < 4; second++) {
            select_players(&app, first, second);
            g_assert_cmpint(app.players[0].kind, ==, first);
            g_assert_cmpint(app.players[1].kind, ==, second);
            g_assert_cmpint(gtk_widget_get_visible(app.model_rows[0]), ==, first == PLAYER_OLLAMA);
            g_assert_cmpint(gtk_widget_get_visible(app.model_rows[1]), ==, second == PLAYER_OLLAMA);
            g_assert_cmpint(gtk_widget_get_visible(app.depth_rows[0]), ==, first == PLAYER_MINIMAX);
            g_assert_cmpint(gtk_widget_get_visible(app.depth_rows[1]), ==, second == PLAYER_MINIMAX);
            if (first == PLAYER_OLLAMA) {
                g_assert_cmpstr(app.job->player.model, ==, "a:1b");
                g_assert_true(app.job->player.ollama_assisted);
                g_assert_false(app.job->player.ollama_thinking);
                g_assert_true(app.job->player.model != app.players[0].model);
            }
            g_assert_cmpint(gtk_widget_get_visible(app.ollama_options[0]), ==, first == PLAYER_OLLAMA);
            g_assert_cmpint(gtk_widget_get_visible(app.ollama_options[1]), ==, second == PLAYER_OLLAMA);
            wait_job(&app);
            g_assert_cmpuint(app.game.moves, ==, 0);
            g_assert_false(app.turn_failed);
        }
    }
    select_players(&app, PLAYER_OLLAMA, PLAYER_OLLAMA);
    wait_job(&app);
    for (unsigned i = 0; i < 2; i++) {
        cancel_timer(&app);
        automatic_move(&app);
        wait_job(&app);
        g_assert_cmpuint(app.game.moves, ==, i + 1);
    }
    g_assert_nonnull(strstr(app.history->str, "modelo: a:1b"));
    g_assert_nonnull(strstr(app.history->str, "modelo: z:1b"));
    g_assert_nonnull(strstr(app.history->str, "modo: assistido"));
    g_assert_nonnull(strstr(app.history->str, "modo: puro"));
    g_assert_nonnull(strstr(app.history->str, "(Ollama)"));
    /* A new game keeps both models and clears the in-memory history. */
    start_game(&app);
    wait_job(&app);
    g_assert_cmpuint(app.game.moves, ==, 0);
    g_assert_cmpuint(app.history->len, ==, 0);
    g_assert_cmpstr(app.players[0].model, ==, "a:1b");
    g_assert_cmpstr(app.players[1].model, ==, "z:1b");

    server.auto_move = FALSE;
    server.body = "{\"done\":true,\"response\":\"{\\\"move\\\":0}\"}";
    select_players(&app, PLAYER_OLLAMA, PLAYER_HUMAN);
    wait_job(&app);
    g_assert_true(app.turn_failed);
    g_assert_false(app.ready);
    g_assert_cmpuint(app.game.moves, ==, 0);
    g_assert_true(gtk_widget_get_visible(app.turn_error_box));
    g_assert_cmpuint(app.source, ==, 0);
    gtk_range_set_value(GTK_RANGE(app.scale), 0);
    drain_events();
    g_assert_null(app.task);
    g_assert_cmpuint(app.game.moves, ==, 0);
    server.auto_move = TRUE;
    retry_ollama(NULL, &app);
    wait_moves(&app, 1);
    g_assert_false(app.turn_failed);
    g_assert_false(gtk_widget_get_visible(app.turn_error_box));
    g_assert_nonnull(strstr(app.history->str, "Tentativa Ollama antes da jogada 1"));
    g_assert_nonnull(strstr(app.history->str, "modelo: a:1b"));

    /* Cancel a pending request and start a different session. No old move or
     * error may escape into the new session, even with a zero delay. */
    server.delayed = TRUE;
    unsigned calls = server.calls;
    select_players(&app, PLAYER_OLLAMA, PLAYER_HUMAN);
    gint64 end = g_get_monotonic_time() + 5000000;
    while (server.calls == calls && g_get_monotonic_time() < end) {
        drain_events();
        g_usleep(1000);
    }
    g_assert_cmpuint(server.calls, >, calls);
    select_players(&app, PLAYER_MINIMAX, PLAYER_HUMAN);
    wait_moves(&app, 1);
    g_assert_false(app.turn_failed);
    g_assert_null(strstr(app.history->str, "Ollama"));
    server.delayed = FALSE;
    configure_clicked(NULL, &app);
    gtk_drop_down_set_selected(app.controllers[0], PLAYER_OLLAMA);
    server.body = "{\"models\":[]}";
    refresh_models_clicked(NULL, &app);
    wait_models(&app);
    g_assert_false(app.models_valid);
    g_assert_false(gtk_widget_get_sensitive(app.begin_button));
    server.status = 500;
    refresh_models_clicked(NULL, &app);
    wait_models(&app);
    g_assert_false(gtk_widget_get_sensitive(app.begin_button));
    gtk_drop_down_set_selected(app.controllers[0], PLAYER_HUMAN);
    g_assert_true(gtk_widget_get_sensitive(app.begin_button));
    /* Close with a discovery request still pending. */
    server.status = 200;
    server.delayed = TRUE;
    refresh_models_clicked(NULL, &app);
    gtk_window_destroy(app.window);
    shutdown_jobs(&app);
    g_assert_null(app.models_task);
    g_assert_null(app.models_cancel);
    g_assert_null(app.task);
    g_object_unref(app.application);
    g_string_free(app.history, TRUE);
    g_rand_free(app.random);
    g_remove(app.history_path);
    g_free(app.history_path);
    g_rmdir(directory);
    g_free(directory);
    teardown(&server, NULL);
}

static void test_live_languages(void)
{
    i18n_set_language(I18N_ENGLISH);
    App app = {0};
    app.delay = 3;
    app.random = g_rand_new_with_seed(31);
    app.history = g_string_new(NULL);
    GError *error = NULL;
    g_autofree char *directory = g_dir_make_tmp("ai-games-language-ui-XXXXXX", &error);
    g_assert_no_error(error);
    app.history_path = g_build_filename(directory, "history.md", NULL);
    app.language_path = g_build_filename(directory, "settings.ini", NULL);
    app.application = GTK_APPLICATION(adw_application_new("io.github.aigames.Language.Test", G_APPLICATION_NON_UNIQUE));
    g_assert_true(g_application_register(G_APPLICATION(app.application), NULL, &error));
    g_assert_no_error(error);
    activate(app.application, &app);
    drain_events();
    g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(app.begin_button)), ==, "Start game");
    select_players(&app, PLAYER_HUMAN, PLAYER_HUMAN);
    play_cell(&app, 0);
    Game snapshot = app.game;
    gtk_widget_grab_focus(GTK_WIDGET(app.languages));
    g_assert_false(key_pressed(NULL, GDK_KEY_1, 0, 0, &app));
    g_assert_cmpmem(&app.game, sizeof app.game, &snapshot, sizeof snapshot);
    gtk_widget_grab_focus(app.board);
    g_autofree char *history = g_strdup(app.history->str);
    const I18nLanguage selections[] = {I18N_PORTUGUESE, I18N_SPANISH, I18N_ITALIAN, I18N_ENGLISH};
    const char *start_labels[] = {"Iniciar partida", "Iniciar partida", "Inizia partita", "Start game"};
    const char *human_labels[] = {"Humano", "Humano", "Umano", "Human"};
    for (unsigned i = 0; i < G_N_ELEMENTS(selections); i++) {
        gtk_drop_down_set_selected(app.languages, selections[i]);
        g_assert_cmpint(i18n_language(), ==, selections[i]);
        g_assert_cmpstr(gtk_button_get_label(GTK_BUTTON(app.begin_button)), ==, start_labels[i]);
        GtkStringList *types = GTK_STRING_LIST(gtk_drop_down_get_model(app.controllers[0]));
        g_assert_cmpstr(gtk_string_list_get_string(types, PLAYER_HUMAN), ==, human_labels[i]);
        g_assert_cmpmem(&app.game, sizeof app.game, &snapshot, sizeof snapshot);
        g_assert_cmpstr(app.history->str, ==, history);
        g_assert_cmpint(app.players[0].kind, ==, PLAYER_HUMAN);
        g_assert_cmpuint(gtk_drop_down_get_selected(app.controllers[0]), ==, PLAYER_HUMAN);
        g_autoptr(GKeyFile) file = g_key_file_new();
        g_assert_true(g_key_file_load_from_file(file, app.language_path, G_KEY_FILE_NONE, &error));
        g_assert_no_error(error);
        g_autofree char *saved = g_key_file_get_string(file, "Interface", "Language", &error);
        g_assert_no_error(error);
        g_assert_cmpstr(saved, ==, i18n_language_code(selections[i]));
    }
    /* A running computation and its minimum delay survive language changes. */
    select_players(&app, PLAYER_HUMAN, PLAYER_MINIMAX);
    play_cell(&app, 0);
    GTask *task = app.task;
    guint64 generation = app.generation;
    guint timer = app.source;
    gtk_drop_down_set_selected(app.languages, I18N_ITALIAN);
    g_assert_true(app.task == task);
    g_assert_cmpuint(app.generation, ==, generation);
    g_assert_cmpuint(app.source, ==, timer);
    g_assert_cmpuint(app.game.moves, ==, 1);
    g_assert_cmpint(app.players[1].kind, ==, PLAYER_MINIMAX);
    wait_job(&app);
    g_assert_true(app.ready);
    g_assert_cmpuint(app.game.moves, ==, 1);
    select_players(&app, PLAYER_HUMAN, PLAYER_HUMAN);
    const unsigned win_c4[] = {0,6,1,6,2,5,3};
    const unsigned win_ttt[] = {0,3,1,4,2};
    const unsigned *moves = GAME_CHOICES == 7 ? win_c4 : win_ttt;
    for (unsigned i = 0; i < (GAME_CHOICES == 7 ? 7u : 5u); i++) play_cell(&app, moves[i]);
    g_assert_cmpint(game_result(&app.game), ==, 1);
    timer = app.source;
    gtk_drop_down_set_selected(app.languages, I18N_ENGLISH);
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(app.status)), "won!"));
    gtk_drop_down_set_selected(app.languages, I18N_PORTUGUESE);
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(app.status)), "venceu!"));
    g_assert_cmpuint(app.source, ==, timer);
    gtk_window_destroy(app.window);
    shutdown_jobs(&app);
    g_object_unref(app.application);
    g_string_free(app.history, TRUE);
    g_rand_free(app.random);
    g_remove(app.history_path);
    g_remove(app.language_path);
    g_free(app.history_path);
    g_free(app.language_path);
    g_rmdir(directory);
    i18n_set_language(I18N_ENGLISH);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    GError *error = NULL;
    g_autofree char *directory = g_dir_make_tmp("ollama-ui-log-XXXXXX", &error);
    g_assert_no_error(error);
    g_autofree char *log_path = g_build_filename(directory, OLLAMA_DEFAULT_LOG_FILE, NULL);
    g_setenv("OLLAMA_LOG_FILE", log_path, TRUE);
    g_autofree char *error_log_path = g_build_filename(directory, OLLAMA_DEFAULT_ERROR_LOG_FILE, NULL);
    g_setenv("OLLAMA_ERROR_LOG_FILE", error_log_path, TRUE);
    g_test_add_func("/ui/languages-live-persistence", test_live_languages);
    g_test_add_func("/ui/ollama-controllers-retry-cancel", test_ollama_controls);
    g_test_add_func("/ui/worker-without-display", test_worker);
    g_test_add_func("/ui/stale-completion-without-display", test_stale_completion);

    g_test_add_func("/ui/controllers-timers-history", test_controls);
    int result = g_test_run();
    g_remove(log_path);
    g_remove(error_log_path);
    g_rmdir(directory);
    return result;
}
