// SPDX-License-Identifier: GPL-3.0-or-later
#include <adwaita.h>
#include <math.h>
#include <string.h>
#include "game.h"
#include "minimax.h"
#include "ollama.h"
#include "ui-i18n.h"

#define DEFAULT_MINIMAX_DEPTH 9

typedef struct {
    Game game;
    PlayerConfig player;
    guint64 generation;
    guint32 seed;
    gint progress;
    Score scores[GAME_CHOICES];
    MinimaxResult minimax;
    int chosen;
    char *ollama_url;
    OllamaResult ollama;
} TurnJob;

typedef struct {
    GtkApplication *application;
    GtkWindow *window;
    GtkWidget *stack, *board, *status, *scale, *error_box, *error_label;
    GtkDropDown *controllers[2], *languages;
    gboolean translating;
    char *language_path, *save_error, *turn_error, *models_error;
    const char *models_status_key;
    GtkWidget *depth_rows[2], *depth_scales[2];
    PlayerConfig players[2];
    GtkDropDown *models[2];
    GtkWidget *model_rows[2], *model_status, *refresh_models, *begin_button;
    GtkWidget *ollama_options[2];
    GtkCheckButton *assist_checks[2], *thinking_checks[2];
    GtkWidget *turn_error_box, *turn_error_label;
    gboolean models_attempted, models_valid, turn_failed;
    GTask *models_task;
    GCancellable *models_cancel;
    char *ollama_url;
    OllamaResult prepared_ollama;
    Game game;
    GRand *random;
    GString *history;
    GQueue pending_reports;
    char *history_path;
    gboolean playing, ready, delay_elapsed, closing;
    guint source, progress_source;
    guint64 generation;
    double delay;
    GTask *task;
    GCancellable *search_cancel;
    TurnJob *job;
    Score prepared_scores[GAME_CHOICES];
    MinimaxResult prepared_minimax;
    int chosen;
} App;

static void schedule_turn(App *app);
static void start_game(App *app);
static void start_search(App *app);
static void maybe_apply(App *app);
static void refresh_models_clicked(GtkButton *button, gpointer data);
static void update_configuration(App *app);
static void refresh_model_status(App *app);
static void refresh_language(App *app);

static PlayerConfig current_player(const App *app)
{
    return app->players[app->game.turn == 1 ? 0 : 1];
}

static gboolean human_turn(const App *app)
{
    return current_player(app).kind == PLAYER_HUMAN;
}

static const char *turn_name(const App *app)
{
    return app->game.turn == 1 ? "X" : "O";
}

static void cancel_timer(App *app)
{
    if (app->source) {
        g_source_remove(app->source);
        app->source = 0;
    }
}

static void stop_progress(App *app)
{
    if (app->progress_source) {
        g_source_remove(app->progress_source);
        app->progress_source = 0;
    }
}

static void cancel_turn(App *app)
{
    cancel_timer(app);
    stop_progress(app);
    app->generation++;
    app->ready = FALSE;
    app->turn_failed = FALSE;
    g_clear_pointer(&app->turn_error, g_free);
    if (app->turn_error_box && !app->closing)
        gtk_widget_set_visible(app->turn_error_box, FALSE);
    if (app->search_cancel)
        g_cancellable_cancel(app->search_cancel);
}

static void update_status(App *app)
{
    if (!app->window || !app->playing)
        return;
    int result = game_result(&app->game);
    if (result) {
        char *message = result == 2 ? g_strdup(_("Draw! New game in 3 seconds.")) :
            g_strdup_printf(_("%c won! New game in 3 seconds."), game_symbol(result));
        gtk_label_set_text(GTK_LABEL(app->status), message);
        g_free(message);
        return;
    }
    PlayerConfig player = current_player(app);
    char *text;
    if (app->turn_failed) {
        text = g_strdup_printf(_("%s’s turn · Ollama paused"), turn_name(app));
    } else if (player.kind == PLAYER_OLLAMA && !app->ready) {
        text = g_strdup_printf(_("%s’s turn · Ollama — %s: thinking…"), turn_name(app), player.model);
    } else if (!human_turn(app) && !app->ready) {
        unsigned completed = app->job && app->job->generation == app->generation ?
            (unsigned) g_atomic_int_get(&app->job->progress) : 0;
        text = player.kind == PLAYER_MINIMAX ?
            g_strdup_printf(_("%s’s turn · Minimax analyzing · %u/%u"), turn_name(app), completed, player.depth) :
            g_strdup_printf(_("%s’s turn · Positional analyzing"), turn_name(app));
    } else {
        text = g_strdup_printf(_("%s’s turn · %s%s"), turn_name(app), _(player_kind_name(player.kind)),
            !human_turn(app) && !app->delay_elapsed ? _(" · waiting for delay") : "");
    }
    gtk_label_set_text(GTK_LABEL(app->status), text);
    g_free(text);
}

