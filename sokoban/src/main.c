// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation.h"
#include "game.h"
#include "i18n.h"
#include "ollama.h"
#include "search.h"
#include <adwaita.h>
#include "../../tools/ui-panels.h"
#include "../../tools/ollama-view.h"
#include <math.h>
#include <string.h>
typedef struct App App;
typedef struct {
  Level level;
  Game game;
  PlayerKind kind;
  SearchLimits limits;
  gboolean thinking, list, pruning;
  unsigned generation;
  guint32 seed;
  char *url, *model;
  SearchResult result;
  OllamaResult model_result;
  int move;
  char **models;
  GError *error;
  GMutex mutex;
  guint64 nodes;
  gint64 elapsed;
  gsize memory;
} Job;
struct App {
  unsigned refs, generation;
  gulong level_handler;
  guint32 seed;
  GtkWidget *search_settings, *push_settings, *ollama_settings;
  GtkWidget *window, *board, *status, *error, *level, *controller, *language,
      *delay, *seconds, *memory, *push_limit, *pruning, *url, *model, *thinking;
  GtkWidget *begin, *pause, *undo_button, *restart, *configure, *retry, *import,
      *refresh;
  GPtrArray *pack;
  Game game, pending;
  Analysis analysis;
  GArray *undo;
  GString *sequence, *history;
  Animation animation;
  Direction direction;
  gboolean pushing, playing, paused, failed, terminal, recorded, closing;
  GByteArray *walk;
  unsigned walk_index, plan_index;
  SearchResult plan;
  GTask *task;
  GCancellable *cancel;
  PlayerKind kind;
  GRand *random;
  guint tick;
  char *message, *settings;
  guint64 nodes;
  gint64 elapsed;
  gsize peak;
};
static void update(App *app);
static void schedule(App *app);
static void stop_work(App *app);
static App *app_ref(App *app) {
  app->refs++;
  return app;
}
static void app_unref(gpointer data) {
  App *app = data;
  if (--app->refs)
    return;
  g_clear_pointer(&app->pack, g_ptr_array_unref);
  g_clear_pointer(&app->undo, g_array_unref);
  g_clear_pointer(&app->walk, g_byte_array_unref);
  search_result_clear(&app->plan);
  g_string_free(app->sequence, TRUE);
  g_string_free(app->history, TRUE);
  g_rand_free(app->random);
  g_free(app->message);
  g_free(app->settings);
  g_free(app);
}
static SearchLimits limits(App *a) {
  return (SearchLimits){
      gtk_spin_button_get_value(GTK_SPIN_BUTTON(a->seconds)),
      (gsize)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(a->memory)) *
          1024 * 1024,
      0, gtk_check_button_get_active(GTK_CHECK_BUTTON(a->pruning))};
}
static void set_message(App *a, const char *s) {
  g_free(a->message);
  a->message = g_strdup(s);
}
static void record(App *a, const char *outcome) {
  if (a->recorded)
    return;
  g_autofree char *map = game_map(&a->game, "\n");
  g_autofree char *report =
      g_strdup_printf("## Sokoban: %s\n\nLevel: %s; controller: %s; pruning: "
                      "%s; pushes: %u; walking: %u; nodes: %" G_GUINT64_FORMAT
                      "; elapsed: %.3f ms; peak accounted memory: %zu bytes; "
                      "optimal: %s.\n\nActions: `%s`\n\n%s\n```\n%s\n```\n\n",
                      outcome, a->game.level->name, player_name(a->kind),
                      limits(a).pruning ? "on" : "off", a->game.pushes,
                      a->game.walking, a->nodes, a->elapsed / 1000.0, a->peak,
                      a->plan.optimal && game_solved(&a->game) ? "yes" : "no",
                      a->sequence->str, a->history->str, map);
  GError *error = NULL;
  if (game_append_report(GAME_HISTORY_FILE, report, &error))
    a->recorded = TRUE;
  else {
    set_message(a, error->message);
    g_clear_error(&error);
  }
}
static void terminal_check(App *a) {
  a->terminal =
      game_solved(&a->game) || game_deadlocked(&a->game, &a->analysis);
  if (a->terminal)
    record(a, game_solved(&a->game) ? "solved" : "deadlocked");
}
static void stop_work(App *a) {
    ollama_view_invalidate(GTK_WINDOW(a->window));
  a->generation++;
  if (a->cancel)
    g_cancellable_cancel(a->cancel);
  g_clear_object(&a->cancel);
  g_clear_object(&a->task);
  a->animation.active = FALSE;
  g_clear_pointer(&a->walk, g_byte_array_unref);
  a->walk_index = 0;
  search_result_clear(&a->plan);
  a->plan = (SearchResult){0};
  a->plan_index = 0;
}
static void reset_game(App *a) {
    ollama_view_clear(GTK_WINDOW(a->window));
  stop_work(a);
  unsigned index = gtk_drop_down_get_selected(GTK_DROP_DOWN(a->level));
  if (index >= a->pack->len)
    index = 0;
  game_init(&a->game, g_ptr_array_index(a->pack, index));
  game_analyze(a->game.level, &a->analysis);
  g_array_set_size(a->undo, 0);
  g_string_truncate(a->sequence, 0);
  g_string_truncate(a->history, 0);
  a->paused = a->failed = a->terminal = a->recorded = FALSE;
  a->nodes = 0;
  a->elapsed = 0;
  a->peak = 0;
  set_message(a, NULL);
}
static void job_free(gpointer data) {
  Job *j = data;
  search_result_clear(&j->result);
  g_strfreev(j->models);
  g_clear_error(&j->error);
  g_free(j->url);
  g_free(j->model);
  g_mutex_clear(&j->mutex);
  g_free(j);
}
static gboolean cancelled(gpointer data) {
  return g_cancellable_is_cancelled(data);
}
typedef struct {
  Job *job;
  GCancellable *cancel;
} Progress;
static gboolean worker_cancelled(gpointer data) {
  return cancelled(((Progress *)data)->cancel);
}
static void progress(guint64 n, gint64 t, gsize m, gpointer data) {
  Job *j = ((Progress *)data)->job;
  g_mutex_lock(&j->mutex);
  j->nodes = n;
  j->elapsed = t;
  j->memory = m;
  g_mutex_unlock(&j->mutex);
}
static void worker(GTask *task, gpointer source, gpointer data,
                   GCancellable *cancel) {
  (void)source;
  Job *j = data;
  if (j->list)
    j->models =
        ollama_list_models(j->url, OLLAMA_LIST_TIMEOUT_MS, cancel, &j->error);
  else if (j->kind == PLAYER_ASTAR || j->kind == PLAYER_GREEDY) {
    Progress p = {j, cancel};
    SearchControl c = {worker_cancelled, progress, &p};
    j->result = search_solve(&j->game, j->kind, &j->limits, &c, j->seed);
  } else if (j->kind == PLAYER_OLLAMA) {
    OllamaOptions options = {.thinking = j->thinking};
    ollama_choose_with_options(&j->game, j->model, j->url,
                               OLLAMA_MOVE_TIMEOUT_MS, cancel, &options,
                               &j->model_result, &j->error);
    j->move = j->model_result.move;
  } else {
    MoveList choices = game_pushes(&j->game);
    unsigned kept = 0;
    Analysis analysis;
    game_analyze(j->game.level, &analysis);
    for (unsigned i = 0; i < choices.count; i++) {
      Game next = j->game;
      unsigned move = choices.items[i];
      next.pos.player = game_neighbor(next.level, move / 4, (move % 4) ^ 1);
      game_step(&next, move % 4, NULL);
      if (!j->pruning || !game_deadlocked(&next, &analysis))
        choices.items[kept++] = move;
    }
    if (kept) {
      GRand *r = g_rand_new_with_seed(j->seed);
      j->move = choices.items[g_rand_int_range(r, 0, kept)];
      g_rand_free(r);
    } else
      g_set_error_literal(&j->error, G_IO_ERROR, G_IO_ERROR_FAILED,
                          _("No push survives the deadlock checks."));
  }
  g_task_return_boolean(task, TRUE);
}
static gboolean make_walk(App *a, unsigned move) {
  g_clear_pointer(&a->walk, g_byte_array_unref);
  a->walk = game_push_path(&a->game, move);
  a->walk_index = 0;
  if (a->walk)
    return TRUE;
  a->failed = TRUE;
  set_message(a, _("Invalid solver path."));
  return FALSE;
}
static void completed(GObject *source, GAsyncResult *result, gpointer data) {
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
      GtkStringList *names =
          gtk_string_list_new((const char *const *)j->models);
      gtk_drop_down_set_model(GTK_DROP_DOWN(a->model), G_LIST_MODEL(names));
      g_object_unref(names);
      set_message(a, NULL);
    } else
      set_message(a, j->error ? j->error->message
                     : error  ? error->message
                              : _("Model discovery failed."));
  } else if (error || j->error) {
    a->failed = TRUE;
    record(a, "error");
    set_message(a, j->error ? j->error->message : error->message);
  } else if (j->kind == PLAYER_ASTAR || j->kind == PLAYER_GREEDY) {
    a->nodes = j->result.nodes;
    a->elapsed = j->result.elapsed_us;
    a->peak = j->result.peak_memory;
    if (j->result.status == SEARCH_SOLVED) {
      a->plan = j->result;
      j->result.path = NULL;
      a->plan_index = 0;
    } else {
      a->failed = TRUE;
      set_message(a, _(search_status_name(j->result.status)));
      record(a, search_status_name(j->result.status));
    }
  } else {
    if (j->kind == PLAYER_OLLAMA)
      ollama_record(a->history, j->model, &j->model_result);
    if (j->move < 0)
      a->failed = TRUE;
    else
      make_walk(a, j->move);
  }
  g_clear_error(&error);
  update(a);
  schedule(a);
  app_unref(a);
}
static void launch(App *a, gboolean list) {
  Job *j = g_new0(Job, 1);
  g_mutex_init(&j->mutex);
  j->generation = a->generation;
  j->kind = a->kind;
  j->list = list;
  j->level = *a->game.level;
  j->game = a->game;
  j->game.level = &j->level;
  j->limits = limits(a);
  j->pruning = j->limits.pruning;
  j->seed = g_rand_int(a->random);
  j->move = -1;
  j->url = g_strdup(gtk_editable_get_text(GTK_EDITABLE(a->url)));
  GtkStringObject *item =
      gtk_drop_down_get_selected_item(GTK_DROP_DOWN(a->model));
  j->model = g_strdup(item ? gtk_string_object_get_string(item) : "");
  j->thinking = gtk_check_button_get_active(GTK_CHECK_BUTTON(a->thinking));
  a->cancel = g_cancellable_new();
  if(!list && j->kind==PLAYER_OLLAMA)ollama_view_bind(GTK_WINDOW(a->window),a->cancel,_("Player"),a->sequence->len+1,j->model);
  g_string_append_printf(a->history,
                         "Decision seed: %u; controller: %s; limits: %.3f s / "
                         "%zu bytes; pruning: %s.\n\n",
                         j->seed, player_name(j->kind), j->limits.seconds,
                         j->limits.memory_bytes, j->pruning ? "on" : "off");
  a->task = g_task_new(NULL, a->cancel, completed, app_ref(a));
  g_task_set_task_data(a->task, j, job_free);
  g_task_run_in_thread(a->task, worker);
  update(a);
}
static void commit(App *a) {
  g_array_append_val(a->undo, a->game);
  a->game = a->pending;
  g_string_append_c(a->sequence, (a->pushing ? "UDLR" : "udlr")[a->direction]);
  a->animation.active = FALSE;
  terminal_check(a);
  update(a);
}
static gboolean start_step(App *a, Direction direction) {
  if (a->animation.active)
    return FALSE;
  a->pending = a->game;
  if (!game_step(&a->pending, direction, &a->pushing))
    return FALSE;
  a->direction = direction;
  animation_start(&a->animation, g_get_monotonic_time(),
                  gtk_spin_button_get_value(GTK_SPIN_BUTTON(a->delay)));
  if (gtk_spin_button_get_value(GTK_SPIN_BUTTON(a->delay)) == 0)
    commit(a);
  gtk_widget_queue_draw(a->board);
  return TRUE;
}
static void schedule(App *a) {
  if (a->closing || !a->playing || a->paused || a->failed || a->terminal ||
      a->animation.active || a->task || a->kind == PLAYER_HUMAN)
    return;
  if (a->walk && a->walk_index < a->walk->len) {
    if (!start_step(a, a->walk->data[a->walk_index++])) {
      a->failed = TRUE;
      set_message(a, _("Invalid solver path."));
      update(a);
    }
    return;
  }
  g_clear_pointer(&a->walk, g_byte_array_unref);
  if (a->plan.path && a->plan_index < a->plan.path->len) {
    make_walk(a, g_array_index(a->plan.path, guint16, a->plan_index++));
    return;
  }
  if (a->plan.path) {
    a->failed = TRUE;
    set_message(a, _("Invalid solver path."));
    update(a);
    return;
  }
  if ((a->kind == PLAYER_RANDOM || a->kind == PLAYER_OLLAMA) &&
      a->game.pushes >= (unsigned)gtk_spin_button_get_value_as_int(
                            GTK_SPIN_BUTTON(a->push_limit))) {
    a->failed = TRUE;
    set_message(a, _("Push limit reached."));
    record(a, "incomplete");
    update(a);
    return;
  }
  launch(a, FALSE);
}
static gboolean tick(gpointer data) {
  App *a = data;
  if (a->animation.active && !a->paused &&
      animation_frame(&a->animation, g_get_monotonic_time()).finished)
    commit(a);
  if (a->task) {
    Job *j = g_task_get_task_data(a->task);
    g_mutex_lock(&j->mutex);
    a->nodes = j->nodes;
    a->elapsed = j->elapsed;
    a->peak = j->memory;
    g_mutex_unlock(&j->mutex);
    update(a);
  }
  schedule(a);
  if (a->animation.active)
    gtk_widget_queue_draw(a->board);
  return G_SOURCE_CONTINUE;
}
static void begin_clicked(GtkButton *button, gpointer data) {
  (void)button;
  App *a = data;
  reset_game(a);
  a->kind = gtk_drop_down_get_selected(GTK_DROP_DOWN(a->controller));
  a->playing = TRUE;
  a->seed = g_random_int();
  g_rand_set_seed(a->random, a->seed);
  if (a->kind == PLAYER_OLLAMA &&
      !gtk_drop_down_get_selected_item(GTK_DROP_DOWN(a->model))) {
    a->playing = FALSE;
    set_message(a, _("Select an Ollama model first."));
  }
  if(a->playing)ollama_view_begin(GTK_WINDOW(a->window));
  terminal_check(a);
  if (a->playing && a->kind == PLAYER_HUMAN)
    gtk_widget_grab_focus(a->board);
  update(a);
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
static void undo_clicked(GtkButton *button, gpointer data) {
  (void)button;
  App *a = data;
  if (a->kind != PLAYER_HUMAN || !a->undo->len)
    return;
  stop_work(a);
  a->game = g_array_index(a->undo, Game, a->undo->len - 1);
  g_array_set_size(a->undo, a->undo->len - 1);
  g_string_truncate(a->sequence, a->sequence->len - 1);
  a->terminal = a->recorded = a->failed = FALSE;
  set_message(a, NULL);
  terminal_check(a);
  ollama_view_undo(GTK_WINDOW(a->window));update(a);
}
static void restart_clicked(GtkButton *button, gpointer data) {
  (void)button;
  App *a = data;
  reset_game(a);
  ollama_view_begin(GTK_WINDOW(a->window));
  terminal_check(a);
  update(a);
  schedule(a);
}
static void configure_clicked(GtkButton *button, gpointer data) {
    ollama_view_clear(GTK_WINDOW(((App *)data)->window));
  (void)button;
  App *a = data;
  stop_work(a);
  a->playing = FALSE;
  a->paused = a->failed = FALSE;
  set_message(a, NULL);
  update(a);
}
static void retry_clicked(GtkButton *button, gpointer data) {
  (void)button;
  App *a = data;
  stop_work(a);
  a->failed = a->paused = a->recorded = FALSE;
  set_message(a, NULL);
  terminal_check(a);
  update(a);
  schedule(a);
}
static void refresh_clicked(GtkButton *button, gpointer data) {
  (void)button;
  App *a = data;
  if (!a->task)
    launch(a, TRUE);
}
static void level_changed(GObject *object, GParamSpec *spec, gpointer data) {
  (void)object;
  (void)spec;
  App *a = data;
  if (a->level && a->pack) {
    reset_game(a);
    update(a);
  }
}
static void install_levels(App *a, GPtrArray *pack) {
  stop_work(a);
  a->playing = FALSE;
  GPtrArray *old = a->pack;
  a->pack = pack;
  GtkStringList *names = gtk_string_list_new(NULL);
  for (unsigned i = 0; i < pack->len; i++) {
    Level *l = g_ptr_array_index(pack, i);
    g_autofree char *label = g_strdup_printf("%u. %s", i + 1, l->name);
    gtk_string_list_append(names, label);
  }
  g_signal_handler_block(a->level, a->level_handler);
  gtk_drop_down_set_model(GTK_DROP_DOWN(a->level), G_LIST_MODEL(names));
  gtk_drop_down_set_selected(GTK_DROP_DOWN(a->level), 0);
  g_signal_handler_unblock(a->level, a->level_handler);
  g_object_unref(names);
  reset_game(a);
  if (old)
    g_ptr_array_unref(old);
  update(a);
}
static gboolean import_path(App *a, const char *path) {
  GError *error = NULL;
  GPtrArray *pack = levels_load(path, &error);
  if (!pack) {
    set_message(a, error->message);
    g_clear_error(&error);
    update(a);
    return FALSE;
  }
  install_levels(a, pack);
  return TRUE;
}
static void file_chosen(GObject *source, GAsyncResult *result, gpointer data) {
  App *a = data;
  GError *error = NULL;
  GFile *file =
      gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, &error);
  if (!a->closing && file) {
    g_autofree char *path = g_file_get_path(file);
    if (path)
      import_path(a, path);
    else {
      set_message(a, _("Select a local XSB file."));
      update(a);
    }
  }
  g_clear_object(&file);
  g_clear_error(&error);
  app_unref(a);
}
static void import_clicked(GtkButton *button, gpointer data) {
  (void)button;
  App *a = data;
  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, _("Import XSB levels"));
  gtk_file_dialog_open(dialog, GTK_WINDOW(a->window), NULL, file_chosen,
                       app_ref(a));
  g_object_unref(dialog);
}
static void builtin_clicked(GtkButton *button, gpointer data) {
  (void)button;
  install_levels(data, levels_builtin());
}
static void controller_changed(GObject *object, GParamSpec *spec,
                               gpointer data) {
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
  if (!i18n_save(a->settings, &error)) {
    set_message(a, error->message);
    g_clear_error(&error);
  }
  update(a);

    ollama_view_retranslate(GTK_WINDOW(a->window));
}
static gboolean key_pressed(GtkEventControllerKey *controller, guint key,
                            guint code, GdkModifierType state, gpointer data) {
  (void)controller;
  (void)code;
  App *a = data;
  if (!a->playing || a->kind != PLAYER_HUMAN || a->terminal ||
      a->animation.active || a->paused ||
      (state & (GDK_CONTROL_MASK | GDK_ALT_MASK)))
    return FALSE;
  Direction d;
  switch (key) {
  case GDK_KEY_Up:
  case GDK_KEY_w:
  case GDK_KEY_W:
    d = UP;
    break;
  case GDK_KEY_Down:
  case GDK_KEY_s:
  case GDK_KEY_S:
    d = DOWN;
    break;
  case GDK_KEY_Left:
  case GDK_KEY_a:
  case GDK_KEY_A:
    d = LEFT;
    break;
  case GDK_KEY_Right:
  case GDK_KEY_d:
  case GDK_KEY_D:
    d = RIGHT;
    break;
  default:
    return FALSE;
  }
  return start_step(a, d);
}
static double board_scale(App *a, int width, int height, double *ox,
                          double *oy) {
  double scale = MIN((width - 40.0) / a->game.level->width,
                     (height - 40.0) / a->game.level->height);
  *ox = (width - scale * a->game.level->width) / 2;
  *oy = (height - scale * a->game.level->height) / 2;
  return scale;
}
static void board_clicked(GtkGestureClick *gesture, int presses, double x,
                          double y, gpointer data) {
  (void)gesture;
  (void)presses;
  App *a = data;
  if (!a->playing || a->kind != PLAYER_HUMAN || a->terminal ||
      a->animation.active)
    return;
  double ox, oy,
      scale = board_scale(a, gtk_widget_get_width(a->board),
                          gtk_widget_get_height(a->board), &ox, &oy);
  int col = floor((x - ox) / scale), row = floor((y - oy) / scale);
  if (col < 0 || row < 0 || col >= (int)a->game.level->width ||
      row >= (int)a->game.level->height)
    return;
  int cell = row * a->game.level->width + col;
  for (Direction d = UP; d <= RIGHT; d++)
    if (game_neighbor(a->game.level, a->game.pos.player, d) == cell) {
      start_step(a, d);
      break;
    }
  gtk_widget_grab_focus(a->board);
}
static void rounded_box(cairo_t *cr, double x, double y, double size,
                        gboolean goal) {
  cairo_set_source_rgb(cr, goal ? .30 : .72, goal ? .65 : .43,
                       goal ? .43 : .18);
  cairo_rectangle(cr, x + .12 * size, y + .12 * size, .76 * size, .76 * size);
  cairo_fill_preserve(cr);
  cairo_set_source_rgb(cr, .27, .17, .08);
  cairo_set_line_width(cr, MAX(1, size * .035));
  cairo_stroke(cr);
  cairo_move_to(cr, x + .24 * size, y + .24 * size);
  cairo_line_to(cr, x + .76 * size, y + .76 * size);
  cairo_move_to(cr, x + .76 * size, y + .24 * size);
  cairo_line_to(cr, x + .24 * size, y + .76 * size);
  cairo_stroke(cr);
}
static void draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                 gpointer data) {
  (void)area;
  App *a = data;
  if (!a->game.level)
    return;
  const Level *l = a->game.level;
  double ox, oy, scale = board_scale(a, width, height, &ox, &oy);
  AnimationFrame frame = animation_frame(&a->animation, g_get_monotonic_time());
  double t = a->animation.active ? frame.position : 0;
  cairo_set_source_rgb(cr, .12, .15, .18);
  cairo_paint(cr);
  cairo_save(cr);
  cairo_translate(cr, ox, oy);
  for (unsigned y = 0; y < l->height; y++)
    for (unsigned x = 0; x < l->width; x++) {
      unsigned cell = y * l->width + x;
      gboolean floor = l->terrain[cell] & TILE_FLOOR;
      cairo_set_source_rgb(cr, floor ? .85 : .29, floor ? .83 : .34,
                           floor ? .74 : .39);
      cairo_rectangle(cr, x * scale + 1, y * scale + 1, scale - 2, scale - 2);
      cairo_fill(cr);
      if (l->terrain[cell] & TILE_GOAL) {
        cairo_set_source_rgb(cr, .32, .56, .37);
        cairo_arc(cr, (x + .5) * scale, (y + .5) * scale, scale * .19, 0,
                  2 * G_PI);
        cairo_fill(cr);
      }
    }
  int moving = a->animation.active && a->pushing
                   ? game_neighbor(l, a->game.pos.player, a->direction)
                   : -1;
  for (unsigned i = 0; i < l->boxes; i++) {
    unsigned cell = a->game.pos.boxes[i];
    double x = cell % l->width, y = cell / l->width;
    if ((int)cell == moving) {
      int dest = game_neighbor(l, cell, a->direction);
      x += ((int)(dest % l->width) - (int)(cell % l->width)) * t;
      y += ((int)(dest / l->width) - (int)(cell / l->width)) * t;
    }
    rounded_box(cr, x * scale, y * scale, scale, l->terrain[cell] & TILE_GOAL);
  }
  unsigned cell = a->game.pos.player;
  double x = cell % l->width, y = cell / l->width;
  if (a->animation.active) {
    x += ((int)(a->pending.pos.player % l->width) - (int)(cell % l->width)) * t;
    y += ((int)(a->pending.pos.player / l->width) - (int)(cell / l->width)) * t;
  }
  cairo_set_source_rgba(cr, .13, .40, .79,
                        a->animation.active ? frame.opacity : 1);
  cairo_arc(cr, (x + .5) * scale, (y + .5) * scale, scale * .30, 0, 2 * G_PI);
  cairo_fill(cr);
  cairo_set_source_rgba(cr, 1, 1, 1, a->animation.active ? frame.opacity : 1);
  cairo_arc(cr, (x + .57) * scale, (y + .43) * scale, scale * .065, 0,
            2 * G_PI);
  cairo_fill(cr);
  cairo_restore(cr);
}
static void retranslate(GtkWidget *widget) {
  const char *text = g_object_get_data(G_OBJECT(widget), "translation");
  if (text) {
    if (GTK_IS_LABEL(widget))
      gtk_label_set_text(GTK_LABEL(widget), _(text));
    else if (GTK_IS_CHECK_BUTTON(widget))
      gtk_check_button_set_label(GTK_CHECK_BUTTON(widget), _(text));
    else if (GTK_IS_BUTTON(widget))
      gtk_button_set_label(GTK_BUTTON(widget), _(text));
  }
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
       child = gtk_widget_get_next_sibling(child))
    retranslate(child);
}
static void update(App *a) {
  if (!a->status || !a->game.level)
    return;
  PlayerKind visible_kind =
      a->playing ? a->kind
                 : gtk_drop_down_get_selected(GTK_DROP_DOWN(a->controller));
  gtk_widget_set_visible(a->search_settings, visible_kind == PLAYER_ASTAR ||
                                                 visible_kind == PLAYER_GREEDY);
  gtk_widget_set_visible(a->pruning, visible_kind == PLAYER_RANDOM ||
                                         visible_kind == PLAYER_ASTAR ||
                                         visible_kind == PLAYER_GREEDY);
  gtk_widget_set_visible(a->push_settings, visible_kind == PLAYER_RANDOM ||
                                               visible_kind == PLAYER_OLLAMA);
  gtk_widget_set_visible(a->ollama_settings, visible_kind == PLAYER_OLLAMA);
  retranslate(a->window);
  unsigned selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(a->controller));
  GtkStringObject *first =
      G_LIST_MODEL(gtk_drop_down_get_model(GTK_DROP_DOWN(a->controller)))
          ? g_list_model_get_item(
                gtk_drop_down_get_model(GTK_DROP_DOWN(a->controller)), 0)
          : NULL;
  if (!first || g_strcmp0(gtk_string_object_get_string(first), _("Human"))) {
    GtkStringList *names = gtk_string_list_new(NULL);
    for (PlayerKind p = PLAYER_HUMAN; p <= PLAYER_OLLAMA; p++)
      gtk_string_list_append(names, _(player_name(p)));
    gtk_drop_down_set_model(GTK_DROP_DOWN(a->controller), G_LIST_MODEL(names));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(a->controller), selected);
    g_object_unref(names);
  }
  g_clear_object(&first);
  gtk_button_set_label(GTK_BUTTON(a->begin), _("Begin"));
  gtk_button_set_label(GTK_BUTTON(a->pause), _(a->paused ? "Resume" : "Pause"));
  gtk_button_set_label(GTK_BUTTON(a->undo_button), _("Undo"));
  gtk_button_set_label(GTK_BUTTON(a->restart), _("Restart"));
  gtk_button_set_label(GTK_BUTTON(a->configure), _("Configure"));
  gtk_button_set_label(GTK_BUTTON(a->retry), _("Retry"));
  gtk_button_set_label(GTK_BUTTON(a->import), _("Import XSB levels"));
  gtk_button_set_label(GTK_BUTTON(a->refresh), _("Refresh models"));
  gtk_widget_set_visible(a->begin, !a->playing);
  gtk_widget_set_visible(a->pause, a->playing && a->kind != PLAYER_HUMAN);
  gtk_widget_set_sensitive(a->pause, !a->terminal && !a->failed);
  gtk_widget_set_visible(a->undo_button, a->playing && a->kind == PLAYER_HUMAN);
  gtk_widget_set_sensitive(a->undo_button, a->undo->len > 0);
  gtk_widget_set_visible(a->retry, a->failed || (a->terminal && !a->recorded));
  gtk_widget_set_sensitive(a->level, !a->playing);
  gtk_widget_set_sensitive(a->controller, !a->playing);
  gtk_widget_set_sensitive(a->import, !a->playing);
  gtk_widget_set_sensitive(a->refresh, !a->task && !a->playing);
  gtk_widget_set_sensitive(a->url, !a->playing);
  gtk_widget_set_sensitive(a->model, !a->playing);
  gtk_widget_set_sensitive(a->seconds, !a->playing || a->failed);
  gtk_widget_set_sensitive(a->memory, !a->playing || a->failed);
  gtk_widget_set_sensitive(a->pruning, !a->playing || a->failed);
  gtk_widget_set_sensitive(a->push_limit, !a->playing || a->failed);
  unsigned goals = 0;
  for (unsigned i = 0; i < a->game.level->boxes; i++)
    goals += !!(a->game.level->terrain[a->game.pos.boxes[i]] & TILE_GOAL);
  const char *state = !a->playing ? _("Choose a level and controller.")
                      : game_solved(&a->game) ? _("Solved")
                      : a->terminal ? _("Deadlocked — Undo or Restart")
                      : a->failed   ? _("Stopped — adjust limits and Retry")
                      : a->paused   ? _("Paused")
                      : a->task     ? _("Planning…")
                      : a->kind == PLAYER_HUMAN ? _("Move with arrows or WASD.")
                                                : _("Playing…");
  g_autofree char *status =
      g_strdup_printf(_("%s\nPushes: %u · Walking: %u · Goals: %u/%u\nNodes: "
                        "%llu · %.2f s · %.1f MiB"),
                      state, a->game.pushes, a->game.walking, goals,
                      a->game.level->boxes, (unsigned long long)a->nodes,
                      a->elapsed / 1000000.0, a->peak / 1048576.0);
  gtk_label_set_text(GTK_LABEL(a->status), status);
  gtk_label_set_text(GTK_LABEL(a->error), a->message ? _(a->message) : "");
  gtk_widget_set_visible(a->error, a->message != NULL);
  gtk_widget_queue_draw(a->board);
}
static GtkWidget *button(GtkWidget *box, const char *label, GCallback callback,
                         App *a) {
  GtkWidget *w = gtk_button_new_with_label(_(label));
  gtk_box_append(GTK_BOX(box), w);
  g_signal_connect(w, "clicked", callback, a);
  g_object_set_data_full(G_OBJECT(w), "translation", g_strdup(label), g_free);
  return w;
}
static void label(GtkWidget *box, const char *text) {
  GtkWidget *w = gtk_label_new(_(text));
  gtk_label_set_xalign(GTK_LABEL(w), 0);
  gtk_box_append(GTK_BOX(box), w);
  g_object_set_data_full(G_OBJECT(w), "translation", g_strdup(text), g_free);
}
static GtkWidget *spin(GtkWidget *box, const char *title, double min,
                       double max, double increment, double value) {
  label(box, title);
  GtkWidget *w = gtk_spin_button_new_with_range(min, max, increment);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), value);
  gtk_box_append(GTK_BOX(box), w);
  return w;
}
static gboolean close_requested(GtkWindow *window, gpointer data) {
  (void)window;
  App *a = data;
  a->closing = TRUE;
  stop_work(a);
  if (a->tick) {
    g_source_remove(a->tick);
    a->tick = 0;
  }
  return FALSE;
}
static App *create_app(GtkApplication *application) {
  App *a = g_new0(App, 1);
  a->refs = 1;
  a->undo = g_array_new(FALSE, FALSE, sizeof(Game));
  a->sequence = g_string_new(NULL);
  a->history = g_string_new(NULL);
  a->random = g_rand_new();
  a->settings = g_build_filename(g_get_user_config_dir(), "ai-games",
                                 "sokoban.ini", NULL);
  i18n_load(a->settings, NULL);
  a->window = adw_application_window_new(application);
  gtk_window_set_title(GTK_WINDOW(a->window), GAME_NAME);
  gtk_window_set_default_size(GTK_WINDOW(a->window), 1120, 800);
  g_object_set_data_full(G_OBJECT(a->window), "app", a, app_unref);
  g_signal_connect(a->window, "close-request", G_CALLBACK(close_requested), a);
  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(a->window), root);
  GtkWidget *header=adw_header_bar_new();
  gtk_box_append(GTK_BOX(root), header);
  GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_vexpand(content, TRUE);
  gtk_box_append(GTK_BOX(root), content);
  GtkWidget *left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_hexpand(left, TRUE);
  gtk_box_append(GTK_BOX(content), left);
  a->board = gtk_drawing_area_new();
  gtk_widget_set_hexpand(a->board, TRUE);
  gtk_widget_set_vexpand(a->board, TRUE);
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(a->board), 650);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(a->board), 450);
  gtk_widget_set_focusable(a->board, TRUE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(a->board), draw, a, NULL);
  gtk_box_append(GTK_BOX(left), a->board);
  a->status = gtk_label_new(NULL);
  gtk_label_set_wrap(GTK_LABEL(a->status), TRUE);
  gtk_label_set_selectable(GTK_LABEL(a->status), TRUE);
  gtk_widget_set_margin_bottom(a->status, 12);
  a->error = gtk_label_new(NULL);
  gtk_label_set_wrap(GTK_LABEL(a->error), TRUE);
  gtk_widget_add_css_class(a->error, "error");
  gtk_box_append(GTK_BOX(left), ui_message_panel(a->status, a->error));
  GtkWidget *scroll = gtk_scrolled_window_new();
  gtk_widget_set_size_request(scroll, 300, -1);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                 GTK_POLICY_AUTOMATIC);
  gtk_box_append(GTK_BOX(content), scroll);
  GtkWidget *side = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
  gtk_widget_set_margin_end(side, 12);
  gtk_widget_set_margin_bottom(side, 12);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), side);
  label(side, "Level");
  a->level = gtk_drop_down_new(NULL, NULL);
  gtk_box_append(GTK_BOX(side), a->level);
  a->level_handler = g_signal_connect(a->level, "notify::selected",
                                      G_CALLBACK(level_changed), a);
  a->import = button(side, "Import XSB levels", G_CALLBACK(import_clicked), a);
  button(side, "Built-in levels", G_CALLBACK(builtin_clicked), a);
  label(side, "Controller");
  const char *controllers[] = {"Human", "Random", "Greedy Best-first",
                               "A*",    "Ollama", NULL};
  a->controller = gtk_drop_down_new_from_strings(controllers);
  gtk_box_append(GTK_BOX(side), a->controller);
  a->search_settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
  gtk_box_append(GTK_BOX(side), a->search_settings);
  a->seconds = spin(a->search_settings, "Planning seconds (0: unlimited)", 0,
                    86400, 1, 60);
  a->memory =
      spin(a->search_settings, "Solver memory limit (MiB)", 1, 4096, 16, 256);
  a->pruning = gtk_check_button_new_with_label(_("Deadlock pruning"));
  gtk_check_button_set_active(GTK_CHECK_BUTTON(a->pruning), TRUE);
  g_object_set_data(G_OBJECT(a->pruning), "translation", "Deadlock pruning");
  gtk_box_append(GTK_BOX(side), a->pruning);
  a->push_settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
  gtk_box_append(GTK_BOX(side), a->push_settings);
  a->push_limit = spin(a->push_settings, "Random / Ollama push limit", 1,
                       1000000, 100, 10000);
  a->delay = spin(side, "Animation delay (seconds)", 0, 3, .1, .3);
  a->ollama_settings = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
  gtk_box_append(GTK_BOX(side), a->ollama_settings);
  label(a->ollama_settings, "Ollama address");
  a->url = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(a->url), OLLAMA_DEFAULT_URL);
  gtk_box_append(GTK_BOX(a->ollama_settings), a->url);
  a->refresh = button(a->ollama_settings, "Refresh models",
                      G_CALLBACK(refresh_clicked), a);
  a->model = gtk_drop_down_new(NULL, NULL);
  gtk_box_append(GTK_BOX(a->ollama_settings), a->model);
  a->thinking = gtk_check_button_new_with_label(_("Enable thinking"));
  gtk_box_append(GTK_BOX(a->ollama_settings), a->thinking);
  g_object_set_data(G_OBJECT(a->thinking), "translation", "Enable thinking");
  a->begin = button(side, "Begin", G_CALLBACK(begin_clicked), a);
  a->pause = button(side, "Pause", G_CALLBACK(pause_clicked), a);
  a->undo_button = button(side, "Undo", G_CALLBACK(undo_clicked), a);
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
  GtkEventController *keys = gtk_event_controller_key_new();
  g_signal_connect(keys, "key-pressed", G_CALLBACK(key_pressed), a);
  gtk_widget_add_controller(a->board, keys);
  GtkGesture *click = gtk_gesture_click_new();
  g_signal_connect(click, "pressed", G_CALLBACK(board_clicked), a);
  gtk_widget_add_controller(a->board, GTK_EVENT_CONTROLLER(click));
  g_signal_connect(a->controller, "notify::selected",
                   G_CALLBACK(controller_changed), a);
  install_levels(a, levels_builtin());
  a->tick = g_timeout_add(16, tick, a);

  adw_header_bar_pack_end(ADW_HEADER_BAR(header),ollama_view_install(GTK_WINDOW(a->window),a->controller,NULL,PLAYER_OLLAMA,i18n_translate));
  return a;
}
#ifndef SOKOBAN_TEST
static void activate(GtkApplication *application, gpointer data) {
  (void)data;
  App *a = create_app(application);
  gtk_window_present(GTK_WINDOW(a->window));
}
int main(int argc, char **argv) {
  AdwApplication *app = adw_application_new("io.github.aigames.Sokoban",
                                            G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  int result = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return result;
}
#endif
