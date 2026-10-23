// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../tools/ollama-view.h"
#include "../../tools/ui-panels.h"
#include "game.h"
#include "i18n.h"
#include "ollama.h"
#include "search.h"
#include <adwaita.h>
#include <math.h>

typedef struct {
  GtkWindow *window;
  GtkWidget *board, *status, *error, *score, *pause, *rules;
  GtkWidget *kind[2], *model[2], *depth[2], *thinking, *harness, *language;
  Game game, previous;
  GString *history;
  GCancellable *cancel;
  guint timer, generation;
  gint64 animation_start;
  int animation_source, animation_dest, animation_player, animation_capture,
      selected;
  GameMove draft;
  gboolean capturing;
  gboolean paused, busy, animating, translating;
} App;
static App app;
static void update(void);
static void schedule(void);
static const char *preference(void) {
  static char *path;
  if (!path)
    path = g_build_filename(g_get_user_config_dir(), "ai-games", GAME_ID ".ini",
                            NULL);
  return path;
}
static void coordinates(int pos, double *x, double *y) {
  *x = 85 + game_x[pos] * 55;
  *y = 20 + game_y[pos] * 55;
}
static void disc(cairo_t *cr, double x, double y, double radius, int side) {
  cairo_set_source_rgb(cr, side == 1 ? .93 : .17, side == 1 ? .88 : .28,
                       side == 1 ? .7 : .4);
  cairo_new_path(cr);
  cairo_arc(cr, x, y, radius, 0, 2 * G_PI);
  cairo_fill_preserve(cr);
  cairo_set_source_rgb(cr, .12, .12, .13);
  cairo_set_line_width(cr, 2);
  cairo_stroke(cr);
}
static void draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                 gpointer data) {
  (void)area;
  (void)data;
  double scale = MIN(width / 500., height / 400.);
  cairo_translate(cr, (width - 500 * scale) / 2, (height - 400 * scale) / 2);
  cairo_scale(cr, scale, scale);
  cairo_set_source_rgb(cr, .91, .87, .77);
  cairo_rectangle(cr, 25, 0, 450, 400);
  cairo_fill(cr);
  cairo_set_source_rgb(cr, .15, .22, .25);
  cairo_set_line_width(cr, 3);
  for (int p = 0; p < GAME_POINTS; p++)
    for (int q = p + 1; q < GAME_POINTS; q++)
      if (game_neighbors[p] & (1u << q)) {
        double x1, y1, x2, y2;
        coordinates(p, &x1, &y1);
        coordinates(q, &x2, &y2);
        cairo_move_to(cr, x1, y1);
        cairo_line_to(cr, x2, y2);
        cairo_stroke(cr);
      }
  for (int p = 0; p < GAME_POINTS; p++) {
    double x, y;
    coordinates(p, &x, &y);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x, y, 9, 0, 2 * G_PI);
    cairo_set_source_rgb(cr, .7, .67, .59);
    cairo_fill(cr);
    GameMoves legal;
    game_legal_moves(&app.game, &legal);
    gboolean highlighted = FALSE;
    for (unsigned i = 0; i < legal.count; i++) {
      GameMove m = legal.moves[i];
      if (app.capturing ? (m.from == app.draft.from && m.to == app.draft.to &&
                           m.capture == p)
                        : (m.from == app.selected && m.to == p))
        highlighted = TRUE;
    }
    if (highlighted) {
      cairo_arc(cr, x, y, 20, 0, 2 * G_PI);
      cairo_set_source_rgb(cr, .1, .7, .8);
      cairo_stroke(cr);
    }
    unsigned owner = game_cell(&app.game, p);
    if (app.capturing) {
      if (p == app.draft.from)
        owner = 0;
      if (p == app.draft.to)
        owner = app.game.turn;
    }
    if (owner && !(app.animating && p == app.animation_dest))
      disc(cr, x, y, 13, owner);
    if (app.animating && p == app.animation_capture) {
      double progress =
          MIN(1., (g_get_monotonic_time() - app.animation_start) / 300000.);
      disc(cr, x, y, 13 * (1 - progress), 3 - app.animation_player);
    }
    if (p == app.selected) {
      cairo_new_sub_path(cr);
      cairo_arc(cr, x, y, 22, 0, 2 * G_PI);
      cairo_set_source_rgb(cr, .95, .55, .1);
      cairo_set_line_width(cr, 3);
      cairo_stroke(cr);
    }
    cairo_set_source_rgb(cr, .2, .2, .2);
    cairo_set_font_size(cr, 15);
    cairo_move_to(cr, x - 5, y + 36);
    cairo_show_text(cr, game_labels[p]);
  }
  if (app.animating) {
    double x1, y1, x2, y2;
    coordinates(app.animation_source < 0 ? app.animation_dest
                                         : app.animation_source,
                &x1, &y1);
    coordinates(app.animation_dest, &x2, &y2);
    double progress =
        MIN(1., (g_get_monotonic_time() - app.animation_start) / 300000.);
    disc(cr, x1 + (x2 - x1) * progress, y1 + (y2 - y1) * progress,
         app.animation_source < 0 ? 13 * progress : 13, app.animation_player);
  }
}
static void stop(void) {
  app.generation++;
  if (app.timer) {
    g_source_remove(app.timer);
    app.timer = 0;
  }
  if (app.cancel) {
    g_cancellable_cancel(app.cancel);
    g_clear_object(&app.cancel);
  }
  app.busy = FALSE;
  ollama_view_invalidate(app.window);
}
static gboolean animate(GtkWidget *widget, GdkFrameClock *clock,
                        gpointer data) {
  (void)clock;
  (void)data;
  gtk_widget_queue_draw(widget);
  if (g_get_monotonic_time() - app.animation_start < 300000)
    return G_SOURCE_CONTINUE;
  app.animating = FALSE;
  update();
  schedule();
  return G_SOURCE_REMOVE;
}
static void play(GameMove move) {
  unsigned player = game_actor(&app.game);
  app.previous = app.game;
  if (!game_play(&app.game, move))
    return;
  app.capturing = FALSE;
  char label[16];
  game_move_label(move, label);
  g_string_append_printf(app.history, "%u: player %u move %s\n",
                         app.game.actions, player, label);
  if (game_result(&app.game)) {
    g_string_append_printf(app.history,
                           "Termination reason: %d; quiet turns %u.\n",
                           app.game.end, app.game.quiet);
    if (game_result(&app.game) == GAME_DRAW)
      g_string_append(app.history, "Result: draw\n\n");
    else
      g_string_append_printf(app.history, "Winner: player %d\n\n",
                             game_result(&app.game));
    GError *error = NULL;
    if (!game_append_report(GAME_HISTORY_FILE, app.history->str, &error)) {
      gtk_label_set_text(GTK_LABEL(app.error), error->message);
      g_clear_error(&error);
    }
  }
  app.selected = -1;
  if (move.to >= 0) {
    app.animating = TRUE;
    app.animation_start = g_get_monotonic_time();
    app.animation_source = move.from;
    app.animation_dest = move.to;
    app.animation_player = player;
    app.animation_capture = move.capture;
    gtk_widget_add_tick_callback(app.board, animate, NULL, NULL);
  }
  update();
  if (!app.animating)
    schedule();
}
typedef struct {
  Game game;
  guint generation, seed, kind, depth;
  char *model;
  OllamaOptions options;
  GameMove move;
  OllamaResult result;
  SearchResult search;
} Work;
static void free_work(gpointer data) {
  Work *w = data;
  g_free(w->model);
  g_free(w);
}
static gboolean cancelled(gpointer data) {
  return g_cancellable_is_cancelled(data);
}
static void worker(GTask *task, gpointer source, gpointer data,
                   GCancellable *cancel) {
  (void)source;
  Work *w = data;
  if (w->kind == 5) {
    OllamaResult result;
    GError *error = NULL;
    if (!ollama_choose_with_options(&w->game, w->model, NULL,
                                    OLLAMA_MOVE_TIMEOUT_MS, cancel, &w->options,
                                    &result, &error)) {
      g_task_return_error(task, error);
      return;
    }
    w->move = result.move;
    w->result = result;
  } else {
    GRand *rng = g_rand_new_with_seed(w->seed);
    if (w->kind == 1) {
      GameMoves moves;
      game_legal_moves(&w->game, &moves);
      w->move = moves.moves[g_rand_int_range(rng, 0, moves.count)];
    } else {
      w->search =
          search_choose_full(&w->game, w->kind == 2 ? 1 : w->depth, 50000,
                             w->kind == 4, rng, cancelled, cancel);
      w->move = w->search.move;
    }
    g_rand_free(rng);
  }
  g_task_return_boolean(task, TRUE);
}
static void complete(GObject *source, GAsyncResult *result, gpointer data) {
  (void)source;
  (void)data;
  GTask *task = G_TASK(result);
  Work *w = g_task_get_task_data(task);
  GError *error = NULL;
  gboolean ok = g_task_propagate_boolean(task, &error);
  if (w->generation != app.generation) {
    g_clear_error(&error);
    return;
  }
  app.busy = FALSE;
  g_clear_object(&app.cancel);
  if (!ok) {
    gtk_label_set_text(GTK_LABEL(app.error), error->message);
    g_clear_error(&error);
    app.paused = TRUE;
    update();
    return;
  }
  if (w->kind == 5)
    ollama_record(app.history, w->model, &w->result);
  else if (w->kind >= 2)
    g_string_append_printf(
        app.history,
        "Search controller %u: completed depth %u; nodes %" G_GUINT64_FORMAT
        "; node budget 50000.\n",
        w->kind, w->search.depth, w->search.nodes);
  play(w->move);
}
static gboolean advance(gpointer data) {
  (void)data;
  app.timer = 0;
  if (app.paused || app.busy || app.animating || game_result(&app.game))
    return G_SOURCE_REMOVE;
  unsigned s = game_actor(&app.game) - 1;
  unsigned kind = gtk_drop_down_get_selected(GTK_DROP_DOWN(app.kind[s]));
  if (!kind)
    return G_SOURCE_REMOVE;
  Work *w = g_new0(Work, 1);
  w->game = app.game;
  w->generation = app.generation;
  /* Stable per-position decision seed; no random numbers are consumed by rules.
   */
  w->seed = app.game.actions * 2654435761u + app.game.turn;
  w->kind = kind;
  w->depth = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app.depth[s]));
  w->model = g_strdup(gtk_editable_get_text(GTK_EDITABLE(app.model[s])));
  w->options.thinking =
      gtk_check_button_get_active(GTK_CHECK_BUTTON(app.thinking));
  w->options.harness =
      gtk_check_button_get_active(GTK_CHECK_BUTTON(app.harness));
  app.cancel = g_cancellable_new();
  app.busy = TRUE;
  if (kind == 5) {
    ollama_view_bind(app.window, app.cancel, s == 0 ? "Player 1" : "Player 2",
                     app.game.actions + 1, w->model);
  }
  GTask *task = g_task_new(NULL, app.cancel, complete, NULL);
  g_task_set_task_data(task, w, free_work);
  g_task_run_in_thread(task, worker);
  g_object_unref(task);
  update();
  return G_SOURCE_REMOVE;
}
static void schedule(void) {
  if (!app.timer && !app.paused && !app.busy && !app.animating &&
      !game_result(&app.game))
    app.timer = g_timeout_add(400, advance, NULL);
}
static void update(void) {
  char text[256];
  int result = game_result(&app.game);
  if (result == GAME_DRAW)
    g_snprintf(text, sizeof text, "%s",
               _(app.game.end == GAME_REPETITION
                     ? "Draw by threefold repetition"
                     : "Draw after 50 turns without capture"));
  else if (result)
    g_snprintf(text, sizeof text, _("Winner: player %d"), result);
  else
    g_snprintf(text, sizeof text, _("Player %u — %s"), game_actor(&app.game),
               _(app.capturing ? "Choose an opposing piece to capture"
                 : game_placing(&app.game) ? "Place a reserve piece"
                 : game_count(&app.game, app.game.turn) == 3
                     ? "Fly to any empty point"
                     : "Select a piece, then an adjacent point"));
  gtk_label_set_text(GTK_LABEL(app.status), text);
  g_snprintf(text, sizeof text,
             _("Reserves P1 / P2: %u / %u     Board P1 / P2: %u / %u     Quiet "
               "turns: %u / 50"),
             app.game.reserve[0], app.game.reserve[1], game_count(&app.game, 1),
             game_count(&app.game, 2), app.game.quiet);
  gtk_label_set_text(GTK_LABEL(app.score), text);
  gtk_button_set_label(GTK_BUTTON(app.pause),
                       _(app.paused ? "Resume" : "Pause"));
  gtk_widget_queue_draw(app.board);
}
static void clicked(GtkGestureClick *gesture, int presses, double x, double y,
                    gpointer data) {
  (void)gesture;
  (void)presses;
  (void)data;
  if (app.busy || app.paused || app.animating ||
      gtk_drop_down_get_selected(
          GTK_DROP_DOWN(app.kind[game_actor(&app.game) - 1])))
    return;
  if (game_result(&app.game))
    return;
  double width = gtk_widget_get_width(app.board),
         height = gtk_widget_get_height(app.board);
  double scale = MIN(width / 500., height / 400.);
  x = (x - (width - 500 * scale) / 2) / scale;
  y = (y - (height - 400 * scale) / 2) / scale;
  for (int p = 0; p < GAME_POINTS; p++) {
    double px, py;
    coordinates(p, &px, &py);
    if (hypot(x - px, y - py) > 24)
      continue;
    GameMoves legal;
    game_legal_moves(&app.game, &legal);
    if (app.capturing) {
      for (unsigned i = 0; i < legal.count; i++) {
        GameMove m = legal.moves[i];
        if (m.from == app.draft.from && m.to == app.draft.to &&
            m.capture == p) {
          play(m);
          return;
        }
      }
      app.capturing = FALSE;
      app.selected = -1;
    } else {
      for (unsigned i = 0; i < legal.count; i++) {
        GameMove m = legal.moves[i];
        if (m.from == app.selected && m.to == p) {
          if (m.capture < 0) {
            play(m);
            return;
          }
          app.draft = m;
          app.capturing = TRUE;
          update();
          return;
        }
      }
      app.selected = game_cell(&app.game, p) == (unsigned)app.game.turn &&
                             app.selected != p
                         ? p
                         : -1;
    }
    update();
    return;
  }
  app.capturing = FALSE;
  app.selected = -1;
  update();
}
static void restart(GtkButton *b, gpointer data) {
  (void)b;
  (void)data;
  if (app.animating)
    return;
  stop();
  game_init(&app.game);
  app.selected = -1;
  app.capturing = FALSE;
  app.paused = FALSE;
  g_autoptr(GDateTime) now = g_date_time_new_now_local();
  g_autofree char *date = g_date_time_format_iso8601(now);
  g_string_printf(app.history, "## %s — %s\n\n", GAME_NAME, date);
  for (int s = 0; s < 2; s++)
    g_string_append_printf(
        app.history, "Player %d controller %u model %s\n", s + 1,
        gtk_drop_down_get_selected(GTK_DROP_DOWN(app.kind[s])),
        gtk_editable_get_text(GTK_EDITABLE(app.model[s])));
  gtk_label_set_text(GTK_LABEL(app.error), "");
  ollama_view_begin(app.window);
  update();
  schedule();
}
static void pause_clicked(GtkButton *b, gpointer data) {
  (void)b;
  (void)data;
  app.paused = !app.paused;
  if (app.paused)
    stop();
  else
    schedule();
  update();
}
static void changed(GObject *obj, GParamSpec *ps, gpointer data) {
  (void)obj;
  (void)ps;
  (void)data;
  if (app.translating)
    return;
  stop();
  app.capturing = FALSE;
  app.selected = -1;
  update();
  schedule();
}
static gboolean closing(GtkWindow *window, gpointer data) {
  (void)window;
  (void)data;
  stop();
  return FALSE;
}
static void translate_labels(GtkWidget *widget) {
  if (GTK_IS_LABEL(widget)) {
    const char *text = gtk_label_get_text(GTK_LABEL(widget));
    gtk_label_set_text(GTK_LABEL(widget), _(text));
  }
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
       child = gtk_widget_get_next_sibling(child))
    translate_labels(child);
}
static void language_changed(GObject *obj, GParamSpec *ps, gpointer data) {
  (void)obj;
  (void)ps;
  (void)data;
  i18n_set_language(gtk_drop_down_get_selected(GTK_DROP_DOWN(app.language)));
  i18n_save(preference(), NULL);
  translate_labels(GTK_WIDGET(app.window));
  for (int side = 0; side < 2; side++) {
    guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app.kind[side]));
    const char *names[] = {_("Human"),   _("Random"),     _("Heuristic"),
                           _("Minimax"), _("Alpha-beta"), "Ollama",
                           NULL};
    GtkStringList *model = gtk_string_list_new(names);
    app.translating = TRUE;
    gtk_drop_down_set_model(GTK_DROP_DOWN(app.kind[side]), G_LIST_MODEL(model));
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app.kind[side]), selected);
    app.translating = FALSE;
    g_object_unref(model);
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.model[side]),
                                   _("Ollama model"));
  }
  update();
  ollama_view_retranslate(app.window);
}
static void activate(GtkApplication *application, gpointer data) {
  (void)data;
  if (app.window) {
    gtk_window_present(app.window);
    return;
  }
  i18n_load(preference(), NULL);
  app.history = g_string_new(NULL);
  app.window = GTK_WINDOW(adw_application_window_new(application));
  gtk_window_set_title(app.window, GAME_NAME);
  gtk_window_set_default_size(app.window, 850, 650);
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(app.window), box);
  GtkWidget *header = adw_header_bar_new();
  gtk_box_append(GTK_BOX(box), header);
  const char *languages[] = {"System default", "English",  "Português",
                             "Español",        "Italiano", NULL};
  app.language = gtk_drop_down_new_from_strings(languages);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.language), i18n_selection());
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), app.language);
  g_signal_connect(app.language, "notify::selected",
                   G_CALLBACK(language_changed), NULL);
  for (int s = 0; s < 2; s++) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(box), row);
    gtk_box_append(GTK_BOX(row),
                   gtk_label_new(s == 0 ? _("Player 1") : _("Player 2")));
    const char *kinds[] = {_("Human"),   _("Random"),     _("Heuristic"),
                           _("Minimax"), _("Alpha-beta"), "Ollama",
                           NULL};
    app.kind[s] = gtk_drop_down_new_from_strings(kinds);
    gtk_box_append(GTK_BOX(row), app.kind[s]);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app.kind[s]), s == 0 ? 0 : 2);
    app.model[s] = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.model[s]), _("Ollama model"));
    app.depth[s] = gtk_spin_button_new_with_range(1, 6, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app.depth[s]), 4);
    gtk_widget_set_tooltip_text(app.depth[s], _("Search depth"));
    gtk_box_append(GTK_BOX(row), app.depth[s]);
    gtk_widget_set_hexpand(app.model[s], TRUE);
    gtk_box_append(GTK_BOX(row), app.model[s]);
    g_signal_connect(app.kind[s], "notify::selected", G_CALLBACK(changed),
                     NULL);
  }
  for (int side = 0; side < 2; side++) {
    g_signal_connect(app.depth[side], "notify::value", G_CALLBACK(changed),
                     NULL);
    g_signal_connect(app.model[side], "notify::text", G_CALLBACK(changed),
                     NULL);
  }
  GtkWidget *messages = ollama_view_install(app.window, app.kind[0],
                                            app.kind[1], 5, i18n_translate);
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), messages);
  GtkWidget *options = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append(GTK_BOX(box), options);
  app.thinking = gtk_check_button_new_with_label(_("Extended thinking"));
  gtk_box_append(GTK_BOX(options), app.thinking);
  app.harness = gtk_check_button_new_with_label(_("Reasoning harness"));
  gtk_box_append(GTK_BOX(options), app.harness);
  g_signal_connect(app.thinking, "notify::active", G_CALLBACK(changed), NULL);
  g_signal_connect(app.harness, "notify::active", G_CALLBACK(changed), NULL);
  GtkWidget *new_game = gtk_button_new_with_label(_("New game"));
  gtk_box_append(GTK_BOX(options), new_game);
  g_signal_connect(new_game, "clicked", G_CALLBACK(restart), NULL);
  app.pause = gtk_button_new_with_label(_("Pause"));
  gtk_box_append(GTK_BOX(options), app.pause);
  g_signal_connect(app.pause, "clicked", G_CALLBACK(pause_clicked), NULL);
  app.board = gtk_drawing_area_new();
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(app.board), 300);
  gtk_widget_set_vexpand(app.board, TRUE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app.board), draw, NULL, NULL);
  gtk_box_append(GTK_BOX(box), app.board);
  GtkGesture *gesture = gtk_gesture_click_new();
  g_signal_connect(gesture, "pressed", G_CALLBACK(clicked), NULL);
  gtk_widget_add_controller(app.board, GTK_EVENT_CONTROLLER(gesture));
  app.score = gtk_label_new("");
  gtk_box_append(GTK_BOX(box), app.score);
  app.status = gtk_label_new("");
  app.error = gtk_label_new("");
  gtk_label_set_wrap(GTK_LABEL(app.status), TRUE);
  gtk_label_set_wrap(GTK_LABEL(app.error), TRUE);
  gtk_box_append(GTK_BOX(box), ui_message_panel(app.status, app.error));
  app.rules = gtk_label_new(
      _("Place nine pieces each, then move along edges. With three pieces, fly "
        "to any empty point. Closing a mill captures one opponent outside "
        "mills, unless all are protected. One capture even for two mills. "
        "Re-forming mills is allowed. Lose below three pieces or when blocked. "
        "Draw: threefold repetition or 50 movement turns without capture. "
        "Click elsewhere to cancel a capture draft."));
  gtk_label_set_wrap(GTK_LABEL(app.rules), TRUE);
  gtk_box_append(GTK_BOX(box), ui_fixed_text_panel(app.rules, 70));
  g_signal_connect(app.window, "close-request", G_CALLBACK(closing), NULL);
  restart(NULL, NULL);
  gtk_window_present(app.window);
}
#ifndef MORRIS_UI_TEST
int main(int argc, char **argv) {
  g_autoptr(AdwApplication) application = adw_application_new(
      "io.github.aigames.NineMensMorris", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(application, "activate", G_CALLBACK(activate), NULL);
  return g_application_run(G_APPLICATION(application), argc, argv);
}
#endif