static void flush_reports(App *app)
{
    while (!g_queue_is_empty(&app->pending_reports)) {
        GError *error = NULL;
        if (!game_append_report(app->history_path, g_queue_peek_head(&app->pending_reports), &error)) {
            g_free(app->save_error);
            app->save_error = g_strdup(error->message);
            char *message = g_strdup_printf(_("%s\nGames awaiting save in this session: %u."),
                                            error->message, app->pending_reports.length);
            gtk_label_set_text(GTK_LABEL(app->error_label), message);
            gtk_widget_set_visible(app->error_box, TRUE);
            g_free(message);
            g_clear_error(&error);
            return;
        }
        g_free(g_queue_pop_head(&app->pending_reports));
    }
    g_clear_pointer(&app->save_error, g_free);
    gtk_widget_set_visible(app->error_box, FALSE);
}

static void retry_save(GtkButton *button, gpointer data)
{
    (void) button;
    flush_reports(data);
}

static gboolean restart_game(gpointer data)
{
    App *app = data;
    app->source = 0;
    start_game(app);
    return G_SOURCE_REMOVE;
}

static char *describe_player(PlayerConfig player)
{
    if (player.kind == PLAYER_OLLAMA)
        return g_strdup_printf("Ollama (%s; %s; raciocínio ampliado %s)", player.model,
            player.ollama_assisted ? (GAME_CHOICES == 9 ? "assistido com defesa completa" : "assistido") : "puro", player.ollama_thinking ? "solicitado" : "não solicitado");
    return player.kind == PLAYER_MINIMAX ?
        g_strdup_printf("Minimax (profundidade %u)", player.depth) : g_strdup(player_kind_name(player.kind));
}

static void play_cell(App *app, unsigned move)
{
    if (app->closing || !app->playing || game_result(&app->game) || move >= GAME_CHOICES || app->game.cells[move / 3][move % 3])
        return;
    PlayerKind kind = current_player(app).kind;
    Score scores[GAME_CHOICES];
    if (kind == PLAYER_HUMAN)
        game_evaluate(&app->game, app->random, scores);
    else {
        if (!app->ready || app->chosen != (int) move)
            return;
        memcpy(scores, app->prepared_scores, sizeof scores);
    }
    game_record(app->history, &app->game, scores, move, kind);
    if (kind == PLAYER_MINIMAX)
        minimax_record(app->history, &app->prepared_minimax);
    if (kind == PLAYER_OLLAMA)
        ollama_record(app->history, current_player(app).model, &app->prepared_ollama);
    app->ready = FALSE;
    game_play(&app->game, move);
    gtk_widget_queue_draw(app->board);
    int result = game_result(&app->game);
    if (!result) {
        schedule_turn(app);
        return;
    }
    cancel_turn(app);
    update_status(app);
    if (result != 2) {
        GDateTime *now = g_date_time_new_now_local();
        char *first = describe_player(app->players[0]), *second = describe_player(app->players[1]);
        char *configuration = g_strdup_printf("X: %s; O: %s. Delay mínimo: %.1f s.", first, second, app->delay);
        g_queue_push_tail(&app->pending_reports,
                         game_report(&app->game, app->history->str, configuration, now));
        g_free(first);
        g_free(second);
        g_free(configuration);
        g_date_time_unref(now);
        flush_reports(app);
    }
    app->source = g_timeout_add(3000, restart_game, app);
}

static void maybe_apply(App *app)
{
    if (!app->closing && app->playing && app->window && !human_turn(app) && app->ready && app->delay_elapsed &&
        !game_result(&app->game))
        play_cell(app, (unsigned) app->chosen);
}

static gboolean automatic_move(gpointer data)
{
    App *app = data;
    app->source = 0;
    app->delay_elapsed = TRUE;
    maybe_apply(app);
    return G_SOURCE_REMOVE;
}

static void arm_delay(App *app)
{
    cancel_timer(app);
    app->delay_elapsed = FALSE;
    app->source = app->delay == 0 ? g_idle_add(automatic_move, app) :
        g_timeout_add((guint) lround(app->delay * 1000), automatic_move, app);
}

static gboolean progress_tick(gpointer data)
{
    App *app = data;
    update_status(app);
    return G_SOURCE_CONTINUE;
}

typedef struct { TurnJob *job; GCancellable *cancel; } WorkerControl;

static gboolean search_cancelled(gpointer data)
{
    WorkerControl *control = data;
    return g_cancellable_is_cancelled(control->cancel);
}

static void search_progress(unsigned completed, gpointer data)
{
    WorkerControl *control = data;
    g_atomic_int_set(&control->job->progress, (gint) completed);
}

static void turn_job_free(gpointer data)
{
    TurnJob *job = data;
    g_free(job->player.model);
    g_free(job->ollama_url);
    g_free(job);
}

static void turn_worker(GTask *task, gpointer source, gpointer task_data, GCancellable *cancel)
{
    (void) source;
    TurnJob *job = task_data;
    GRand *random = g_rand_new_with_seed(job->seed);
    game_evaluate(&job->game, random, job->scores);
    gboolean ok = !g_cancellable_is_cancelled(cancel);
    if (ok && job->player.kind == PLAYER_MINIMAX) {
        WorkerControl data = {job, cancel};
        SearchControl control = {.cancelled = search_cancelled, .progress = search_progress, .data = &data};
        ok = minimax_evaluate(&job->game, job->player.depth, &control, &job->minimax);
        if (ok) {
            job->chosen = minimax_choose(&job->minimax, random);
            job->minimax.best_move = job->chosen;
        }
    } else if (ok && job->player.kind == PLAYER_OLLAMA) {
        GError *error = NULL;
        OllamaOptions options = {.assisted = job->player.ollama_assisted, .thinking = job->player.ollama_thinking};
        ok = ollama_choose_with_options(&job->game, job->player.model, job->ollama_url,
                           OLLAMA_MOVE_TIMEOUT_MS, cancel, &options, &job->ollama, &error);
        if (!ok) {
            g_rand_free(random);
            g_task_return_error(task, error);
            return;
        }
        job->chosen = job->ollama.move;
    } else if (ok) {
        job->chosen = game_best_move(job->scores);
    }
    g_rand_free(random);
    g_task_return_boolean(task, ok);
}

