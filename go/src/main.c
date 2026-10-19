// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation.h"
#include "i18n.h"
#include "ollama.h"
#include "search.h"
#include <adwaita.h>
#include <math.h>
#include <string.h>
typedef struct App App;
typedef struct {
  Game game;
  PlayerKind kind;
  SearchOptions options;
  gboolean list, thinking;
  unsigned generation;
  guint32 seed;
  gsize history_length;
  char *url, *model;
  char **models;
  int move;
  SearchResult result;
  OllamaResult ollama;
  GError *error;
  GMutex mutex;
  unsigned completed;
  guint64 nodes;
} Job;
struct App {
  unsigned refs, generation, progress_ticks;
  gsize pending_length;
  GtkWidget *window, *board, *status, *error, *moves, *controllers[2],
      *models[2], *thinking[2], *iterations[2], *model_settings[2],
      *mcts_settings[2];
  GtkWidget *seconds, *memory, *planning, *komi, *action_limit, *delay, *url,
      *server_settings, *language, *begin, *pass, *pause, *undo, *restart,
      *configure, *retry, *refresh;
  Game game;
  State pending_state;
  PlayerKind kinds[2];
  SearchResult last_search;
  Animation animation;
  int pending, cursor;
  gboolean ready, playing, paused, failed, incomplete, recorded, closing;
  GString *history, *sequence;
  GArray *history_lengths;
  GRand *random;
  GTask *task;
  GCancellable *cancel;
  guint tick;
  char *message, *preferences;
  guint32 seed;
};
static void update(App *a);
static void schedule(App *a);
static App *app_ref(App *a) {
  a->refs++;
  return a;
}
static void app_unref(gpointer data) {
  App *a = data;
  if (--a->refs)
    return;
  game_clear(&a->game);
  g_string_free(a->history, TRUE);
  g_string_free(a->sequence, TRUE);
  g_array_unref(a->history_lengths);
  g_rand_free(a->random);
  g_free(a->message);
  g_free(a->preferences);
  g_free(a);
}
static void set_message(App *a, const char *s) {
  g_free(a->message);
  a->message = g_strdup(s);
}
static SearchOptions read_options(App *a, unsigned color) {
  return (SearchOptions){
      gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->iterations[color])),
      gtk_spin_button_get_value(GTK_SPIN_BUTTON(a->seconds)),
      (gsize)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->memory)) *
          1024 * 1024,
      0};
}
static void cancel_work(App *a) {
  a->generation++;
  if (a->cancel)
    g_cancellable_cancel(a->cancel);
  g_clear_object(&a->cancel);
  g_clear_object(&a->task);
  a->animation.active = FALSE;
  a->ready = FALSE;
  a->pending = -1;
}
static void reset(App *a) {
  cancel_work(a);
  game_clear(&a->game);
  double komi =
      round(gtk_spin_button_get_value(GTK_SPIN_BUTTON(a->komi)) * 2) / 2;
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->komi), komi);
  game_init(&a->game, komi);
  a->paused = a->failed = a->incomplete = a->recorded = FALSE;
  a->last_search = (SearchResult){0};
  g_string_truncate(a->history, 0);
  g_string_truncate(a->sequence, 0);
  g_array_set_size(a->history_lengths, 0);
  set_message(a, NULL);
  a->seed = g_random_int();
  g_rand_set_seed(a->random, a->seed);
}
static void record(App *a) {
  if (a->recorded || (!game_finished(&a->game) && !a->incomplete))
    return;
  Score score;
  game_score(&a->game, &score);
  int winner = game_winner(&a->game);
  g_autofree char *map = game_map(&a->game);
  g_autofree char *report = g_strdup_printf(
      "## Go 9×9 — %s\n\nRules: Chinese-style area, no suicide, positional "
      "superko; captures played out. Komi: %.1f. Seed: %u. Black: %s; White: "
      "%s. Black area: %.1f; White area including komi: %.1f; neutral: %u; "
      "margin Black−White: %.1f. Captures: %u−%u (excluded from score). "
      "Actions: %u.\n\nSequence: %s\n\n%s\n```\n%s```\n\n",
      winner < 0        ? "incomplete"
      : winner == BLACK ? "Black wins"
      : winner == WHITE ? "White wins"
                        : "draw",
      a->game.komi, a->seed, player_name(a->kinds[0]), player_name(a->kinds[1]),
      score.area[0], score.area[1], score.neutral, score.margin,
      a->game.state.captures[0], a->game.state.captures[1],
      a->game.history->len - 1, a->sequence->str, a->history->str, map);
  GError *error = NULL;
  if (game_append_report(GAME_HISTORY_FILE, report, &error))
    a->recorded = TRUE;
  else {
    set_message(a, error->message);
    g_clear_error(&error);
  }
}
static void job_free(gpointer data) {
  Job *j = data;
  game_clear(&j->game);
  g_free(j->url);
  g_free(j->model);
  g_strfreev(j->models);
  g_clear_error(&j->error);
  g_mutex_clear(&j->mutex);
  g_free(j);
}
typedef struct {
  Job *job;
  GCancellable *cancel;
} Work;
static gboolean cancelled(gpointer data) {
  return g_cancellable_is_cancelled(((Work *)data)->cancel);
}
static void progress(unsigned done, guint64 nodes, gpointer data) {
  Job *j = ((Work *)data)->job;
  g_mutex_lock(&j->mutex);
  j->completed = done;
  j->nodes = nodes;
  g_mutex_unlock(&j->mutex);
}
static void worker(GTask *task, gpointer source, gpointer data,
                   GCancellable *cancel) {
  (void)source;
  Job *j = data;
  GRand *r = g_rand_new_with_seed(j->seed);
  if (j->list)
    j->models =
        ollama_list_models(j->url, OLLAMA_LIST_TIMEOUT_MS, cancel, &j->error);
  else if (j->kind == PLAYER_RANDOM) {
    MoveList legal = game_legal(&j->game);
    j->move = game_random(&legal, r);
  } else if (j->kind == PLAYER_HEURISTIC) {
    int values[GO_CHOICES];
    j->move = game_heuristic(&j->game, r, values);
  } else if (j->kind == PLAYER_MCTS) {
    Work w = {j, cancel};
    SearchControl c = {cancelled, progress, &w};
    if (mcts_evaluate(&j->game, &j->options, &c, r, &j->result))
      j->move = search_choose(&j->result, r);
    else
      g_set_error_literal(&j->error, G_IO_ERROR,
                          j->result.status == SEARCH_CANCELLED
                              ? G_IO_ERROR_CANCELLED
                              : G_IO_ERROR_FAILED,
                          search_status_name(j->result.status));
  } else {
    OllamaOptions options = {.thinking = j->thinking};
    if (ollama_choose_with_options(&j->game, j->model, j->url,
                                   OLLAMA_MOVE_TIMEOUT_MS, cancel, &options,
                                   &j->ollama, &j->error))
      j->move = j->ollama.move;
  }
  g_rand_free(r);
  g_task_return_boolean(task, TRUE);
}
static void done(GObject *source, GAsyncResult *result, gpointer data) {
  (void)source;
  App *a = data;
  GTask *task = G_TASK(result);
  Job *j = g_task_get_task_data(task);
  GError *error = NULL;
  g_task_propagate_boolean(task, &error);
  if (a->closing || j->generation != a->generation) {
    g_clear_error(&error);
    app_unref(a);
    return;
  }
  g_clear_object(&a->task);
  g_clear_object(&a->cancel);
  if (j->list) {
    if (j->models) {
      for (unsigned c = 0; c < 2; c++) {
        GtkStringList *names =
            gtk_string_list_new((const char *const *)j->models);
        gtk_drop_down_set_model(GTK_DROP_DOWN(a->models[c]),
                                G_LIST_MODEL(names));
        g_object_unref(names);
      }
      set_message(a, j->models[0] ? NULL : _("No Ollama models found."));
    } else
      set_message(a, j->error ? j->error->message
                     : error  ? error->message
                              : _("Model discovery failed."));
  } else if (error || j->error) {
    a->failed = TRUE;
    set_message(a, j->error ? j->error->message : error->message);
  } else if (j->move < 0 || game_try(&a->game, j->move, NULL) != MOVE_OK) {
    a->failed = TRUE;
    set_message(a, _("The prepared move is no longer legal."));
  } else {
    a->pending_length = j->history_length;
    a->pending = j->move;
    a->ready = TRUE;
    a->last_search = j->result;
    g_string_append_printf(
        a->history,
        "Decision: %s; seed: %u; %.3f seconds / %zu bytes; requested "
        "simulations: %u; completed: %u; rollouts cut off: %u; status: %s; "
        "nodes: %" G_GUINT64_FORMAT "; elapsed: %.3f ms; peak memory: %zu.\n\n",
        player_name(j->kind), j->seed, j->options.seconds,
        j->options.memory_bytes, j->options.iterations, j->result.completed,
        j->result.cutoffs, search_status_name(j->result.status),
        j->result.nodes, j->result.elapsed_us / 1000.0, j->result.peak_memory);
    if (j->kind == PLAYER_OLLAMA)
      ollama_record(a->history, j->model, &j->ollama);
  }
  g_clear_error(&error);
  update(a);
  schedule(a);
  app_unref(a);
}
static void launch(App *a, gboolean list) {
  if (a->task || a->closing)
    return;
  unsigned color = a->game.state.turn;
  Job *j = g_new0(Job, 1);
  g_mutex_init(&j->mutex);
  game_copy(&j->game, &a->game, 0);
  j->kind = a->kinds[color];
  j->options = read_options(a, color);
  j->history_length = a->history->len;
  j->generation = a->generation;
  j->list = list;
  j->seed = g_rand_int(a->random);
  j->move = -1;
  j->url = g_strdup(gtk_editable_get_text(GTK_EDITABLE(a->url)));
  GtkStringObject *item =
      gtk_drop_down_get_selected_item(GTK_DROP_DOWN(a->models[color]));
  j->model = g_strdup(item ? gtk_string_object_get_string(item) : "");
  j->thinking =
      gtk_check_button_get_active(GTK_CHECK_BUTTON(a->thinking[color]));
  a->cancel = g_cancellable_new();
  a->task = g_task_new(NULL, a->cancel, done, app_ref(a));
  g_task_set_task_data(a->task, j, job_free);
  g_task_run_in_thread(a->task, worker);
  update(a);
}
static void commit(App *a) {
  unsigned move = a->pending, color = a->game.state.turn;
  gsize length = a->pending_length;
  if (!game_play(&a->game, move)) {
    a->failed = TRUE;
    set_message(a, _("The prepared move is no longer legal."));
  } else {
    g_array_append_val(a->history_lengths, length);
    g_string_append_printf(a->sequence, "%s%s", a->sequence->len ? " " : "",
                           game_label(move));
    g_string_append_printf(
        a->history, "Action %u: %s %s. Captures: %u−%u.\n\n",
        a->game.history->len - 1, color == BLACK ? "Black" : "White",
        game_label(move), a->game.state.captures[0], a->game.state.captures[1]);
  }
  a->animation.active = FALSE;
  a->pending = -1;
  a->ready = FALSE;
  if (!game_finished(&a->game) &&
      a->game.history->len - 1 >= (unsigned)gtk_spin_button_get_value_as_int(
                                      GTK_SPIN_BUTTON(a->action_limit)))
    a->incomplete = TRUE;
  record(a);
  update(a);
}
static gboolean start_move(App *a, unsigned move) {
  if (!a->playing || a->paused || a->animation.active || a->incomplete ||
      a->failed)
    return FALSE;
  MoveError error = game_try(&a->game, move, &a->pending_state);
  if (error != MOVE_OK) {
    set_message(a, _(move_error_name(error)));
    update(a);
    return FALSE;
  }
  set_message(a, NULL);
  if (!a->ready)
    a->pending_length = a->history->len;
  a->pending = move;
  a->ready = FALSE;
  animation_start(&a->animation, g_get_monotonic_time(),
                  gtk_spin_button_get_value(GTK_SPIN_BUTTON(a->delay)));
  if (!a->animation.duration)
    commit(a);
  update(a);
  return TRUE;
}
static void schedule(App *a) {
  if (!a->playing || a->closing || a->paused || a->failed || a->incomplete ||
      a->animation.active || game_finished(&a->game) ||
      a->kinds[a->game.state.turn] == PLAYER_HUMAN)
    return;
  if (a->ready)
    start_move(a, a->pending);
  else if (!a->task)
    launch(a, FALSE);
}
static gboolean tick(gpointer data) {
  App *a = data;
  if (a->animation.active && !a->paused &&
      animation_frame(&a->animation, g_get_monotonic_time()).finished)
    commit(a);
  schedule(a);
  if (a->task && ++a->progress_ticks % 6 == 0)
    update(a);
  if (a->animation.active)
    gtk_widget_queue_draw(a->board);
  return G_SOURCE_CONTINUE;
}
static void begin_clicked(GtkButton *button, gpointer data) {
  (void)button;
  App *a = data;
  reset(a);
  for (unsigned c = 0; c < 2; c++) {
    a->kinds[c] = gtk_drop_down_get_selected(GTK_DROP_DOWN(a->controllers[c]));
    if (a->kinds[c] == PLAYER_OLLAMA &&
        !gtk_drop_down_get_selected_item(GTK_DROP_DOWN(a->models[c]))) {
      a->playing = FALSE;
      set_message(a, _("Select an Ollama model first."));
      update(a);
      return;
    }
  }
  a->playing = TRUE;
  update(a);
  gtk_widget_grab_focus(a->board);
  schedule(a);
}
static void pause_clicked(GtkButton *button, gpointer data) {
  (void)button;
  App *a = data;
  a->paused = !a->paused;
  if (a->paused)
    animation_pause(&a->animation, g_get_monotonic_time());
  else
    animation_resume(&a->animation, g_get_monotonic_time());
  update(a);
  schedule(a);
}
static void pass_clicked(GtkButton *button, gpointer data) {
  (void)button;
  App *a = data;
  if (a->kinds[a->game.state.turn] == PLAYER_HUMAN)
    start_move(a, GO_PASS);
}
static void undo_clicked(GtkButton *button, gpointer data) {
  (void)button;
  App *a = data;
  if (game_finished(&a->game) || a->incomplete || a->animation.active ||
      (a->kinds[0] != PLAYER_HUMAN && a->kinds[1] != PLAYER_HUMAN) ||
      a->game.history->len <= 1)
    return;
  cancel_work(a);
  game_undo(&a->game);
  gsize length =
      g_array_index(a->history_lengths, gsize, a->history_lengths->len - 1);
  g_array_set_size(a->history_lengths, a->history_lengths->len - 1);
  g_string_truncate(a->history, length);
  g_string_truncate(a->sequence, 0);
  for (unsigned i = 1; i < a->game.history->len; i++)
    g_string_append_printf(
        a->sequence, "%s%s", i > 1 ? " " : "",
        game_label(g_array_index(a->game.history, State, i).last));
  a->paused = TRUE;
  a->failed = FALSE;
  a->last_search = (SearchResult){0};
  set_message(a, NULL);
  update(a);
}
static void restart_clicked(GtkButton *button, gpointer data) {
  (void)button;
  App *a = data;
  reset(a);
  a->playing = TRUE;
  update(a);
  schedule(a);
}
static void configure_clicked(GtkButton *button, gpointer data) {
  (void)button;
  App *a = data;
  cancel_work(a);
  a->playing = FALSE;
  a->paused = a->failed = FALSE;
  set_message(a, NULL);
  update(a);
}
static void retry_clicked(GtkButton *button, gpointer data) {
  (void)button;
  App *a = data;
  if (game_finished(&a->game) || a->incomplete) {
    record(a);
    update(a);
    return;
  }
  cancel_work(a);
  a->failed = FALSE;
  a->paused = FALSE;
  set_message(a, NULL);
  update(a);
  schedule(a);
}
static void refresh_clicked(GtkButton *button, gpointer data) {
  (void)button;
  launch(data, TRUE);
}
static void retranslate(GtkWidget *widget) {
  const char *s = g_object_get_data(G_OBJECT(widget), "translation");
  if (s) {
    if (GTK_IS_LABEL(widget))
      gtk_label_set_text(GTK_LABEL(widget), _(s));
    else if (GTK_IS_CHECK_BUTTON(widget))
      gtk_check_button_set_label(GTK_CHECK_BUTTON(widget), _(s));
    else if (GTK_IS_BUTTON(widget))
      gtk_button_set_label(GTK_BUTTON(widget), _(s));
  }
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
       child = gtk_widget_get_next_sibling(child))
    retranslate(child);
}
static void update(App *a) {
  if (!a->status || !a->game.history)
    return;
  retranslate(a->window);
  gboolean any_mcts = FALSE, any_ollama = FALSE;
  for (unsigned c = 0; c < 2; c++) {
    PlayerKind kind =
        a->playing
            ? a->kinds[c]
            : gtk_drop_down_get_selected(GTK_DROP_DOWN(a->controllers[c]));
    any_mcts |= kind == PLAYER_MCTS;
    any_ollama |= kind == PLAYER_OLLAMA;
    gtk_widget_set_visible(a->mcts_settings[c], kind == PLAYER_MCTS);
    gtk_widget_set_visible(a->model_settings[c], kind == PLAYER_OLLAMA);
    gtk_widget_set_sensitive(a->controllers[c], !a->playing);
    gtk_widget_set_sensitive(a->iterations[c], !a->playing || a->failed);
    gtk_widget_set_sensitive(a->models[c], !a->playing);
    gtk_widget_set_sensitive(a->thinking[c], !a->playing);
    GtkStringObject *first = g_list_model_get_item(
        gtk_drop_down_get_model(GTK_DROP_DOWN(a->controllers[c])), 0);
    if (g_strcmp0(gtk_string_object_get_string(first), _("Human"))) {
      unsigned selected =
          gtk_drop_down_get_selected(GTK_DROP_DOWN(a->controllers[c]));
      GtkStringList *names = gtk_string_list_new(NULL);
      for (PlayerKind p = PLAYER_HUMAN; p < PLAYER_COUNT; p++)
        gtk_string_list_append(names, _(player_name(p)));
      gtk_drop_down_set_model(GTK_DROP_DOWN(a->controllers[c]),
                              G_LIST_MODEL(names));
      gtk_drop_down_set_selected(GTK_DROP_DOWN(a->controllers[c]), selected);
      g_object_unref(names);
    }
    g_object_unref(first);
  }
  gtk_widget_set_visible(a->planning, any_mcts);
  gtk_widget_set_visible(a->server_settings, any_ollama);
  gtk_widget_set_sensitive(a->komi, !a->playing);
  gtk_widget_set_sensitive(a->action_limit, !a->playing);
  gtk_widget_set_sensitive(a->seconds, !a->playing || a->failed);
  gtk_widget_set_sensitive(a->memory, !a->playing || a->failed);
  gtk_widget_set_sensitive(a->url, !a->playing);
  gtk_widget_set_sensitive(a->refresh, !a->playing && !a->task);
  gtk_widget_set_visible(a->begin, !a->playing);
  gtk_widget_set_visible(a->restart, a->playing);
  gtk_widget_set_visible(a->configure, a->playing || a->task != NULL);
  gtk_widget_set_visible(a->pass, a->playing);
  gtk_widget_set_sensitive(
      a->pass, a->playing && !a->paused && !a->failed && !a->incomplete &&
                   !game_finished(&a->game) && !a->animation.active &&
                   a->kinds[a->game.state.turn] == PLAYER_HUMAN);
  gtk_widget_set_visible(a->pause, a->playing);
  gtk_button_set_label(GTK_BUTTON(a->pause), _(a->paused ? "Resume" : "Pause"));
  gtk_widget_set_sensitive(a->pause, !game_finished(&a->game) &&
                                         !a->incomplete && !a->failed);
  gtk_widget_set_visible(a->undo, a->playing && (a->kinds[0] == PLAYER_HUMAN ||
                                                 a->kinds[1] == PLAYER_HUMAN));
  gtk_widget_set_sensitive(a->undo, a->game.history->len > 1 &&
                                        !game_finished(&a->game) &&
                                        !a->incomplete && !a->animation.active);
  gtk_widget_set_visible(
      a->retry, a->failed || ((game_finished(&a->game) || a->incomplete) &&
                              !a->recorded));
  Score s;
  game_score(&a->game, &s);
  GString *status = g_string_new(NULL);
  if (game_finished(&a->game)) {
    int winner = game_winner(&a->game);
    g_string_append_printf(status,
                           _("%s · margin %.1f\nBlack area: %.1f · White area: "
                             "%.1f + %.1f komi\nNeutral points: %u"),
                           _(winner == BLACK   ? "Black wins"
                             : winner == WHITE ? "White wins"
                                               : "Draw"),
                           fabs(s.margin), s.area[0], s.area[1] - a->game.komi,
                           a->game.komi, s.neutral);
  } else
    g_string_append(
        status, !a->playing     ? _("Configure players, then Begin.")
                : a->incomplete ? _("Action limit reached — incomplete")
                : a->failed     ? _("Stopped — adjust limits and Retry")
                : a->paused     ? _("Paused")
                : a->task       ? _("Thinking…")
                          : _(a->game.state.turn == BLACK ? "Black to move"
                                                          : "White to move"));
  g_string_append_printf(status,
                         _("\nCaptures: Black %u · White %u · Consecutive "
                           "passes: %u\nActions: %u · Komi: %.1f"),
                         a->game.state.captures[0], a->game.state.captures[1],
                         a->game.state.passes, a->game.history->len - 1,
                         a->game.komi);
  if (a->task) {
    Job *j = g_task_get_task_data(a->task);
    g_mutex_lock(&j->mutex);
    unsigned completed = j->completed;
    guint64 nodes = j->nodes;
    g_mutex_unlock(&j->mutex);
    if (!j->list && j->kind == PLAYER_MCTS)
      g_string_append_printf(status,
                             _("\nPlanning: %u simulations · %llu actions"),
                             completed, (unsigned long long)nodes);
  }
  if (a->last_search.completed)
    g_string_append_printf(status, _("\nSimulations: %u/%u · Cutoffs: %u · %s"),
                           a->last_search.completed, a->last_search.requested,
                           a->last_search.cutoffs,
                           _(search_status_name(a->last_search.status)));
  gtk_label_set_text(GTK_LABEL(a->status), status->str);
  g_string_free(status, TRUE);
  gtk_label_set_text(GTK_LABEL(a->error), a->message ? _(a->message) : "");
  gtk_widget_set_visible(a->error, a->message != NULL);
  GString *moves = g_string_new(NULL);
  for (unsigned i = 1; i < a->game.history->len; i++)
    g_string_append_printf(
        moves, "%u. %s %s\n", i, _((i % 2) ? "Black" : "White"),
        game_label(g_array_index(a->game.history, State, i).last));
  gtk_label_set_text(GTK_LABEL(a->moves), moves->str);
  g_string_free(moves, TRUE);
  gtk_widget_queue_draw(a->board);
}
static void changed(GObject *object, GParamSpec *spec, gpointer data) {
  (void)object;
  (void)spec;
  update(data);
}
static void language_changed(GObject *object, GParamSpec *spec, gpointer data) {
  (void)object;
  (void)spec;
  App *a = data;
  i18n_set_language(gtk_drop_down_get_selected(GTK_DROP_DOWN(a->language)));
  GError *error = NULL;
  if (!i18n_save(a->preferences, &error)) {
    set_message(a, error->message);
    g_clear_error(&error);
  }
  update(a);
}
static double geometry(int width, int height, double *ox, double *oy) {
  double step = MIN((width - 70.0) / 8, (height - 70.0) / 8);
  *ox = (width - 8 * step) / 2;
  *oy = (height - 8 * step) / 2;
  return step;
}
static void stone(cairo_t *cr, double x, double y, double r, unsigned color,
                  double opacity) {
  cairo_set_source_rgba(cr, color == BLACK ? .08 : .96,
                        color == BLACK ? .09 : .96, color == BLACK ? .10 : .93,
                        opacity);
  cairo_arc(cr, x, y, r, 0, 2 * G_PI);
  cairo_fill_preserve(cr);
  cairo_set_line_width(cr, 1);
  cairo_set_source_rgba(cr, .18, .17, .15, opacity);
  cairo_stroke(cr);
}
static void draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                 gpointer data) {
  (void)area;
  App *a = data;
  double ox, oy, step = geometry(width, height, &ox, &oy);
  cairo_set_source_rgb(cr, .77, .58, .32);
  cairo_paint(cr);
  cairo_set_source_rgb(cr, .20, .15, .09);
  cairo_set_line_width(cr, 1.2);
  for (unsigned i = 0; i < 9; i++) {
    cairo_move_to(cr, ox + i * step, oy);
    cairo_line_to(cr, ox + i * step, oy + 8 * step);
    cairo_move_to(cr, ox, oy + i * step);
    cairo_line_to(cr, ox + 8 * step, oy + i * step);
  }
  cairo_stroke(cr);
  cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cr, 13);
  for (unsigned i = 0; i < 9; i++) {
    char c[2] = {"ABCDEFGHJ"[i], 0}, r[3];
    g_snprintf(r, sizeof r, "%u", 9 - i);
    cairo_move_to(cr, ox + i * step - 4, oy + 8 * step + 24);
    cairo_show_text(cr, c);
    cairo_move_to(cr, ox - 24, oy + i * step + 4);
    cairo_show_text(cr, r);
  }
  const unsigned stars[] = {20, 24, 40, 56, 60};
  for (unsigned i = 0; i < G_N_ELEMENTS(stars); i++) {
    cairo_arc(cr, ox + (stars[i] % 9) * step, oy + (8 - stars[i] / 9) * step, 3,
              0, 2 * G_PI);
    cairo_fill(cr);
  }
  Score score;
  game_score(&a->game, &score);
  if (game_finished(&a->game))
    for (unsigned p = 0; p < GO_POINTS; p++)
      if (game_at(&a->game.state.board, p) < 0 && score.owner[p] >= 0) {
        cairo_set_source_rgba(cr, score.owner[p] == BLACK ? .08 : 1,
                              score.owner[p] == BLACK ? .08 : 1,
                              score.owner[p] == BLACK ? .08 : 1, .65);
        cairo_rectangle(cr, ox + (p % 9) * step - 4,
                        oy + (8 - p / 9) * step - 4, 8, 8);
        cairo_fill(cr);
      }
  AnimationFrame f = animation_frame(&a->animation, g_get_monotonic_time());
  for (unsigned p = 0; p < GO_POINTS; p++) {
    int c = game_at(&a->game.state.board, p);
    gboolean captured =
        a->animation.active && (a->pending_state.removed[p / 64] &
                                (G_GUINT64_CONSTANT(1) << (p % 64)));
    if (c >= 0)
      stone(cr, ox + (p % 9) * step, oy + (8 - p / 9) * step, step * .43, c,
            captured ? 1 - f.position : 1);
  }
  if (a->animation.active && a->pending < GO_POINTS)
    stone(cr, ox + (a->pending % 9) * step, oy + (8 - a->pending / 9) * step,
          step * .43, a->game.state.turn, f.opacity);
  int last = a->game.state.last;
  if (last >= 0 && last < GO_POINTS) {
    cairo_set_source_rgb(cr, .85, .27, .12);
    cairo_arc(cr, ox + (last % 9) * step, oy + (8 - last / 9) * step,
              step * .12, 0, 2 * G_PI);
    cairo_fill(cr);
  }
  for (unsigned p = 0; p < GO_POINTS; p++)
    if (a->game.state.removed[p / 64] & (G_GUINT64_CONSTANT(1) << (p % 64))) {
      cairo_set_source_rgb(cr, .82, .20, .10);
      double x = ox + (p % 9) * step, y = oy + (8 - p / 9) * step;
      cairo_rectangle(cr, x - step * .18, y - step * .18, step * .36,
                      step * .36);
      cairo_stroke(cr);
    }
  if (gtk_widget_has_focus(a->board)) {
    cairo_set_source_rgb(cr, .20, .45, .77);
    cairo_arc(cr, ox + (a->cursor % 9) * step, oy + (8 - a->cursor / 9) * step,
              step * .47, 0, 2 * G_PI);
    cairo_stroke(cr);
  }
}
static void board_clicked(GtkGestureClick *gesture, int count, double x,
                          double y, gpointer data) {
  (void)gesture;
  (void)count;
  App *a = data;
  double ox, oy,
      step = geometry(gtk_widget_get_width(a->board),
                      gtk_widget_get_height(a->board), &ox, &oy);
  int col = lround((x - ox) / step), row = 8 - lround((y - oy) / step);
  if (col < 0 || col > 8 || row < 0 || row > 8)
    return;
  a->cursor = row * 9 + col;
  gtk_widget_grab_focus(a->board);
  if (a->playing && a->kinds[a->game.state.turn] == PLAYER_HUMAN)
    start_move(a, a->cursor);
}
static gboolean key_pressed(GtkEventControllerKey *keys, guint key, guint code,
                            GdkModifierType modifiers, gpointer data) {
  (void)keys;
  (void)code;
  App *a = data;
  if (modifiers & (GDK_CONTROL_MASK | GDK_ALT_MASK))
    return FALSE;
  switch (key) {
  case GDK_KEY_Left:
    if (a->cursor % 9)
      a->cursor--;
    break;
  case GDK_KEY_Right:
    if (a->cursor % 9 < 8)
      a->cursor++;
    break;
  case GDK_KEY_Up:
    if (a->cursor < 72)
      a->cursor += 9;
    break;
  case GDK_KEY_Down:
    if (a->cursor >= 9)
      a->cursor -= 9;
    break;
  case GDK_KEY_Return:
  case GDK_KEY_space:
    if (a->playing && a->kinds[a->game.state.turn] == PLAYER_HUMAN)
      start_move(a, a->cursor);
    break;
  default:
    return FALSE;
  }
  gtk_widget_queue_draw(a->board);
  return TRUE;
}
static void label(GtkWidget *box, const char *s) {
  GtkWidget *w = gtk_label_new(_(s));
  gtk_label_set_xalign(GTK_LABEL(w), 0);
  gtk_box_append(GTK_BOX(box), w);
  g_object_set_data_full(G_OBJECT(w), "translation", g_strdup(s), g_free);
}
static GtkWidget *button(GtkWidget *box, const char *s, GCallback callback,
                         App *a) {
  GtkWidget *w = gtk_button_new_with_label(_(s));
  gtk_box_append(GTK_BOX(box), w);
  g_object_set_data_full(G_OBJECT(w), "translation", g_strdup(s), g_free);
  g_signal_connect(w, "clicked", callback, a);
  return w;
}
static GtkWidget *spin(GtkWidget *box, const char *s, double min, double max,
                       double increment, double value) {
  label(box, s);
  GtkWidget *w = gtk_spin_button_new_with_range(min, max, increment);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), value);
  gtk_box_append(GTK_BOX(box), w);
  return w;
}
static gboolean close_requested(GtkWindow *window, gpointer data) {
  (void)window;
  App *a = data;
  a->closing = TRUE;
  cancel_work(a);
  if (a->tick) {
    g_source_remove(a->tick);
    a->tick = 0;
  }
  return FALSE;
}
static App *create_app(GtkApplication *application) {
  App *a = g_new0(App, 1);
  a->refs = 1;
  a->pending = -1;
  a->cursor = 40;
  a->history = g_string_new(NULL);
  a->sequence = g_string_new(NULL);
  a->history_lengths = g_array_new(FALSE, FALSE, sizeof(gsize));
  a->random = g_rand_new();
  a->preferences =
      g_build_filename(g_get_user_config_dir(), "ai-games", "go.ini", NULL);
  i18n_load(a->preferences, NULL);
  game_init(&a->game, 7.5);
  a->window = adw_application_window_new(application);
  gtk_window_set_title(GTK_WINDOW(a->window), GAME_NAME);
  gtk_window_set_default_size(GTK_WINDOW(a->window), 1120, 850);
  g_object_set_data_full(G_OBJECT(a->window), "app", a, app_unref);
  g_signal_connect(a->window, "close-request", G_CALLBACK(close_requested), a);
  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(a->window), root);
  gtk_box_append(GTK_BOX(root), adw_header_bar_new());
  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_vexpand(content, TRUE);
  gtk_box_append(GTK_BOX(root), content);
  GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_hexpand(left, TRUE);
  gtk_box_append(GTK_BOX(content), left);
  a->board = gtk_drawing_area_new();
  gtk_widget_set_hexpand(a->board, TRUE);
  gtk_widget_set_vexpand(a->board, TRUE);
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(a->board), 600);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(a->board), 500);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(a->board), draw, a, NULL);
  gtk_widget_set_focusable(a->board, TRUE);
  gtk_box_append(GTK_BOX(left), a->board);
  a->status = gtk_label_new(NULL);
  gtk_label_set_wrap(GTK_LABEL(a->status), TRUE);
  gtk_label_set_selectable(GTK_LABEL(a->status), TRUE);
  gtk_box_append(GTK_BOX(left), a->status);
  a->error = gtk_label_new(NULL);
  gtk_label_set_wrap(GTK_LABEL(a->error), TRUE);
  gtk_widget_add_css_class(a->error, "error");
  gtk_box_append(GTK_BOX(left), a->error);
  GtkWidget *scroll = gtk_scrolled_window_new();
  gtk_widget_set_size_request(scroll, 300, -1);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_box_append(GTK_BOX(content), scroll);
  GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
  gtk_widget_set_margin_end(side, 12);
  gtk_widget_set_margin_bottom(side, 12);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), side);
  const char *controllers[] = {"Human", "Random", "Heuristic",
                               "MCTS",  "Ollama", NULL};
  for (unsigned c = 0; c < 2; c++) {
    label(side, c == BLACK ? "Black player" : "White player");
    a->controllers[c] = gtk_drop_down_new_from_strings(controllers);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(a->controllers[c]),
                               c == BLACK ? PLAYER_HUMAN : PLAYER_HEURISTIC);
    gtk_box_append(GTK_BOX(side), a->controllers[c]);
    a->mcts_settings[c] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    gtk_box_append(GTK_BOX(side), a->mcts_settings[c]);
    a->iterations[c] =
        spin(a->mcts_settings[c], "MCTS simulations", 1, 1000000, 100, 1000);
    a->model_settings[c] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
    gtk_box_append(GTK_BOX(side), a->model_settings[c]);
    a->models[c] = gtk_drop_down_new(NULL, NULL);
    gtk_box_append(GTK_BOX(a->model_settings[c]), a->models[c]);
    a->thinking[c] = gtk_check_button_new_with_label(_("Enable thinking"));
    g_object_set_data(G_OBJECT(a->thinking[c]), "translation",
                      "Enable thinking");
    gtk_box_append(GTK_BOX(a->model_settings[c]), a->thinking[c]);
  }
  a->komi = spin(side, "White komi", 0, 20, .5, 7.5);
  a->action_limit = spin(side, "Whole-game action limit", 1, 100000, 100, 1000);
  a->planning = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
  gtk_box_append(GTK_BOX(side), a->planning);
  a->seconds =
      spin(a->planning, "Planning seconds (0: unlimited)", 0, 18000, 1, 5);
  a->memory = spin(a->planning, "Solver memory limit (MiB)", 1, 4096, 16, 64);
  a->delay = spin(side, "Animation delay (seconds)", 0, 3, .1, .3);
  a->server_settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
  gtk_box_append(GTK_BOX(side), a->server_settings);
  label(a->server_settings, "Ollama address");
  a->url = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(a->url), OLLAMA_DEFAULT_URL);
  gtk_box_append(GTK_BOX(a->server_settings), a->url);
  a->refresh = button(a->server_settings, "Refresh models",
                      G_CALLBACK(refresh_clicked), a);
  a->begin = button(side, "Begin", G_CALLBACK(begin_clicked), a);
  a->pass = button(side, "Pass", G_CALLBACK(pass_clicked), a);
  a->pause = button(side, "Pause", G_CALLBACK(pause_clicked), a);
  a->undo = button(side, "Undo", G_CALLBACK(undo_clicked), a);
  a->restart = button(side, "Restart", G_CALLBACK(restart_clicked), a);
  a->configure = button(side, "Configure", G_CALLBACK(configure_clicked), a);
  a->retry = button(side, "Retry", G_CALLBACK(retry_clicked), a);
  label(side, "Language");
  const char *languages[] = {"System",  "English",  "Português",
                             "Español", "Italiano", NULL};
  a->language = gtk_drop_down_new_from_strings(languages);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(a->language), i18n_selection());
  gtk_box_append(GTK_BOX(side), a->language);
  g_signal_connect(a->language, "notify::selected",
                   G_CALLBACK(language_changed), a);
  label(side, "Move history");
  GtkWidget *moves_scroll = gtk_scrolled_window_new();
  gtk_widget_set_size_request(moves_scroll, -1, 140);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(moves_scroll),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  a->moves = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(a->moves), 0);
  gtk_label_set_selectable(GTK_LABEL(a->moves), TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(moves_scroll), a->moves);
  gtk_box_append(GTK_BOX(side), moves_scroll);
  for (unsigned c = 0; c < 2; c++)
    g_signal_connect(a->controllers[c], "notify::selected", G_CALLBACK(changed),
                     a);
  GtkGesture *click = gtk_gesture_click_new();
  g_signal_connect(click, "pressed", G_CALLBACK(board_clicked), a);
  gtk_widget_add_controller(a->board, GTK_EVENT_CONTROLLER(click));
  GtkEventController *keys = gtk_event_controller_key_new();
  g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), a);
  gtk_widget_add_controller(a->board, keys);
  update(a);
  a->tick = g_timeout_add(16, tick, a);
  return a;
}
#ifndef GO_TEST
static void activate(GtkApplication *application, gpointer data) {
  (void)data;
  App *a = create_app(application);
  gtk_window_present(GTK_WINDOW(a->window));
}
int main(int argc, char **argv) {
  AdwApplication *app =
      adw_application_new("io.github.aigames.Go", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  int result = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return result;
}
#endif
