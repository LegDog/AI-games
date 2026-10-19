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
    unsigned generation;
    SearchLimits limits;
    GMutex progress_mutex;
    guint64 nodes; gint64 elapsed; gsize memory;
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
    GtkWidget *controller, *board_type, *center, *time_limit, *memory_limit, *time_label, *memory_label;
    GtkWidget *model, *model_label, *refresh, *assisted, *thinking, *delay, *language, *retry;
    GRand *random;
    Game game;
    Animation animation;
    PlayerKind kind;
    unsigned generation;
    SearchLimits limits;
    SearchResult plan;
    unsigned plan_index;
    guint64 plan_expected;
    GString *diagnostics;
    guint32 seed;
    char *selected_model, *url, *preferences, *error, *pending_details, *details[MAX_JUMPS];
    OllamaOptions options;
    gboolean playing, paused, ready, closing, saved, failed;
    int selected, pending;
    guint tick, progress_source;
    GTask *task;
    GCancellable *cancel;
} App;

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
static void cancel_work(App *app)
{
    app->generation++;
    app->plan = (SearchResult) {0}; app->plan_index = 0;
    if (app->progress_source) { g_source_remove(app->progress_source); app->progress_source = 0; }
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
    BoardKind board = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->board_type));
    game_init(&app->game, board, board == BOARD_ENGLISH && gtk_check_button_get_active(GTK_CHECK_BUTTON(app->center)) ? GOAL_CENTER : GOAL_ANY);
    g_string_truncate(app->diagnostics, 0);
    for (unsigned i = 0; i < MAX_JUMPS; i++) g_clear_pointer(&app->details[i], g_free);
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
    g_autoptr(GString) report = g_string_new("# Peg Solitaire attempt\n\n");
    const Board *board = game_board(app->game.board);
    g_string_append_printf(report, "%s; controller: %s; board: %s; goal: %s; seed: %u; planning limit: %.3f s / %zu bytes; assistance: %s; thinking: %s; result: %s; pegs remaining: %u; jumps: %u.\n\n",
        timestamp, player_name(app->kind), board->name, app->game.goal == GOAL_CENTER ? "center" : "any", app->seed,
        app->limits.seconds, app->limits.memory_bytes, app->options.assisted ? "on" : "off", app->options.thinking ? "on" : "off",
        game_result_name(game_result(&app->game)), game_count(app->game.occupied), app->game.moves);
    g_string_append(report, app->diagnostics->str);
    for (unsigned i = 0; i < app->game.moves; i++) {
        Move m = board->jumps[app->game.history[i]];
        g_string_append_printf(report, "## Jump %u\n\n%s -> %s -> %s\n\n%s", i + 1,
            board->labels[m.source], board->labels[m.middle], board->labels[m.destination], app->details[i] ? app->details[i] : "");
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
        app->details[app->game.moves - 1] = g_strdup_printf("%sAnimation duration: %.3f ms (half blink, half hop).\n\n",
            app->pending_details ? app->pending_details : "", app->animation.duration / 1000.0);
        if ((app->kind == PLAYER_BACKTRACKING || app->kind == PLAYER_ASTAR) && app->plan_index < app->plan.length &&
            app->plan.path[app->plan_index] == (unsigned) move) { app->plan_index++; app->plan_expected = app->game.occupied; }
        app->selected = -1;
        save_attempt(app);
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
    g_mutex_clear(&job->progress_mutex);
    g_free(job->model); g_free(job->url); g_free(job->details); g_strfreev(job->models); g_free(job);
}
static void search_progress(guint64 nodes, gint64 elapsed, gsize memory, gpointer data)
{
    Job *job = data; g_mutex_lock(&job->progress_mutex);
    job->nodes = nodes; job->elapsed = elapsed; job->memory = memory;
    g_mutex_unlock(&job->progress_mutex);
}
typedef struct { Job *job; GCancellable *cancel; } WorkerControl;
static gboolean worker_cancelled(gpointer data) { return search_cancelled(((WorkerControl *) data)->cancel); }
static void worker_progress(guint64 nodes, gint64 elapsed, gsize memory, gpointer data)
{
    search_progress(nodes,elapsed,memory,((WorkerControl *) data)->job);
}
static void worker(GTask *task, gpointer source, gpointer task_data, GCancellable *cancel)
{
    (void) source; Job *job = task_data; GError *error = NULL;
    if (job->list) {
        job->models = ollama_list_models(job->url, OLLAMA_LIST_TIMEOUT_MS, cancel, &error);
        if (error) g_task_return_error(task,error); else g_task_return_boolean(task,TRUE);
        return;
    }
    g_autoptr(GRand) random = g_rand_new_with_seed(job->seed);
    g_autoptr(GString) details = g_string_new(NULL);
    gint64 start = g_get_monotonic_time(); MoveList legal = game_legal_moves(&job->game), candidates = {0};
    if (job->kind == PLAYER_BACKTRACKING || job->kind == PLAYER_ASTAR) {
        WorkerControl state = {job,cancel}; SearchControl control = {.cancelled = worker_cancelled,.progress = worker_progress,.data = &state};
        job->search = search_solve(&job->game,job->kind,&job->limits,&control,job->seed);
        if (job->search.status == SEARCH_SOLVED && job->search.length) candidates.items[candidates.count++] = job->search.path[0];
        g_string_append_printf(details,"Search: %s; nodes: %" G_GUINT64_FORMAT "; elapsed: %.3f ms; peak solver memory: %zu bytes; planned jumps: %u.\n\n",
            search_status_name(job->search.status),job->search.nodes,job->search.elapsed_us/1000.0,job->search.peak_memory,job->search.length);
    } else if (job->kind == PLAYER_OLLAMA) {
        if (!ollama_choose_with_options(&job->game,job->model,job->url,OLLAMA_MOVE_TIMEOUT_MS,cancel,&job->options,&job->ollama,&error)) {
            g_task_return_error(task,error); return;
        }
        candidates.items[candidates.count++] = job->ollama.move; ollama_record(details,job->model,&job->ollama);
    } else if (job->kind == PLAYER_RANDOM) candidates = legal;
    else {
        gint64 best = G_MININT64;
        for (unsigned i = 0; i < legal.count; i++) {
            gint64 score = game_heuristic(&job->game,legal.items[i]);
            if (score > best) { best = score; candidates.count = 0; }
            if (score == best) candidates.items[candidates.count++] = legal.items[i];
        }
    }
    const Board *board = game_board(job->game.board);
    g_string_append(details,"| Source | Captured | Destination | Heuristic |\n| --- | --- | --- | --- |\n");
    for (unsigned i = 0; i < legal.count; i++) {
        unsigned index = legal.items[i]; Move m = board->jumps[index];
        g_string_append_printf(details,"| %s | %s | %s | %" G_GINT64_FORMAT " |\n",board->labels[m.source],board->labels[m.middle],board->labels[m.destination],game_heuristic(&job->game,index));
    }
    job->chosen = game_random_move(&candidates,random);
    g_string_append_printf(details,"\nDecision seed: %u; elapsed: %.3f ms.\n\n",job->seed,(g_get_monotonic_time()-start)/1000.0);
    job->details = g_string_free(g_steal_pointer(&details),FALSE);
    if (g_cancellable_is_cancelled(cancel)) g_task_return_new_error(task,G_IO_ERROR,G_IO_ERROR_CANCELLED,"Cancelled");
    else g_task_return_boolean(task,TRUE);
}
static void done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void) source;
    App *app = data;
    Job *job = g_task_get_task_data(G_TASK(result));
    GError *error = NULL;
    gboolean ok = g_task_propagate_boolean(G_TASK(result), &error);
    if (app->progress_source) { g_source_remove(app->progress_source); app->progress_source = 0; }
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
            if (job->kind == PLAYER_BACKTRACKING || job->kind == PLAYER_ASTAR) {
                app->plan = job->search; app->plan_index = 0; app->plan_expected = app->game.occupied;
            }
            app->pending = job->chosen; app->ready = TRUE;
            app->pending_details = g_steal_pointer(&job->details);
        } else if (ok && (job->kind == PLAYER_BACKTRACKING || job->kind == PLAYER_ASTAR)) {
            app->failed = TRUE;
            set_error(app, job->search.status == SEARCH_UNSOLVABLE ? _("No winning continuation exists.") :
                job->search.status == SEARCH_LIMIT ? _("Search limit reached. Adjust limits and Retry.") : _("Search allocation failed."));
            g_string_append(app->diagnostics,job->details ? job->details : "");
        } else if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            app->failed = TRUE; set_error(app, error->message);
            g_autofree char *safe = g_strescape(error->message,NULL);
            g_string_append_printf(app->diagnostics,"Decision error: %s\n\n",safe);
        }
    }
    g_clear_error(&error);
    if (!app->closing) { update(app); schedule(app); }
}
static SearchLimits selected_limits(App *app)
{
    return (SearchLimits) {gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->time_limit)),
        (gsize) gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->memory_limit))*1024*1024,0};
}
static gboolean progress_tick(gpointer data)
{
    App *app = data;
    if (!app->task || app->closing) { app->progress_source = 0; return G_SOURCE_REMOVE; }
    update(app); return G_SOURCE_CONTINUE;
}
static void launch(App *app, gboolean list)
{
    if (app->task || app->closing) return;
    Job *job = g_new0(Job, 1);
    job->game = app->game; job->kind = app->kind;
    app->limits = selected_limits(app); job->limits = app->limits;
    job->generation = app->generation; job->seed = g_rand_int(app->random);
    job->model = g_strdup(app->selected_model); job->url = g_strdup(app->url);
    job->options = app->options; job->options.limits = job->limits; job->list = list; job->chosen = -1;
    g_mutex_init(&job->progress_mutex);
    app->cancel = g_cancellable_new();
    app->task = g_task_new(NULL, app->cancel, done, app);
    g_task_set_task_data(app->task, job, job_free);
    g_task_set_return_on_cancel(app->task, FALSE);
    g_task_run_in_thread(app->task, worker);
    if (!list) app->progress_source = g_timeout_add(100,progress_tick,app);
    update(app);
}
static void schedule(App *app)
{
    if (!app->playing || app->closing || app->paused || app->failed || app->animation.active ||
        game_result(&app->game) || app->kind == PLAYER_HUMAN) return;
    if (!app->ready && (app->kind == PLAYER_BACKTRACKING || app->kind == PLAYER_ASTAR) && app->plan.length &&
        app->plan_index < app->plan.length && app->plan_expected == app->game.occupied) {
        int move = app->plan.path[app->plan_index];
        if (game_legal(&app->game,move)) {
            app->pending = move; app->ready = TRUE;
            app->pending_details = g_strdup_printf("Runtime plan step %u/%u; search nodes: %" G_GUINT64_FORMAT "; planning time: %.3f ms; solver memory: %zu bytes.\n\n",
                app->plan_index+1,app->plan.length,app->plan.nodes,app->plan.elapsed_us/1000.0,app->plan.peak_memory);
        } else app->plan.length = 0;
    }
    if (app->ready) start_animation(app, app->pending);
    else if (!app->task) launch(app, FALSE);
}
static void begin_clicked(GtkButton *button_widget, gpointer data)
{
    (void) button_widget;
    App *app = data;
    app->playing = FALSE;
    clear_attempt(app);
    app->kind = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->controller));
    app->limits = selected_limits(app);
    app->options = (OllamaOptions) {.assisted = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->assisted)),
        .thinking = gtk_check_button_get_active(GTK_CHECK_BUTTON(app->thinking)), .limits = app->limits};
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
    app->saved = FALSE; app->selected = -1; set_error(app, NULL); update(app);
}
static void retry_clicked(GtkButton *widget, gpointer data)
{
    (void) widget; App *app = data;
    if (!app->playing || !app->failed || app->task) return;
    cancel_work(app); app->failed = FALSE; app->paused = FALSE; set_error(app,NULL); update(app); schedule(app);
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
    gboolean config = !app->playing;
    PlayerKind kind = config ? gtk_drop_down_get_selected(GTK_DROP_DOWN(app->controller)) : app->kind;
    gboolean modeling = kind == PLAYER_OLLAMA;
    gboolean searching = kind == PLAYER_BACKTRACKING || kind == PLAYER_ASTAR || modeling;
    gtk_widget_set_sensitive(app->controller,config); gtk_widget_set_sensitive(app->board_type,config);
    gboolean english = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->board_type)) == BOARD_ENGLISH;
    gtk_widget_set_visible(app->center,english); gtk_widget_set_sensitive(app->center,config);
    gtk_widget_set_visible(app->time_label,searching); gtk_widget_set_visible(app->time_limit,searching);
    gtk_widget_set_visible(app->memory_label,searching); gtk_widget_set_visible(app->memory_limit,searching);
    gtk_widget_set_sensitive(app->time_limit,config || app->failed); gtk_widget_set_sensitive(app->memory_limit,config || app->failed);
    gtk_widget_set_visible(app->model_label,modeling); gtk_widget_set_visible(app->model,modeling);
    gtk_widget_set_visible(app->refresh,modeling); gtk_widget_set_visible(app->assisted,modeling); gtk_widget_set_visible(app->thinking,modeling);
    gtk_widget_set_sensitive(app->model,config); gtk_widget_set_sensitive(app->refresh,config && !app->task);
    gtk_widget_set_sensitive(app->assisted,config); gtk_widget_set_sensitive(app->thinking,config);
    gtk_widget_set_visible(app->begin,config); gtk_widget_set_sensitive(app->begin,!modeling || gtk_drop_down_get_selected_item(GTK_DROP_DOWN(app->model)) != NULL);
    gtk_widget_set_visible(app->pause,app->playing && kind != PLAYER_HUMAN);
    gtk_widget_set_sensitive(app->pause,app->playing && game_result(&app->game) == GAME_ONGOING && !app->failed);
    gtk_button_set_label(GTK_BUTTON(app->pause),_(app->paused ? "Resume" : "Pause"));
    gtk_widget_set_visible(app->undo,app->playing && kind == PLAYER_HUMAN);
    gtk_widget_set_sensitive(app->undo,app->game.moves > 0 && !app->animation.active);
    gtk_widget_set_visible(app->restart,app->playing); gtk_widget_set_visible(app->configure,app->playing);
    gtk_widget_set_visible(app->retry,app->playing && app->failed); gtk_widget_set_sensitive(app->retry,!app->task);
    GameResult result = game_result(&app->game);
    const char *state = config ? _("Choose a board and controller, then begin.") : result == GAME_SOLVED ? _("Solved!") :
        result == GAME_STUCK ? _("No legal jumps remain.") : result == GAME_WRONG_FINISH ? _("One peg remains outside the required center.") :
        app->failed ? _("Decision stopped. Adjust limits and Retry, or restart.") : app->paused ? _("Paused") : app->animation.active ? _("Jumping peg…") :
        kind == PLAYER_HUMAN ? _(app->selected < 0 ? "Select an occupied source peg." : "Select a highlighted destination.") : _("Planning jumps…");
    g_autoptr(GString) text = g_string_new(NULL);
    g_string_append_printf(text,_("%u pegs remaining · %u jumps\n%s"),game_count(app->game.occupied),app->game.moves,state);
    if (app->task && searching) {
        Job *job = g_task_get_task_data(app->task); guint64 nodes; gint64 elapsed; gsize memory;
        g_mutex_lock(&job->progress_mutex); nodes = job->nodes; elapsed = job->elapsed; memory = job->memory; g_mutex_unlock(&job->progress_mutex);
        if (nodes) g_string_append_printf(text,_("\nNodes: %u · %.1f s · %.1f MiB"),(unsigned) MIN(nodes,G_MAXUINT),elapsed/1000000.0,memory/1048576.0);
    }
    gtk_label_set_text(GTK_LABEL(app->status),text->str); gtk_widget_queue_draw(app->board);
}
static void board_transform(App *app, double *scale, double *x, double *y)
{
    double width = gtk_widget_get_width(app->board), height = gtk_widget_get_height(app->board);
    *scale = MIN(width, height) / 502.;
    *x = (width - *scale * 502) / 2; *y = (height - *scale * 502) / 2;
}
static void circle(cairo_t *cr, double x, double y, double radius)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x, y, radius, 0, 2 * G_PI);
}
static void draw(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
    (void) area; (void) width; (void) height; App *app = data;
    const Board *b = game_board(app->game.board);
    double scale,x,y; board_transform(app,&scale,&x,&y);
    cairo_set_source_rgb(cr,.055,.065,.095); cairo_paint(cr); cairo_translate(cr,x,y); cairo_scale(cr,scale,scale);
    AnimationFrame frame = animation_frame(&app->animation,g_get_monotonic_time());
    gboolean moving = app->animation.active && app->pending >= 0;
    Move active = moving ? b->jumps[app->pending] : (Move) {MAX_POINTS,MAX_POINTS,MAX_POINTS};
    for (unsigned p = 0; p < b->points; p++) {
        circle(cr,b->x[p],b->y[p],23); cairo_set_source_rgb(cr,.17,.18,.22); cairo_fill_preserve(cr);
        cairo_set_source_rgb(cr,.83,.69,.22); cairo_set_line_width(cr,2); cairo_stroke(cr);
        if ((app->game.occupied & POINT_BIT(p)) && (!moving || p != active.source)) {
            double opacity = moving && p == active.middle ? 1-CLAMP((frame.position-.75)*4,0,1) : 1;
            circle(cr,b->x[p],b->y[p],17); cairo_set_source_rgba(cr,.25,.85,.95,opacity); cairo_fill(cr);
            circle(cr,b->x[p]-5,b->y[p]-5,4); cairo_set_source_rgba(cr,1,1,1,.65*opacity); cairo_fill(cr);
        }
        if ((int) p == app->selected && !moving) { circle(cr,b->x[p],b->y[p],26); cairo_set_source_rgb(cr,1,.8,.25); cairo_set_line_width(cr,3); cairo_stroke(cr); }
        cairo_set_source_rgb(cr,1,1,.9); cairo_set_font_size(cr,12); cairo_move_to(cr,b->x[p]-8,b->y[p]+37); cairo_show_text(cr,b->labels[p]);
    }
    if (app->selected >= 0 && !moving) {
        MoveList legal = game_legal_moves(&app->game);
        for (unsigned i = 0; i < legal.count; i++) {
            Move m = b->jumps[legal.items[i]]; if (m.source != (unsigned) app->selected) continue;
            circle(cr,b->x[m.destination],b->y[m.destination],27); cairo_set_source_rgb(cr,.4,1,.7); cairo_set_line_width(cr,3); cairo_stroke(cr);
        }
    }
    if (moving) {
        double px = b->x[active.source]+frame.position*(b->x[active.destination]-b->x[active.source]);
        double py = b->y[active.source]+frame.position*(b->y[active.destination]-b->y[active.source])-25*sin(G_PI*frame.position);
        circle(cr,px,py,18); cairo_set_source_rgba(cr,.35,.95,1,frame.opacity); cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr,1,1,1,frame.opacity); cairo_set_line_width(cr,2); cairo_stroke(cr);
    }
}
static void select_point(App *app, int point)
{
    if (!app->playing || app->kind != PLAYER_HUMAN || app->animation.active || game_result(&app->game) || point < 0) return;
    if (app->selected >= 0) {
        int move = game_find_move(game_board(app->game.board),app->selected, point);
        if (move >= 0 && game_legal(&app->game, move)) {
            app->pending_details = g_strdup("Human jump.\n\n");
            start_animation(app, move); return;
        }
        if (app->selected == point) { app->selected = -1; update(app); return; }
    }
    app->selected = -1;
    MoveList legal = game_legal_moves(&app->game); const Board *board = game_board(app->game.board);
    for (unsigned i = 0; i < legal.count; i++) if (board->jumps[legal.items[i]].source == (unsigned) point) {
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
    const Board *board = game_board(app->game.board);
    for (unsigned p = 0; p < board->points; p++) if (hypot(x - board->x[p], y - board->y[p]) < 25) { select_point(app, p); return; }
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
static void board_changed(GObject *object, GParamSpec *spec, gpointer data)
{
    (void) object; (void) spec; App *app = data;
    if (!app->playing) {
        BoardKind board = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->board_type));
        game_init(&app->game,board,board == BOARD_ENGLISH && gtk_check_button_get_active(GTK_CHECK_BUTTON(app->center)) ? GOAL_CENTER : GOAL_ANY);
    }
    update(app);
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
    const char *names[] = {_("Human"), _("Random"), _("Heuristic"), _("Backtracking"), "A*", "Ollama", NULL};
    GtkStringList *model = gtk_string_list_new(names);
    gtk_drop_down_set_model(GTK_DROP_DOWN(app->controller), G_LIST_MODEL(model)); g_object_unref(model);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller), selected);
    guint board_selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->board_type));
    const char *boards[] = {_("English"),_("Triangular"),NULL};
    GtkStringList *board_model = gtk_string_list_new(boards);
    gtk_drop_down_set_model(GTK_DROP_DOWN(app->board_type),G_LIST_MODEL(board_model)); g_object_unref(board_model);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->board_type),board_selected);
    retranslate(app->window); gtk_window_set_title(GTK_WINDOW(app->window), _(GAME_NAME));
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
    gtk_box_append(GTK_BOX(sidebar), label("Board"));
    const char *boards[] = {_("English"),_("Triangular"),NULL};
    app->board_type = gtk_drop_down_new_from_strings(boards); gtk_box_append(GTK_BOX(sidebar),app->board_type);
    app->center = check("Require center finish"); gtk_box_append(GTK_BOX(sidebar),app->center);
    gtk_box_append(GTK_BOX(sidebar), label("Controller"));
    const char *controllers[] = {_("Human"), _("Random"), _("Heuristic"), _("Backtracking"), "A*", "Ollama", NULL};
    app->controller = gtk_drop_down_new_from_strings(controllers); gtk_box_append(GTK_BOX(sidebar), app->controller);
    app->time_label = label("Planning seconds (0: unlimited)"); gtk_box_append(GTK_BOX(sidebar),app->time_label);
    app->time_limit = gtk_spin_button_new_with_range(0,18000,1); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->time_limit),60); gtk_box_append(GTK_BOX(sidebar),app->time_limit);
    app->memory_label = label("Solver memory limit (MiB)"); gtk_box_append(GTK_BOX(sidebar),app->memory_label);
    app->memory_limit = gtk_spin_button_new_with_range(16,4096,16); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->memory_limit),256); gtk_box_append(GTK_BOX(sidebar),app->memory_limit);
    app->model_label = label("Ollama model"); gtk_box_append(GTK_BOX(sidebar), app->model_label);
    const char *empty[] = {NULL}; app->model = gtk_drop_down_new_from_strings(empty); gtk_box_append(GTK_BOX(sidebar), app->model);
    app->refresh = button("Refresh models", G_CALLBACK(refresh_clicked), app); gtk_box_append(GTK_BOX(sidebar), app->refresh);
    app->assisted = check("Backtracking assistance"); gtk_box_append(GTK_BOX(sidebar), app->assisted);
    app->thinking = check("Extended thinking"); gtk_box_append(GTK_BOX(sidebar), app->thinking);
    gtk_box_append(GTK_BOX(sidebar), label("Animation delay (seconds)"));
    app->delay = gtk_spin_button_new_with_range(0, 3, .1); gtk_spin_button_set_digits(GTK_SPIN_BUTTON(app->delay), 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay), .5); gtk_box_append(GTK_BOX(sidebar), app->delay);
    app->begin = button("Begin", G_CALLBACK(begin_clicked), app); gtk_widget_add_css_class(app->begin, "suggested-action"); gtk_box_append(GTK_BOX(sidebar), app->begin);
    app->retry = button("Retry",G_CALLBACK(retry_clicked),app); gtk_box_append(GTK_BOX(sidebar),app->retry);
    app->pause = button("Pause", G_CALLBACK(pause_clicked), app); gtk_box_append(GTK_BOX(sidebar), app->pause);
    app->undo = button("Undo", G_CALLBACK(undo_clicked), app); gtk_box_append(GTK_BOX(sidebar), app->undo);
    app->restart = button("Restart", G_CALLBACK(restart_clicked), app); gtk_box_append(GTK_BOX(sidebar), app->restart);
    app->configure = button("New configuration", G_CALLBACK(configure_clicked), app); gtk_box_append(GTK_BOX(sidebar), app->configure);
    app->status = gtk_label_new(NULL); gtk_label_set_wrap(GTK_LABEL(app->status), TRUE); gtk_label_set_xalign(GTK_LABEL(app->status), 0);
    gtk_box_append(GTK_BOX(sidebar), app->status);
    app->error_label = gtk_label_new(NULL); gtk_label_set_wrap(GTK_LABEL(app->error_label), TRUE); gtk_widget_add_css_class(app->error_label, "error");
    gtk_box_append(GTK_BOX(sidebar), app->error_label); gtk_widget_set_visible(app->error_label, FALSE);
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10); gtk_widget_set_hexpand(right, TRUE); gtk_box_append(GTK_BOX(body), right);
    app->board = gtk_drawing_area_new(); gtk_widget_set_size_request(app->board, 520, 520); gtk_widget_set_vexpand(app->board, TRUE);
    gtk_widget_set_hexpand(app->board, TRUE); gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->board), draw, app, NULL);
    gtk_widget_set_focusable(app->board, TRUE); gtk_box_append(GTK_BOX(right), app->board);
    GtkWidget *rules = label("Jump an adjacent occupied peg into an empty hole and remove the jumped peg. English: horizontal or vertical jumps. Triangular: jumps along its three axes. Finish with one peg; the optional English goal requires the center.");
    gtk_label_set_wrap(GTK_LABEL(rules), TRUE); gtk_box_append(GTK_BOX(right), rules);
    GtkGesture *gesture = gtk_gesture_click_new(); g_signal_connect(gesture, "pressed", G_CALLBACK(click), app);
    gtk_widget_add_controller(app->board, GTK_EVENT_CONTROLLER(gesture));
    GtkEventController *keys = gtk_event_controller_key_new(); g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), app);
    gtk_widget_add_controller(app->window, keys);
    g_signal_connect(app->language, "notify::selected", G_CALLBACK(language_changed), app);
    g_signal_connect(app->controller, "notify::selected", G_CALLBACK(controller_changed), app);
    g_signal_connect(app->model, "notify::selected", G_CALLBACK(model_changed), app);
    g_signal_connect(app->board_type,"notify::selected",G_CALLBACK(board_changed),app);
    g_signal_connect(app->center,"notify::active",G_CALLBACK(board_changed),app);
    g_signal_connect(app->window, "close-request", G_CALLBACK(close_window), app);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(app->window), root);
    update(app); gtk_window_present(GTK_WINDOW(app->window));
}
int main(int argc, char **argv)
{
    App app = {.selected = -1, .pending = -1};
    app.random = g_rand_new();
    app.diagnostics = g_string_new(NULL); game_init(&app.game,BOARD_ENGLISH,GOAL_ANY);
    app.url = g_strdup(g_getenv("OLLAMA_URL"));
    app.preferences = g_build_filename(g_get_user_config_dir(), "ai-games", "peg-solitaire.ini", NULL);
    GError *error = NULL;
    if (!i18n_load(app.preferences, &error)) { g_printerr("%s\n", error->message); g_clear_error(&error); }
    app.application = GTK_APPLICATION(adw_application_new("io.github.aigames.PegSolitaire", G_APPLICATION_DEFAULT_FLAGS));
    g_signal_connect(app.application, "activate", G_CALLBACK(activate), &app);
    int status = g_application_run(G_APPLICATION(app.application), argc, argv);
    app.closing = TRUE;
    if (app.cancel) g_cancellable_cancel(app.cancel);
    while (app.task) g_main_context_iteration(NULL, TRUE);
    for (unsigned i = 0; i < MAX_JUMPS; i++) g_free(app.details[i]);
    g_string_free(app.diagnostics,TRUE); g_object_unref(app.application); g_rand_free(app.random);
    g_free(app.preferences); g_free(app.url); g_free(app.selected_model); g_free(app.error); g_free(app.pending_details);
    return status;
}