static void turn_completed(GObject *source, GAsyncResult *result, gpointer data)
{
    (void) source;
    App *app = data;
    GTask *task = G_TASK(result);
    TurnJob *job = g_task_get_task_data(task);
    GError *error = NULL;
    gboolean ok = g_task_propagate_boolean(task, &error);
    gboolean current = !app->closing && app->window && app->playing && !human_turn(app) &&
        job->generation == app->generation && !game_result(&app->game);
    gboolean accepted = ok && current;
    app->job = NULL;
    g_clear_object(&app->task);
    g_clear_object(&app->search_cancel);
    stop_progress(app);
    if (accepted) {
        memcpy(app->prepared_scores, job->scores, sizeof app->prepared_scores);
        app->prepared_minimax = job->minimax;
        app->prepared_ollama = job->ollama;
        app->chosen = job->chosen;
        app->ready = job->chosen >= 0;
        update_status(app);
        maybe_apply(app);
    } else if (current && error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        app->turn_failed = TRUE;
        cancel_timer(app);
        g_autofree char *model = g_markup_escape_text(job->player.model ? job->player.model : "", -1);
        g_autofree char *message = g_markup_escape_text(error->message, -1);
        g_string_append_printf(app->history,
            "Tentativa Ollama antes da jogada %u — modelo: %s; duração: %.3f s; erro: %s.\n\n",
            app->game.moves + 1, model, job->ollama.elapsed_us / 1000000.0, message);
        g_free(app->turn_error);
        app->turn_error = g_strdup(error->message);
        gtk_label_set_text(GTK_LABEL(app->turn_error_label), _(error->message));
        gtk_widget_set_visible(app->turn_error_box, TRUE);
        update_status(app);
    } else if (!app->closing && app->window && app->playing && !human_turn(app) && !game_result(&app->game)) {
        /* A new session waits for the cancelled task to finish before starting
         * its search: there is never more than one worker per application. */
        start_search(app);
    }
    g_clear_error(&error);
}

static void start_search(App *app)
{
    if (app->closing || app->turn_failed || app->task || !app->window || !app->playing || human_turn(app) || game_result(&app->game))
        return;
    TurnJob *job = g_new0(TurnJob, 1);
    job->game = app->game;
    job->player = current_player(app);
    job->player.model = g_strdup(job->player.model);
    job->ollama_url = g_strdup(app->ollama_url);
    job->generation = app->generation;
    job->seed = g_rand_int(app->random);
    job->chosen = -1;
    app->job = job;
    app->search_cancel = g_cancellable_new();
    app->task = g_task_new(NULL, app->search_cancel, turn_completed, app);
    g_task_set_task_data(app->task, job, turn_job_free);
    g_task_set_return_on_cancel(app->task, FALSE);
    app->progress_source = g_timeout_add(100, progress_tick, app);
    g_task_run_in_thread(app->task, turn_worker);
}

static void schedule_turn(App *app)
{
    cancel_turn(app);
    app->delay_elapsed = FALSE;
    update_status(app);
    if (!human_turn(app)) {
        arm_delay(app);
        start_search(app);
    }
}

static void start_game(App *app)
{
    if (app->closing) return;
    cancel_turn(app);
    game_init(&app->game);
    g_string_truncate(app->history, 0);
    app->playing = TRUE;
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "game");
    gtk_widget_queue_draw(app->board);
    gtk_widget_grab_focus(app->board);
    schedule_turn(app);
}

static void begin_clicked(GtkButton *button, gpointer data)
{
    (void) button;
    App *app = data;
    for (unsigned i = 0; i < 2; i++) {
        if (gtk_drop_down_get_selected(app->controllers[i]) == PLAYER_OLLAMA &&
            (!app->models_valid || gtk_drop_down_get_selected(app->models[i]) == GTK_INVALID_LIST_POSITION))
            return;
    }
    for (unsigned i = 0; i < 2; i++) {
        g_free(app->players[i].model);
        guint selected = gtk_drop_down_get_selected(app->models[i]);
        app->players[i].model = selected == GTK_INVALID_LIST_POSITION ? NULL :
            g_strdup(gtk_string_list_get_string(GTK_STRING_LIST(gtk_drop_down_get_model(app->models[i])), selected));
        app->players[i].kind = (PlayerKind) gtk_drop_down_get_selected(app->controllers[i]);
        app->players[i].depth = (unsigned) gtk_range_get_value(GTK_RANGE(app->depth_scales[i]));
        app->players[i].ollama_assisted = gtk_check_button_get_active(app->assist_checks[i]);
        app->players[i].ollama_thinking = gtk_check_button_get_active(app->thinking_checks[i]);
    }
    start_game(app);
}

