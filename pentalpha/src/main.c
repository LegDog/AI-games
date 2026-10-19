// SPDX-License-Identifier: GPL-3.0-or-later
#include <adwaita.h>
#include <math.h>
#include <glib/gstdio.h>
#include "animation.h"
#include "i18n.h"
#include "ollama.h"
#include "search.h"

typedef struct {
    Game game;
    PlayerKind kind;
    unsigned depth, generation;
    guint32 seed;
    char *model, *url;
    gboolean list;
    OllamaOptions options;
    int chosen;
    SearchResult search;
    OllamaResult ollama;
    char **models;
    char *details;
} Job;
typedef struct {
    GtkApplication *application;
    GtkWidget *window, *board, *status, *error_label, *begin, *pause, *undo, *restart, *configure;
    GtkWidget *controller, *depth, *depth_label, *model, *model_label, *refresh, *assisted, *thinking, *delay, *language;
    GtkWidget *pause_at_end_switch, *end_dialog;
    gboolean pause_at_end;
    cairo_surface_t *background;
    GRand *random;
    Game game;
    Animation animation;
    PlayerKind kind;
    unsigned search_depth, generation;
    guint32 seed;
    char *selected_model, *url, *preferences, *error, *pending_details, *details[9];
    OllamaOptions options;
    gboolean playing, paused, ready, closing, saved, failed;
    int selected, pending;
    guint tick;
    GTask *task;
    GCancellable *cancel;
} App;