static void controller_changed(GObject *object, GParamSpec *pspec, gpointer data)
{
    (void) object;
    (void) pspec;
    App *app = data;
    if (app->translating) return;
    update_configuration(app);
    gboolean needed = FALSE;
    for (unsigned i = 0; i < 2; i++)
        needed |= gtk_drop_down_get_selected(app->controllers[i]) == PLAYER_OLLAMA;
    if (needed && !app->models_attempted)
        refresh_models_clicked(NULL, app);
}

static void configure_clicked(GtkButton *button, gpointer data)
{
    (void) button;
    App *app = data;
    cancel_turn(app);
    app->playing = FALSE;
    g_string_truncate(app->history, 0);
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "configuration");
}

static void delay_changed(GtkRange *range, gpointer data)
{
    App *app = data;
    app->delay = round(gtk_range_get_value(range) * 10) / 10;
    if (app->playing && !app->turn_failed && !game_result(&app->game) && !human_turn(app)) {
        arm_delay(app);
        update_status(app);
    }
}

/* App stays alive until every worker and completion callback has returned.
 * Workers own snapshots only; no GTK access or borrowed window pointers. */
static void shutdown_jobs(App *app)
{
    app->closing = TRUE;
    app->playing = FALSE;
    cancel_turn(app);
    if (app->models_cancel) g_cancellable_cancel(app->models_cancel);
    while (app->task || app->models_task)
        g_main_context_iteration(NULL, TRUE);
    for (unsigned i = 0; i < 2; i++)
        g_clear_pointer(&app->players[i].model, g_free);
    g_clear_pointer(&app->ollama_url, g_free);
    g_clear_pointer(&app->save_error, g_free);
    g_clear_pointer(&app->turn_error, g_free);
    g_clear_pointer(&app->models_error, g_free);
}

static void draw_board(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
    App *app = data;
    double size = MIN(width, height);
    double left = (width - size) / 2.0, top = (height - size) / 2.0;
    double unit = size / 3;
    GdkRGBA color;
    gtk_widget_get_color(GTK_WIDGET(area), &color);
    cairo_translate(cr, left, top);
    cairo_set_line_width(cr, MAX(1.5, size / 200));
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, 0.35);
    for (unsigned i = 1; i < 3; i++) {
        cairo_move_to(cr, i * unit, 0);
        cairo_line_to(cr, i * unit, size);
        cairo_move_to(cr, 0, i * unit);
        cairo_line_to(cr, size, i * unit);
    }
    cairo_stroke(cr);
    for (unsigned i = 0; i < 9; i++) {
        double x = (i % 3) * unit, y = (i / 3) * unit;
        int piece = app->game.cells[i / 3][i % 3];
        cairo_set_source_rgba(cr, color.red, color.green, color.blue, piece ? 1.0 : 0.45);
        cairo_set_line_width(cr, MAX(2.0, size / 65));
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        if (piece == 1) {
            cairo_move_to(cr, x + unit * .25, y + unit * .25);
            cairo_line_to(cr, x + unit * .75, y + unit * .75);
            cairo_move_to(cr, x + unit * .75, y + unit * .25);
            cairo_line_to(cr, x + unit * .25, y + unit * .75);
            cairo_stroke(cr);
        } else if (piece == -1) {
            cairo_new_sub_path(cr);
            cairo_arc(cr, x + unit / 2, y + unit / 2, unit * .28, 0, 2 * G_PI);
            cairo_stroke(cr);
        } else {
            char label[2] = {(char) ('0' + 7 - (i / 3) * 3 + i % 3), '\0'};
            cairo_set_font_size(cr, unit * .14);
            cairo_move_to(cr, x + unit * .08, y + unit * .19);
            cairo_show_text(cr, label);
        }
    }
    const unsigned *winning = game_winning_line(&app->game);
    if (winning) {
        double x1 = (winning[0] % 3 + .5) * unit;
        double y1 = (winning[0] / 3 + .5) * unit;
        double x2 = (winning[2] % 3 + .5) * unit;
        double y2 = (winning[2] / 3 + .5) * unit;
        double dx = (x2 - x1) * .15, dy = (y2 - y1) * .15;
        cairo_new_path(cr);
        cairo_set_source_rgb(cr, .18, .82, .45);
        cairo_set_line_width(cr, MAX(3.0, size / 80));
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_move_to(cr, x1 - dx, y1 - dy);
        cairo_line_to(cr, x2 + dx, y2 + dy);
        cairo_stroke(cr);
    }
}

static void board_clicked(GtkGestureClick *gesture, int presses, double x, double y, gpointer data)
{
    (void) gesture;
    (void) presses;
    App *app = data;
    if (!app->playing || !human_turn(app) || game_result(&app->game))
        return;
    double width = gtk_widget_get_width(app->board), height = gtk_widget_get_height(app->board);
    double size = MIN(width, height);
    x -= (width - size) / 2;
    y -= (height - size) / 2;
    if (size <= 0 || x < 0 || y < 0 || x >= size || y >= size)
        return;
    gtk_widget_grab_focus(app->board);
    play_cell(app, (unsigned) (y / (size / 3)) * 3 + (unsigned) (x / (size / 3)));
}

static gboolean key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode,
                            GdkModifierType state, gpointer data)
{
    (void) controller;
    (void) keycode;
    App *app = data;
    GtkWidget *focus = app->window ? gtk_window_get_focus(app->window) : NULL;
    if (focus && app->languages && (focus == GTK_WIDGET(app->languages) ||
        gtk_widget_is_ancestor(focus, GTK_WIDGET(app->languages))))
        return FALSE;
    if (!app->playing || !human_turn(app) || game_result(&app->game) ||
        (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)))
        return FALSE;
    int number = 0;
    if (keyval >= GDK_KEY_1 && keyval <= GDK_KEY_9)
        number = keyval - GDK_KEY_0;
    else if (keyval >= GDK_KEY_KP_1 && keyval <= GDK_KEY_KP_9)
        number = keyval - GDK_KEY_KP_0;
    else {
        switch (keyval) {
        case GDK_KEY_KP_End: number = 1; break;
        case GDK_KEY_KP_Down: number = 2; break;
        case GDK_KEY_KP_Page_Down: number = 3; break;
        case GDK_KEY_KP_Left: number = 4; break;
        case GDK_KEY_KP_Begin: number = 5; break;
        case GDK_KEY_KP_Right: number = 6; break;
        case GDK_KEY_KP_Home: number = 7; break;
        case GDK_KEY_KP_Up: number = 8; break;
        case GDK_KEY_KP_Page_Up: number = 9; break;
        default: return FALSE;
        }
    }
    unsigned cell = (2 - (number - 1) / 3) * 3 + (number - 1) % 3;
    play_cell(app, cell);
    return TRUE;
}

static void set_margins(GtkWidget *widget, int margin)
{
    gtk_widget_set_margin_start(widget, margin);
    gtk_widget_set_margin_end(widget, margin);
    gtk_widget_set_margin_top(widget, margin);
    gtk_widget_set_margin_bottom(widget, margin);
}

static const char *selected_model(App *app, unsigned player)
{
    guint selected = gtk_drop_down_get_selected(app->models[player]);
    return selected == GTK_INVALID_LIST_POSITION ? NULL :
        gtk_string_list_get_string(GTK_STRING_LIST(gtk_drop_down_get_model(app->models[player])), selected);
}

static void update_configuration(App *app)
{
    gboolean needed = FALSE, valid = TRUE;
    for (unsigned i = 0; i < 2; i++) {
        if (!app->models[i]) continue; /* Configuration widgets are being built. */
        guint kind = gtk_drop_down_get_selected(app->controllers[i]);
        gboolean ollama = kind == PLAYER_OLLAMA;
        needed |= ollama;
        valid &= !ollama || (app->models_valid && selected_model(app, i));
        gtk_widget_set_visible(app->depth_rows[i], kind == PLAYER_MINIMAX);
        gtk_widget_set_visible(app->model_rows[i], ollama);
        gtk_widget_set_visible(app->ollama_options[i], ollama);
        gtk_widget_set_sensitive(GTK_WIDGET(app->models[i]), app->models_valid);
    }
    if (!app->begin_button) return;
    gtk_widget_set_sensitive(app->begin_button, valid);
    gtk_widget_set_visible(app->refresh_models, needed);
    gtk_widget_set_sensitive(app->refresh_models, app->models_task == NULL);
    gtk_widget_set_visible(app->model_status, needed);
}

static void model_changed(GObject *object, GParamSpec *pspec, gpointer data)
{
    (void) object;
    (void) pspec;
    update_configuration(data);
}

static void models_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void) source;
    GError *error = NULL;
    char **names = ollama_list_models(data, OLLAMA_LIST_TIMEOUT_MS, cancel, &error);
    if (names) g_task_return_pointer(task, names, (GDestroyNotify) g_strfreev);
    else g_task_return_error(task, error);
}

static void models_completed(GObject *source, GAsyncResult *result, gpointer data)
{
    (void) source;
    App *app = data;
    GError *error = NULL;
    g_auto(GStrv) names = g_task_propagate_pointer(G_TASK(result), &error);
    g_clear_object(&app->models_task);
    g_clear_object(&app->models_cancel);
    if (!app->closing && app->window) {
        app->models_valid = names && names[0];
        if (names) {
            /* Preserve each player's choice independently across refreshes. */
            for (unsigned i = 0; i < 2; i++) {
                g_autofree char *previous = g_strdup(selected_model(app, i));
                g_autoptr(GtkStringList) strings = gtk_string_list_new((const char * const *) names);
                gtk_drop_down_set_model(app->models[i], G_LIST_MODEL(strings));
                guint selected = names[0] ? 0 : GTK_INVALID_LIST_POSITION;
                for (unsigned j = 0; names[j]; j++)
                    if (g_strcmp0(previous, names[j]) == 0) selected = j;
                gtk_drop_down_set_selected(app->models[i], selected);
            }
        }
        g_free(app->models_error);
        app->models_error = error ? g_strdup(error->message) : NULL;
        app->models_status_key = app->models_valid ? "Models available from local Ollama." :
            "No models available. Install a model in Ollama and refresh the list.";
        refresh_model_status(app);
        update_configuration(app);
    }
    g_clear_error(&error);
}