/* Coordinates are the supplied SVG circles after their group transforms. */
static const double point_x[10] = {
    251.000240000, 472.087016032, 387.864636432, 114.135843568, 29.913463968, 302.716800562, 334.644446085, 251.000119206, 167.355799734, 199.283430444
};
static const double point_y[10] = {
    20.806903295, 181.823122584, 442.003636862, 442.003636862, 181.823122584, 180.955053249, 279.189381601, 339.963711829, 279.189381601, 180.955053249
};
static void schedule(App *app);
static void update(App *app);
static void start_animation(App *app, int move);
static void set_error(App *app, const char *message)
{
    g_free(app->error); app->error = g_strdup(message);
    gtk_label_set_text(GTK_LABEL(app->error_label), message ? _(message) : "");
    gtk_widget_set_visible(app->error_label, message != NULL);
}
static GtkWidget *label(const char *message)
{
    GtkWidget *widget = gtk_label_new(_(message));
    gtk_label_set_xalign(GTK_LABEL(widget), 0);
    g_object_set_data_full(G_OBJECT(widget), "message", g_strdup(message), g_free);
    return widget;
}
static GtkWidget *button(const char *message, GCallback callback, App *app)
{
    GtkWidget *widget = gtk_button_new_with_label(_(message));
    g_object_set_data_full(G_OBJECT(widget), "message", g_strdup(message), g_free);
    g_signal_connect(widget, "clicked", callback, app);
    return widget;
}
static GtkWidget *check(const char *message)
{
    GtkWidget *widget = gtk_check_button_new_with_label(_(message));
    g_object_set_data_full(G_OBJECT(widget), "message", g_strdup(message), g_free);
    return widget;
}
static void retranslate(GtkWidget *widget)
{
    const char *message = g_object_get_data(G_OBJECT(widget), "message");
    if (message) {
        if (GTK_IS_LABEL(widget)) gtk_label_set_text(GTK_LABEL(widget), _(message));
        else if (GTK_IS_BUTTON(widget)) gtk_button_set_label(GTK_BUTTON(widget), _(message));
        else if (GTK_IS_CHECK_BUTTON(widget)) gtk_check_button_set_label(GTK_CHECK_BUTTON(widget), _(message));
    }
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child)) retranslate(child);
}
static void dismiss_end_pause(App *app)
{
    if (!app->end_dialog) return;
    GtkWidget *dialog=app->end_dialog;
    app->end_dialog=NULL;
    app->paused=FALSE;
    gtk_window_destroy(GTK_WINDOW(dialog));
}
static void acknowledge_end_pause(GtkButton *button_widget, gpointer data)
{
    (void)button_widget;
    App *app=data;
    dismiss_end_pause(app);
    update(app);
}
static gboolean close_end_pause(GtkWindow *window, gpointer data)
{
    (void)window;
    acknowledge_end_pause(NULL,data);
    return TRUE;
}
static void show_end_pause(App *app)
{
    if (!app->pause_at_end || !game_result(&app->game) || app->end_dialog || app->closing) return;
    app->paused=TRUE;
    app->end_dialog=gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(app->end_dialog),_("Attempt finished"));
    gtk_window_set_transient_for(GTK_WINDOW(app->end_dialog),GTK_WINDOW(app->window));
    gtk_window_set_modal(GTK_WINDOW(app->end_dialog),TRUE);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(app->end_dialog),TRUE);
    gtk_window_set_resizable(GTK_WINDOW(app->end_dialog),FALSE);
    GtkWidget *content=gtk_box_new(GTK_ORIENTATION_VERTICAL,16);
    gtk_widget_set_margin_top(content,24);gtk_widget_set_margin_bottom(content,24);
    gtk_widget_set_margin_start(content,24);gtk_widget_set_margin_end(content,24);
    gtk_box_append(GTK_BOX(content),label(game_result(&app->game)==1?"Solved!":"No legal placements remain."));
    gtk_box_append(GTK_BOX(content),label("Paused until you select Continue. The final board will remain visible."));
    GtkWidget *ack=button("Continue",G_CALLBACK(acknowledge_end_pause),app);
    gtk_widget_add_css_class(ack,"suggested-action");gtk_box_append(GTK_BOX(content),ack);
    gtk_window_set_child(GTK_WINDOW(app->end_dialog),content);
    gtk_window_set_default_widget(GTK_WINDOW(app->end_dialog),ack);
    g_signal_connect(app->end_dialog,"close-request",G_CALLBACK(close_end_pause),app);
    gtk_window_present(GTK_WINDOW(app->end_dialog));
}
static void cancel_work(App *app)
{
    dismiss_end_pause(app);
    app->generation++;
    if (app->cancel) g_cancellable_cancel(app->cancel);
    if (app->tick) { gtk_widget_remove_tick_callback(app->board, app->tick); app->tick = 0; }
    app->animation = (Animation) {0};
    app->ready = FALSE;
    app->pending = -1;
    g_clear_pointer(&app->pending_details, g_free);
}
static void clear_attempt(App *app)
{
    cancel_work(app);
    game_init(&app->game);
    for (unsigned i = 0; i < 9; i++) g_clear_pointer(&app->details[i], g_free);
    app->selected = -1; app->paused = FALSE; app->saved = FALSE; app->failed = FALSE;
    set_error(app, NULL);
    app->seed = g_random_int();
    g_rand_set_seed(app->random, app->seed);
}
static void save_attempt(App *app)
{
    if (app->saved || !game_result(&app->game)) return;
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    g_autofree char *timestamp = g_date_time_format_iso8601(now);
    g_autoptr(GString) report = g_string_new("# Pentalpha attempt\n\n");
    g_string_append_printf(report, "%s; controller: %s; seed: %u; depth: %u; assistance: %s; thinking: %s; result: %s; pieces: %u/9.\n\n",
        timestamp, player_name(app->kind), app->seed, app->search_depth, app->options.assisted ? "on" : "off",
        app->options.thinking ? "on" : "off", game_result(&app->game) == 1 ? "solved" : "stuck", app->game.moves);
    for (unsigned i = 0; i < app->game.moves; i++) {
        Move m = app->game.history[i];
        g_string_append_printf(report, "## Placement %u\n\n%s -> %s -> %s\n\n%s", i + 1,
            game_labels[m.entry], game_labels[m.middle], game_labels[m.destination], app->details[i] ? app->details[i] : "");
    }
    g_string_append(report, "---\n\n");
    GError *error = NULL;
    if (!game_append_report(GAME_HISTORY_FILE, report->str, &error)) {
        set_error(app, error->message); g_clear_error(&error);
    } else app->saved = TRUE;
}
static void commit(App *app)
{
    int move = app->pending;
    app->animation.active = FALSE;
    app->pending = -1;
    app->ready = FALSE;
    if (move >= 0 && game_play(&app->game, move)) {
        app->details[app->game.moves - 1] = g_strdup_printf("%sAnimation duration: %.3f ms (half blink, half slide).\n\n",
            app->pending_details ? app->pending_details : "", app->animation.duration / 1000.0);
        app->selected = -1;
        save_attempt(app);
        show_end_pause(app);
    }
    g_clear_pointer(&app->pending_details, g_free);
    update(app);
    schedule(app);
}
static gboolean tick(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
    (void) widget;
    App *app = data;
    AnimationFrame frame = animation_frame(&app->animation, gdk_frame_clock_get_frame_time(clock));
    gtk_widget_queue_draw(app->board);
    if (!app->paused && frame.finished) {
        app->tick = 0;
        commit(app);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}
static void start_animation(App *app, int move)
{
    if (!app->playing || app->closing || app->animation.active || !game_legal(&app->game, move)) return;
    app->pending = move;
    app->ready = FALSE;
    animation_start(&app->animation, g_get_monotonic_time(), gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->delay)));
    if (app->animation.duration == 0) { commit(app); return; }
    app->tick = gtk_widget_add_tick_callback(app->board, tick, app, NULL);
    update(app);
}
static gboolean search_cancelled(gpointer data) { return g_cancellable_is_cancelled(data); }
static void job_free(gpointer data)
{
    Job *job = data;
    g_free(job->model); g_free(job->url); g_free(job->details); g_strfreev(job->models); g_free(job);
}
static void worker(GTask *task, gpointer source, gpointer task_data, GCancellable *cancel)
{
    (void) source;
    Job *job = task_data;
    GError *error = NULL;
    if (job->list) {
        job->models = ollama_list_models(job->url, OLLAMA_LIST_TIMEOUT_MS, cancel, &error);
        if (error) g_task_return_error(task, error); else g_task_return_boolean(task, TRUE);
        return;
    }
    g_autoptr(GRand) random = g_rand_new_with_seed(job->seed);
    g_autoptr(GString) details = g_string_new(NULL);
    gint64 start = g_get_monotonic_time();
    guint32 legal = game_legal_moves(&job->game), candidates = legal;
    if (job->kind == PLAYER_SEARCH) {
        SearchControl control = {.cancelled = search_cancelled, .data = cancel};
        if (!search_evaluate(&job->game, job->depth, &control, &job->search)) {
            g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled"); return;
        }
        candidates = job->search.best;
        g_string_append_printf(details, "Search depth: %u/%u; nodes: %" G_GUINT64_FORMAT "; elapsed: %.3f ms.\n\n",
            job->search.completed_depth, job->search.requested_depth, job->search.nodes, job->search.elapsed_us / 1000.0);
    } else if (job->kind == PLAYER_OLLAMA) {
        if (!ollama_choose_with_options(&job->game, job->model, job->url, OLLAMA_MOVE_TIMEOUT_MS, cancel,
                &job->options, &job->ollama, &error)) { g_task_return_error(task, error); return; }
        candidates = 1u << job->ollama.move;
        ollama_record(details, job->model, &job->ollama);
    } else if (job->kind == PLAYER_HEURISTIC) {
        int best = -1; candidates = 0;
        for (unsigned m = 0; m < 20; m++) if (legal & (1u << m)) {
            int score = game_heuristic(&job->game, m);
            if (score > best) { best = score; candidates = 0; }
            if (score == best) candidates |= 1u << m;
        }
    }
    g_string_append(details, "| Entry | Middle | Destination | Heuristic | Search |\n| --- | --- | --- | --- | --- |\n");
    for (unsigned m = 0; m < 20; m++) if (legal & (1u << m)) {
        Move move = game_moves[m];
        g_string_append_printf(details, "| %s | %s | %s | %d | ", game_labels[move.entry], game_labels[move.middle],
            game_labels[move.destination], game_heuristic(&job->game, m));
        if (job->kind == PLAYER_SEARCH) g_string_append_printf(details, "%d", job->search.values[m]);
        else g_string_append(details, "—");
        g_string_append(details, " |\n");
    }
    job->chosen = game_random_move(candidates, random);
    g_string_append_printf(details, "\nDecision seed: %u; elapsed: %.3f ms.\n\n", job->seed, (g_get_monotonic_time() - start) / 1000.0);
    job->details = g_string_free(g_steal_pointer(&details), FALSE);
    if (g_cancellable_is_cancelled(cancel)) g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled");
    else g_task_return_boolean(task, TRUE);
}
static void done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void) source;
    App *app = data;
    Job *job = g_task_get_task_data(G_TASK(result));
    GError *error = NULL;
    gboolean ok = g_task_propagate_boolean(G_TASK(result), &error);
    g_clear_object(&app->task);
    g_clear_object(&app->cancel);
    if (!app->closing && job->list) {
        if (ok) {
            GtkStringList *models = gtk_string_list_new((const char * const *) job->models);
            gtk_drop_down_set_model(GTK_DROP_DOWN(app->model), G_LIST_MODEL(models));
            g_object_unref(models);
            if (!job->models[0]) set_error(app, _("No Ollama models found."));
        } else if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) set_error(app, error->message);
    } else if (!app->closing && job->generation == app->generation && app->playing) {
        if (ok && game_legal(&app->game, job->chosen)) {
            app->pending = job->chosen; app->ready = TRUE;
            app->pending_details = g_steal_pointer(&job->details);
        } else if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            app->failed = TRUE; set_error(app, error->message);
        }
    }
    g_clear_error(&error);
    if (!app->closing) { update(app); schedule(app); }
}
static void launch(App *app, gboolean list)
{
    if (app->task || app->closing) return;
    Job *job = g_new0(Job, 1);
    job->game = app->game; job->kind = app->kind; job->depth = app->search_depth;
    job->generation = app->generation; job->seed = g_rand_int(app->random);
    job->model = g_strdup(app->selected_model); job->url = g_strdup(app->url);
    job->options = app->options; job->list = list; job->chosen = -1;
    app->cancel = g_cancellable_new();
    app->task = g_task_new(NULL, app->cancel, done, app);
    g_task_set_task_data(app->task, job, job_free);
    g_task_set_return_on_cancel(app->task, FALSE);
    g_task_run_in_thread(app->task, worker);
    update(app);
}
static void schedule(App *app)
{
    if (!app->playing || app->closing || app->paused || app->failed || app->animation.active ||
        game_result(&app->game) || app->kind == PLAYER_HUMAN) return;
    if (app->ready) start_animation(app, app->pending);
    else if (!app->task) launch(app, FALSE);
}
static void begin_clicked(GtkButton *button_widget, gpointer data)
{
    (void) button_widget;
    App *app = data;
    app->playing = FALSE;
    clear_attempt(app);
    app->pause_at_end=gtk_check_button_get_active(GTK_CHECK_BUTTON(app->pause_at_end_switch));
    app->kind = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->controller));
    app->search_depth = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->depth));
    app->options = (OllamaOptions) {.assisted = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->assisted)),
        .thinking = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->thinking))};
    g_clear_pointer(&app->selected_model, g_free);
    GtkStringObject *model = gtk_drop_down_get_selected_item(GTK_DROP_DOWN(app->model));
    app->selected_model = g_strdup(model ? gtk_string_object_get_string(model) : "");
    if (app->kind == PLAYER_OLLAMA && !*app->selected_model) { set_error(app, _("Select an Ollama model first.")); update(app); return; }
    app->playing = TRUE;
    update(app); schedule(app);
}
static void restart_clicked(GtkButton *button_widget, gpointer data)
{
    (void) button_widget;
    App *app = data;
    clear_attempt(app); app->playing = TRUE;
    update(app); schedule(app);
}
static void configure_clicked(GtkButton *button_widget, gpointer data)
{
    (void) button_widget;
    App *app = data;
    app->playing = FALSE; clear_attempt(app); update(app);
}
static void pause_clicked(GtkButton *button_widget, gpointer data)
{
    (void) button_widget;
    App *app = data;
    app->paused = !app->paused;
    if (app->paused) animation_pause(&app->animation, g_get_monotonic_time());
    else { animation_resume(&app->animation, g_get_monotonic_time()); schedule(app); }
    update(app);
}
static void undo_clicked(GtkButton *button_widget, gpointer data)
{
    (void) button_widget;
    App *app = data;
    if (app->kind != PLAYER_HUMAN || app->animation.active || !game_undo(&app->game)) return;
    g_clear_pointer(&app->details[app->game.moves], g_free);
    dismiss_end_pause(app);
    app->saved = FALSE; app->selected = -1; set_error(app, NULL); update(app);
}
static void refresh_clicked(GtkButton *button_widget, gpointer data)
{
    (void) button_widget;
    App *app = data;
    if (!app->task && !app->playing) { set_error(app, NULL); launch(app, TRUE); }
}
static void update(App *app)
{
    if (!app->window) return;
    gboolean configuring = !app->playing;
    PlayerKind selected_kind = configuring ? gtk_drop_down_get_selected(GTK_DROP_DOWN(app->controller)) : app->kind;
    gboolean searching = selected_kind == PLAYER_SEARCH, modeling = selected_kind == PLAYER_OLLAMA;
    gtk_widget_set_visible(app->depth_label, searching); gtk_widget_set_visible(app->depth, searching);
    gtk_widget_set_visible(app->model_label, modeling); gtk_widget_set_visible(app->model, modeling);
    gtk_widget_set_visible(app->refresh, modeling); gtk_widget_set_visible(app->assisted, modeling);
    gtk_widget_set_visible(app->thinking, modeling);
    gtk_widget_set_visible(app->pause, app->playing && app->kind != PLAYER_HUMAN);
    gtk_widget_set_visible(app->undo, app->playing && app->kind == PLAYER_HUMAN);
    gtk_widget_set_visible(app->restart, app->playing); gtk_widget_set_visible(app->configure, app->playing);
    gtk_widget_set_sensitive(app->begin, !modeling || gtk_drop_down_get_selected_item(GTK_DROP_DOWN(app->model)) != NULL);
    gtk_widget_set_sensitive(app->pause_at_end_switch,configuring);
    gtk_widget_set_sensitive(app->controller, configuring);
    gtk_widget_set_sensitive(app->depth, configuring);
    gtk_widget_set_sensitive(app->model, configuring);
    gtk_widget_set_sensitive(app->assisted, configuring);
    gtk_widget_set_sensitive(app->thinking, configuring);
    gtk_widget_set_sensitive(app->refresh, configuring && !app->task);
    gtk_widget_set_visible(app->begin, configuring);
    gtk_widget_set_sensitive(app->restart, app->playing);
    gtk_widget_set_sensitive(app->configure, app->playing);
    gtk_widget_set_sensitive(app->undo, app->playing && app->kind == PLAYER_HUMAN && app->game.moves > 0 && !app->animation.active);
    gtk_widget_set_sensitive(app->pause, app->playing && app->kind != PLAYER_HUMAN && !game_result(&app->game));
    gtk_button_set_label(GTK_BUTTON(app->pause), _(app->paused ? "Resume" : "Pause"));
    const char *state = configuring ? _("Choose a controller and begin.") : game_result(&app->game) == 1 ? _("Solved!") :
        game_result(&app->game) == 2 ? _("No legal placements remain.") : app->failed ? _("Decision failed. Restart or change configuration.") :
        app->paused ? _("Paused") : app->animation.active ? _("Placing piece…") : app->kind == PLAYER_HUMAN ?
        _(app->selected < 0 ? "Select an empty entry point." : "Select a highlighted destination.") : _("Finding a placement…");
    g_autofree char *text = g_strdup_printf(_("%u/9 placed · %u remaining\n%s"), app->game.moves, 9 - app->game.moves, state);
    gtk_label_set_text(GTK_LABEL(app->status), text);
    gtk_widget_queue_draw(app->board);
}
static void board_transform(App *app, double *scale, double *x, double *y)
{
    double width = gtk_widget_get_width(app->board), height = gtk_widget_get_height(app->board);
    *scale = MIN(width, height) / 502.;
    *x = (width - *scale * 502) / 2; *y = (height - *scale * 502) / 2;
}
static void circle(cairo_t *cr, double x, double y, double radius)
{
    cairo_arc(cr, x, y, radius, 0, 2 * G_PI);
}
static void draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
    (void) area; (void) width; (void) height;
    App *app = data;
    double scale, x, y; board_transform(app, &scale, &x, &y);
    cairo_set_source_rgb(cr, .055, .065, .095); cairo_paint(cr);
    cairo_translate(cr, x, y); cairo_scale(cr, scale, scale);
    if (app->background) {
        cairo_save(cr); cairo_scale(cr, 502. / cairo_image_surface_get_width(app->background), 502. / cairo_image_surface_get_height(app->background));
        cairo_set_source_surface(cr, app->background, 0, 0); cairo_paint(cr); cairo_restore(cr);
    }
    if (app->selected >= 0 && !app->animation.active) {
        for (unsigned m = 0; m < 20; m++) if (game_legal(&app->game, m) && game_moves[m].entry == (unsigned) app->selected) {
            Move move = game_moves[m];
            cairo_set_source_rgba(cr, .3, .9, 1, .65); cairo_set_line_width(cr, 3);
            cairo_move_to(cr, point_x[move.entry], point_y[move.entry]); cairo_line_to(cr, point_x[move.destination], point_y[move.destination]); cairo_stroke(cr);
            circle(cr, point_x[move.destination], point_y[move.destination], 16); cairo_stroke(cr);
        }
    }
    static const double label_dx[10] = {22, -8, 15, -25, -9, 18, 18, -8, -32, -30};
    static const double label_dy[10] = {4, 32, 22, 22, 32, -12, 4, 32, 4, -12};
    for (unsigned p = 0; p < 10; p++) {
        if (app->game.occupied & (1u << p)) {
            circle(cr, point_x[p], point_y[p], 11); cairo_set_source_rgb(cr, .25, .85, .95); cairo_fill(cr);
            circle(cr, point_x[p] - 3, point_y[p] - 3, 3); cairo_set_source_rgba(cr, 1, 1, 1, .65); cairo_fill(cr);
        }
        if ((int) p == app->selected) { circle(cr, point_x[p], point_y[p], 18); cairo_set_source_rgb(cr, 1, .8, .25); cairo_set_line_width(cr, 3); cairo_stroke(cr); }
        cairo_set_source_rgb(cr, 1, 1, .9); cairo_set_font_size(cr, 13);
        cairo_move_to(cr, point_x[p] + label_dx[p], point_y[p] + label_dy[p]); cairo_show_text(cr, game_labels[p]);
    }
    if (app->animation.active && app->pending >= 0) {
        Move move = game_moves[app->pending];
        AnimationFrame frame = animation_frame(&app->animation, g_get_monotonic_time());
        double px = point_x[move.entry] + frame.position * (point_x[move.destination] - point_x[move.entry]);
        double py = point_y[move.entry] + frame.position * (point_y[move.destination] - point_y[move.entry]);
        circle(cr, px, py, 12); cairo_set_source_rgba(cr, .35, .95, 1, frame.opacity); cairo_fill(cr);
        circle(cr, px, py, 15); cairo_set_source_rgba(cr, 1, 1, 1, frame.opacity); cairo_set_line_width(cr, 2); cairo_stroke(cr);
    }
}
static void select_point(App *app, int point)
{
    if (!app->playing || app->kind != PLAYER_HUMAN || app->animation.active || game_result(&app->game) || point < 0) return;
    if (app->selected >= 0) {
        int move = game_find_move(app->selected, point);
        if (move >= 0 && game_legal(&app->game, move)) {
            g_autoptr(GString) details = g_string_new("Human placement.\n\n");
            for (unsigned m = 0; m < 20; m++) if (game_legal(&app->game, m))
                g_string_append_printf(details, "%s -> %s: heuristic %d.\n", game_labels[game_moves[m].entry],
                    game_labels[game_moves[m].destination], game_heuristic(&app->game, m));
            g_string_append_c(details, '\n');
            app->pending_details = g_string_free(g_steal_pointer(&details), FALSE);
            start_animation(app, move); return;
        }
        if (app->selected == point) { app->selected = -1; update(app); return; }
    }
    app->selected = -1;
    for (unsigned m = 0; m < 20; m++) if (game_legal(&app->game, m) && game_moves[m].entry == (unsigned) point) {
        app->selected = point; break;
    }
    update(app);
}
static void click(GtkGestureClick *gesture, int presses, double x, double y, gpointer data)
{
    (void) gesture; (void) presses;
    App *app = data;
    double scale, dx, dy; board_transform(app, &scale, &dx, &dy);
    if (scale <= 0) return;
    x = (x - dx) / scale; y = (y - dy) / scale;
    for (unsigned p = 0; p < 10; p++) if (hypot(x - point_x[p], y - point_y[p]) < 22) { select_point(app, p); return; }
}
static gboolean key_pressed(GtkEventControllerKey *controller, guint key, guint code, GdkModifierType state, gpointer data)
{
    (void) controller; (void) code; (void) state;
    App *app = data;
    if (key == GDK_KEY_Escape) { app->selected = -1; update(app); return TRUE; }
    return FALSE;
}
static void controller_changed(GObject *object, GParamSpec *spec, gpointer data)
{
    (void) object; (void) spec;
    App *app = data;
    update(app);
    if (!app->playing && gtk_drop_down_get_selected(GTK_DROP_DOWN(app->controller)) == PLAYER_OLLAMA &&
        !app->task && !gtk_drop_down_get_selected_item(GTK_DROP_DOWN(app->model))) launch(app, TRUE);
}
static void model_changed(GObject *object, GParamSpec *spec, gpointer data)
{
    (void) object; (void) spec;
    update(data);
}
static void language_changed(GObject *object, GParamSpec *spec, gpointer data)
{
    (void) spec;
    App *app = data;
    i18n_set_language(gtk_drop_down_get_selected(GTK_DROP_DOWN(object)));
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->controller));
    const char *names[] = {_("Human"), _("Random"), _("Heuristic"), _("Search"), "Ollama", NULL};
    GtkStringList *model = gtk_string_list_new(names);
    gtk_drop_down_set_model(GTK_DROP_DOWN(app->controller), G_LIST_MODEL(model)); g_object_unref(model);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller), selected);
    retranslate(app->window);
    if(app->end_dialog) {
        gtk_window_set_title(GTK_WINDOW(app->end_dialog),_("Attempt finished"));
        retranslate(app->end_dialog);
    } gtk_window_set_title(GTK_WINDOW(app->window), _(GAME_NAME));
    if (app->error) gtk_label_set_text(GTK_LABEL(app->error_label), _(app->error));
    update(app);
    GError *error = NULL;
    g_autofree char *directory = g_path_get_dirname(app->preferences);
    if (g_mkdir_with_parents(directory, 0700) < 0 || !i18n_save(app->preferences, &error)) {
        set_error(app, error ? error->message : _("Could not save language preference.")); g_clear_error(&error);
    }
}
static gboolean close_window(GtkWindow *window, gpointer data)
{
    (void) window;
    App *app = data;
    app->closing = TRUE; cancel_work(app); app->window = NULL;
    return FALSE;
}
static void activate(GtkApplication *application, gpointer data)
{
    App *app = data;
    if (app->window) { gtk_window_present(GTK_WINDOW(app->window)); return; }
    app->window = adw_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(app->window), _(GAME_NAME));
    gtk_window_set_default_size(GTK_WINDOW(app->window), 940, 760);
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *header = adw_header_bar_new(); gtk_box_append(GTK_BOX(root), header);
    const char *languages[] = {"System", "English", "Português", "Español", "Italiano", NULL};
    app->language = gtk_drop_down_new_from_strings(languages);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language), i18n_selection());
    adw_header_bar_pack_end(ADW_HEADER_BAR(header), app->language);
    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_margin_start(body, 16); gtk_widget_set_margin_end(body, 16); gtk_widget_set_margin_bottom(body, 16);
    gtk_box_append(GTK_BOX(root), body); gtk_widget_set_vexpand(body, TRUE);
    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request(sidebar, 260, -1);
    GtkWidget *scroll = gtk_scrolled_window_new(); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), sidebar);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, 280, -1); gtk_box_append(GTK_BOX(body), scroll);
    gtk_box_append(GTK_BOX(sidebar), label("Controller"));
    const char *controllers[] = {_("Human"), _("Random"), _("Heuristic"), _("Search"), "Ollama", NULL};
    app->controller = gtk_drop_down_new_from_strings(controllers); gtk_box_append(GTK_BOX(sidebar), app->controller);
    app->depth_label = label("Search depth"); gtk_box_append(GTK_BOX(sidebar), app->depth_label);
    app->depth = gtk_spin_button_new_with_range(1, 9, 1); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->depth), 9); gtk_box_append(GTK_BOX(sidebar), app->depth);
    app->model_label = label("Ollama model"); gtk_box_append(GTK_BOX(sidebar), app->model_label);
    const char *empty[] = {NULL}; app->model = gtk_drop_down_new_from_strings(empty); gtk_box_append(GTK_BOX(sidebar), app->model);
    app->refresh = button("Refresh models", G_CALLBACK(refresh_clicked), app); gtk_box_append(GTK_BOX(sidebar), app->refresh);
    app->assisted = check("Search assistance"); gtk_box_append(GTK_BOX(sidebar), app->assisted);
    app->thinking = check("Extended thinking"); gtk_box_append(GTK_BOX(sidebar), app->thinking);
    gtk_box_append(GTK_BOX(sidebar), label("Animation delay (seconds)"));
    app->delay = gtk_spin_button_new_with_range(0, 3, .1); gtk_spin_button_set_digits(GTK_SPIN_BUTTON(app->delay), 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay), .5); gtk_box_append(GTK_BOX(sidebar), app->delay);
    app->pause_at_end_switch=check("Pause at the end (solved or blocked)");
    gtk_box_append(GTK_BOX(sidebar),app->pause_at_end_switch);
    app->begin = button("Begin", G_CALLBACK(begin_clicked), app); gtk_widget_add_css_class(app->begin, "suggested-action"); gtk_box_append(GTK_BOX(sidebar), app->begin);
    app->pause = button("Pause", G_CALLBACK(pause_clicked), app); gtk_box_append(GTK_BOX(sidebar), app->pause);
    app->undo = button("Undo", G_CALLBACK(undo_clicked), app); gtk_box_append(GTK_BOX(sidebar), app->undo);
    app->restart = button("Restart", G_CALLBACK(restart_clicked), app); gtk_box_append(GTK_BOX(sidebar), app->restart);
    app->configure = button("New configuration", G_CALLBACK(configure_clicked), app); gtk_box_append(GTK_BOX(sidebar), app->configure);
    app->status = gtk_label_new(NULL); gtk_label_set_wrap(GTK_LABEL(app->status), TRUE); gtk_label_set_xalign(GTK_LABEL(app->status), 0);
    gtk_box_append(GTK_BOX(sidebar), app->status);
    app->error_label = gtk_label_new(NULL); gtk_label_set_wrap(GTK_LABEL(app->error_label), TRUE); gtk_widget_add_css_class(app->error_label, "error");
    gtk_box_append(GTK_BOX(sidebar), app->error_label); gtk_widget_set_visible(app->error_label, FALSE);
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10); gtk_widget_set_hexpand(right, TRUE); gtk_box_append(GTK_BOX(body), right);
    app->board = gtk_drawing_area_new(); gtk_widget_set_size_request(app->board, 400, 400); gtk_widget_set_vexpand(app->board, TRUE);
    gtk_widget_set_hexpand(app->board, TRUE); gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->board), draw, app, NULL);
    gtk_widget_set_focusable(app->board, TRUE); gtk_box_append(GTK_BOX(right), app->board);
    GtkWidget *rules = label("Place nine pieces. Enter an empty point, pass over the next point, and stop at the following empty point on the same straight line. The middle point may be occupied.");
    gtk_label_set_wrap(GTK_LABEL(rules), TRUE); gtk_box_append(GTK_BOX(right), rules);
    GtkGesture *gesture = gtk_gesture_click_new(); g_signal_connect(gesture, "pressed", G_CALLBACK(click), app);
    gtk_widget_add_controller(app->board, GTK_EVENT_CONTROLLER(gesture));
    GtkEventController *keys = gtk_event_controller_key_new(); g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), app);
    gtk_widget_add_controller(app->window, keys);
    g_signal_connect(app->language, "notify::selected", G_CALLBACK(language_changed), app);
    g_signal_connect(app->controller, "notify::selected", G_CALLBACK(controller_changed), app);
    g_signal_connect(app->model, "notify::selected", G_CALLBACK(model_changed), app);
    g_signal_connect(app->window, "close-request", G_CALLBACK(close_window), app);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(app->window), root);
    GError *error = NULL;
    g_autoptr(GdkPixbuf) pixbuf = gdk_pixbuf_new_from_resource_at_scale("/io/github/aigames/Pentalpha/board.svg", 1004, 1004, TRUE, &error);
    if (!pixbuf) { set_error(app, error->message); g_clear_error(&error); }
    else {
        int w = gdk_pixbuf_get_width(pixbuf), h = gdk_pixbuf_get_height(pixbuf);
        int channels = gdk_pixbuf_get_n_channels(pixbuf), stride = gdk_pixbuf_get_rowstride(pixbuf);
        app->background = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        unsigned char *output = cairo_image_surface_get_data(app->background);
        const unsigned char *pixels = gdk_pixbuf_read_pixels(pixbuf);
        int out_stride = cairo_image_surface_get_stride(app->background);
        for (int row = 0; row < h; row++) for (int column = 0; column < w; column++) {
            const unsigned char *p = pixels + row * stride + column * channels;
            unsigned alpha = channels == 4 ? p[3] : 255;
            ((guint32 *) (output + row * out_stride))[column] = (alpha << 24) |
                ((p[0] * alpha / 255) << 16) | ((p[1] * alpha / 255) << 8) | (p[2] * alpha / 255);
        }
        cairo_surface_mark_dirty(app->background);
    }
    update(app); gtk_window_present(GTK_WINDOW(app->window));
}
int main(int argc, char **argv)
{
    App app = {.selected = -1, .pending = -1};
    app.random = g_rand_new();
    app.url = g_strdup(g_getenv("OLLAMA_URL"));
    app.preferences = g_build_filename(g_get_user_config_dir(), "ai-games", "pentalpha.ini", NULL);
    GError *error = NULL;
    if (!i18n_load(app.preferences, &error)) { g_printerr("%s\n", error->message); g_clear_error(&error); }
    app.application = GTK_APPLICATION(adw_application_new("io.github.aigames.Pentalpha", G_APPLICATION_DEFAULT_FLAGS));
    g_signal_connect(app.application, "activate", G_CALLBACK(activate), &app);
    int status = g_application_run(G_APPLICATION(app.application), argc, argv);
    app.closing = TRUE;
    if (app.cancel) g_cancellable_cancel(app.cancel);
    while (app.task) g_main_context_iteration(NULL, TRUE);
    for (unsigned i = 0; i < 9; i++) g_free(app.details[i]);
    g_clear_pointer(&app.background, cairo_surface_destroy); g_object_unref(app.application); g_rand_free(app.random);
    g_free(app.preferences); g_free(app.url); g_free(app.selected_model); g_free(app.error); g_free(app.pending_details);
    return status;
}