static void refresh_models_clicked(GtkButton *button, gpointer data)
{
    (void) button;
    App *app = data;
    if (app->closing || app->models_task) return;
    app->models_attempted = TRUE;
    app->models_valid = FALSE;
    app->models_cancel = g_cancellable_new();
    app->models_task = g_task_new(NULL, app->models_cancel, models_completed, app);
    g_task_set_task_data(app->models_task, g_strdup(app->ollama_url), g_free);
    g_task_set_return_on_cancel(app->models_task, FALSE);
    g_clear_pointer(&app->models_error, g_free);
    app->models_status_key = "Loading models…";
    refresh_model_status(app);
    update_configuration(app);
    g_task_run_in_thread(app->models_task, models_worker);
}

static void retry_ollama(GtkButton *button, gpointer data)
{
    (void) button;
    App *app = data;
    if (app->playing && app->turn_failed && current_player(app).kind == PLAYER_OLLAMA)
        schedule_turn(app);
}

static GtkWidget *make_configuration(App *app)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    set_margins(box, 24);
    GtkWidget *title = ui_label("Tic-Tac-Toe");
    gtk_widget_add_css_class(title, "title-1");
    gtk_box_append(GTK_BOX(box), title);
    const char *types[] = {_("Human"), _("Positional"), "Minimax", "Ollama", NULL};
    const char *labels[] = {"X — starts", "O"};
    for (unsigned i = 0; i < 2; i++) {
        gtk_box_append(GTK_BOX(box), ui_label(labels[i]));
        app->controllers[i] = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(types));
        gtk_drop_down_set_selected(app->controllers[i], i == 0 ? PLAYER_HUMAN : PLAYER_POSITIONAL);
        gtk_box_append(GTK_BOX(box), GTK_WIDGET(app->controllers[i]));
        app->depth_rows[i] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_box_append(GTK_BOX(app->depth_rows[i]), ui_label("Depth (moves)"));
        app->depth_scales[i] = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, GAME_MAX_DEPTH, 1);
        gtk_scale_set_digits(GTK_SCALE(app->depth_scales[i]), 0);
        gtk_scale_set_draw_value(GTK_SCALE(app->depth_scales[i]), TRUE);
        gtk_scale_set_value_pos(GTK_SCALE(app->depth_scales[i]), GTK_POS_RIGHT);
        gtk_range_set_round_digits(GTK_RANGE(app->depth_scales[i]), 0);
        ui_tooltip(app->depth_scales[i],
            "Each level is one move. Higher values may take longer; New configuration cancels the analysis.");
        gtk_range_set_value(GTK_RANGE(app->depth_scales[i]), DEFAULT_MINIMAX_DEPTH);
        gtk_widget_set_hexpand(app->depth_scales[i], TRUE);
        gtk_box_append(GTK_BOX(app->depth_rows[i]), app->depth_scales[i]);
        gtk_box_append(GTK_BOX(box), app->depth_rows[i]);
        gtk_widget_set_visible(app->depth_rows[i], FALSE);
        app->model_rows[i] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_box_append(GTK_BOX(app->model_rows[i]), ui_label("Model"));
        const char *empty[] = {NULL};
        app->models[i] = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(empty));
        gtk_drop_down_set_enable_search(app->models[i], TRUE);
        gtk_widget_set_hexpand(GTK_WIDGET(app->models[i]), TRUE);
        gtk_box_append(GTK_BOX(app->model_rows[i]), GTK_WIDGET(app->models[i]));
        gtk_box_append(GTK_BOX(box), app->model_rows[i]);
        gtk_widget_set_visible(app->model_rows[i], FALSE);
        app->ollama_options[i] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        app->assist_checks[i] = GTK_CHECK_BUTTON(ui_check("Tactical assistance"));
        ui_tooltip(GTK_WIDGET(app->assist_checks[i]),
            GAME_CHOICES == 9 ?
            "Complete defense: searches to the end and excludes forced losses when alternatives exist. The model chooses among the remaining moves. Unchecked: pure Ollama." :
            "Prioritizes immediate wins and, when possible, excludes moves that let the opponent win on the next turn. Unchecked: pure Ollama.");
        app->thinking_checks[i] = GTK_CHECK_BUTTON(ui_check("Extended thinking"));
        gtk_check_button_set_active(app->thinking_checks[i], TRUE);
        ui_tooltip(GTK_WIDGET(app->thinking_checks[i]),
            "Requests thinking supported by the model. Cogito uses separate analysis and decision requests. This may take longer; unchecked uses the model default.");
        gtk_box_append(GTK_BOX(app->ollama_options[i]), GTK_WIDGET(app->assist_checks[i]));
        gtk_box_append(GTK_BOX(app->ollama_options[i]), GTK_WIDGET(app->thinking_checks[i]));
        gtk_box_append(GTK_BOX(box), app->ollama_options[i]);
        gtk_widget_set_visible(app->ollama_options[i], FALSE);
        g_signal_connect(app->models[i], "notify::selected", G_CALLBACK(model_changed), app);
        g_signal_connect(app->controllers[i], "notify::selected", G_CALLBACK(controller_changed), app);
    }
    app->model_status = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(app->model_status), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(app->model_status), 55);
    gtk_widget_set_visible(app->model_status, FALSE);
    gtk_box_append(GTK_BOX(box), app->model_status);
    app->refresh_models = ui_button("Refresh models");
    g_signal_connect(app->refresh_models, "clicked", G_CALLBACK(refresh_models_clicked), app);
    gtk_widget_set_visible(app->refresh_models, FALSE);
    gtk_box_append(GTK_BOX(box), app->refresh_models);
    GtkWidget *button = ui_button("Start game");
    app->begin_button = button;
    gtk_widget_add_css_class(button, "suggested-action");
    g_signal_connect(button, "clicked", G_CALLBACK(begin_clicked), app);
    gtk_box_append(GTK_BOX(box), button);
    return box;
}

static GtkWidget *make_game(App *app)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(controls, 12);
    gtk_widget_set_margin_end(controls, 12);
    app->status = gtk_label_new("");
    gtk_widget_set_hexpand(app->status, TRUE);
    gtk_label_set_xalign(GTK_LABEL(app->status), 0);
    gtk_label_set_wrap(GTK_LABEL(app->status), TRUE);
    gtk_box_append(GTK_BOX(controls), app->status);
    GtkWidget *configure = ui_button("New configuration");
    g_signal_connect(configure, "clicked", G_CALLBACK(configure_clicked), app);
    gtk_box_append(GTK_BOX(controls), configure);
    gtk_box_append(GTK_BOX(box), controls);

    GtkWidget *delay_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(delay_box, 12);
    gtk_widget_set_margin_end(delay_box, 12);
    gtk_box_append(GTK_BOX(delay_box), ui_label("Delay (s)"));
    app->scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 3, .1);
    gtk_scale_set_digits(GTK_SCALE(app->scale), 1);
    gtk_scale_set_draw_value(GTK_SCALE(app->scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(app->scale), GTK_POS_RIGHT);
    gtk_range_set_round_digits(GTK_RANGE(app->scale), 1);
    gtk_range_set_value(GTK_RANGE(app->scale), app->delay);
    gtk_widget_set_hexpand(app->scale, TRUE);
    ui_tooltip(app->scale, "Minimum automatic turn delay; computation runs during the wait");
    g_signal_connect(app->scale, "value-changed", G_CALLBACK(delay_changed), app);
    gtk_box_append(GTK_BOX(delay_box), app->scale);
    gtk_box_append(GTK_BOX(box), delay_box);

    app->turn_error_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    set_margins(app->turn_error_box, 12);
    gtk_widget_add_css_class(app->turn_error_box, "error");
    app->turn_error_label = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(app->turn_error_label), TRUE);
    gtk_widget_set_hexpand(app->turn_error_label, TRUE);
    gtk_box_append(GTK_BOX(app->turn_error_box), app->turn_error_label);
    GtkWidget *retry_turn = ui_button("Retry");
    g_signal_connect(retry_turn, "clicked", G_CALLBACK(retry_ollama), app);
    gtk_box_append(GTK_BOX(app->turn_error_box), retry_turn);
    gtk_widget_set_visible(app->turn_error_box, FALSE);
    gtk_box_append(GTK_BOX(box), app->turn_error_box);

    app->board = gtk_drawing_area_new();
    gtk_widget_set_hexpand(app->board, TRUE);
    gtk_widget_set_vexpand(app->board, TRUE);
    gtk_widget_set_focusable(app->board, TRUE);
    ui_tooltip(app->board, "Use the mouse or keys 1–9 in numeric keypad order");
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->board), draw_board, app, NULL);
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(board_clicked), app);
    gtk_widget_add_controller(app->board, GTK_EVENT_CONTROLLER(click));
    gtk_box_append(GTK_BOX(box), app->board);
    return box;
}

static void window_destroyed(GtkWidget *widget, gpointer data)
{
    (void) widget;
    App *app = data;
    app->closing = TRUE;
    if (app->models_cancel) g_cancellable_cancel(app->models_cancel);
    cancel_turn(app);
    app->playing = FALSE;
    app->window = NULL;
}

static void refresh_model_status(App *app)
{
    if (app->model_status)
        gtk_label_set_text(GTK_LABEL(app->model_status), app->models_error ? _(app->models_error) :
            _(app->models_status_key ? app->models_status_key : ""));
}

static void refresh_language(App *app)
{
    if (!app->window || app->closing) return;
    app->translating = TRUE;
    const char *types[] = {_("Human"), _("Positional"), "Minimax", "Ollama", NULL};
    for (unsigned i = 0; i < 2; i++) {
        guint selected = gtk_drop_down_get_selected(app->controllers[i]);
        g_autoptr(GtkStringList) names = gtk_string_list_new(types);
        gtk_drop_down_set_model(app->controllers[i], G_LIST_MODEL(names));
        gtk_drop_down_set_selected(app->controllers[i], selected);
    }
    /* This runs inside the language drop-down's selection notification, which
     * still references its current selection model: translate in place. */
    const char *system_name[] = {_("System default"), NULL};
    gtk_string_list_splice(GTK_STRING_LIST(gtk_drop_down_get_model(app->languages)), 0, 1, system_name);
    gtk_drop_down_set_selected(app->languages, i18n_selection());
    ui_retranslate_tree(GTK_WIDGET(app->window));
    refresh_model_status(app);
    if (app->turn_error)
        gtk_label_set_text(GTK_LABEL(app->turn_error_label), _(app->turn_error));
    if (app->save_error) {
        g_autofree char *message = g_strdup_printf(_("%s\nGames awaiting save in this session: %u."),
            _(app->save_error), app->pending_reports.length);
        gtk_label_set_text(GTK_LABEL(app->error_label), message);
    }
    update_status(app);
    app->translating = FALSE;
    update_configuration(app);
}

static void language_changed(GObject *object, GParamSpec *pspec, gpointer data)
{
    (void) object;
    (void) pspec;
    App *app = data;
    if (app->translating || app->closing) return;
    guint selected = gtk_drop_down_get_selected(app->languages);
    /* Refreshing re-notifies the selection; ignore it once applied. */
    if (selected >= I18N_LANGUAGE_COUNT || selected == i18n_selection()) return;
    i18n_set_language((I18nLanguage) selected);
    refresh_language(app);
    GError *error = NULL;
    if (!i18n_save(app->language_path, &error)) {
        g_printerr(_("Could not save language preference: %s\n"), error->message);
        g_clear_error(&error);
    }
}

static void activate(GtkApplication *application, gpointer data)
{
    App *app = data;
    if (app->window) {
        gtk_window_present(app->window);
        return;
    }
    app->window = GTK_WINDOW(adw_application_window_new(application));
    ui_title(app->window, "Tic-Tac-Toe");
    gtk_window_set_default_size(app->window, 620, 740);
    g_signal_connect(app->window, "destroy", G_CALLBACK(window_destroyed), app);
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *header = adw_header_bar_new();
    GtkWidget *language_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(language_box), ui_label("Language"));
    const char *language_names[] = {_("System default"), "English", "Português", "Español", "Italiano", NULL};
    app->languages = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(language_names));
    gtk_drop_down_set_selected(app->languages, i18n_selection());
    ui_tooltip(GTK_WIDGET(app->languages), "Choose the interface language");
    g_signal_connect(app->languages, "notify::selected", G_CALLBACK(language_changed), app);
    gtk_box_append(GTK_BOX(language_box), GTK_WIDGET(app->languages));
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), language_box);
    gtk_box_append(GTK_BOX(root), header);

    app->error_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    set_margins(app->error_box, 12);
    gtk_widget_add_css_class(app->error_box, "error");
    app->error_label = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(app->error_label), TRUE);
    gtk_widget_set_hexpand(app->error_label, TRUE);
    gtk_box_append(GTK_BOX(app->error_box), app->error_label);
    GtkWidget *retry = ui_button("Retry saving");
    g_signal_connect(retry, "clicked", G_CALLBACK(retry_save), app);
    gtk_box_append(GTK_BOX(app->error_box), retry);
    gtk_widget_set_visible(app->error_box, FALSE);
    gtk_box_append(GTK_BOX(root), app->error_box);

    app->stack = gtk_stack_new();
    gtk_widget_set_vexpand(app->stack, TRUE);
    /* Hidden pages must not reserve the configuration page's height during play. */
    gtk_stack_set_vhomogeneous(GTK_STACK(app->stack), FALSE);
    gtk_stack_set_hhomogeneous(GTK_STACK(app->stack), FALSE);
    gtk_stack_add_named(GTK_STACK(app->stack), make_configuration(app), "configuration");
    gtk_stack_add_named(GTK_STACK(app->stack), make_game(app), "game");
    gtk_box_append(GTK_BOX(root), app->stack);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(app->window), root);

    GtkEventController *keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), app);
    gtk_widget_add_controller(GTK_WIDGET(app->window), keys);
    gtk_window_present(app->window);
}

int main(int argc, char **argv)
{
    App app = {0};
    app.language_path = g_build_filename(g_get_user_config_dir(), "ai-games", "tic-tac-toe.ini", NULL);
    g_autofree char *legacy_language_path = g_build_filename(g_get_user_config_dir(), "ai-games", "jogo-da-velha.ini", NULL);
    GError *language_error = NULL;
    if (!i18n_load_with_legacy(app.language_path, legacy_language_path, &language_error)) {
        g_printerr(_("Could not load language preference: %s\n"), language_error->message);
        g_clear_error(&language_error);
    }
    app.delay = .5;
    app.random = g_rand_new();
    app.history = g_string_new(NULL);
    char *cwd = g_get_current_dir();
    app.history_path = g_build_filename(cwd, GAME_HISTORY_FILE, NULL);
    g_free(cwd);
    app.application = GTK_APPLICATION(adw_application_new("io.github.aigames.TicTacToe",
                                                        G_APPLICATION_NON_UNIQUE));
    g_signal_connect(app.application, "activate", G_CALLBACK(activate), &app);
    int result = g_application_run(G_APPLICATION(app.application), argc, argv);
    shutdown_jobs(&app);
    g_object_unref(app.application);
    g_queue_clear_full(&app.pending_reports, g_free);
    g_string_free(app.history, TRUE);
    g_rand_free(app.random);
    g_free(app.history_path);
    g_free(app.language_path);
    return result;
}
