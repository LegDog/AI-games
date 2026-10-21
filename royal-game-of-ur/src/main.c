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
  GtkWidget *board, *status, *error, *roll, *reserve, *pause, *rules;
  GtkWidget *kind[2], *model[2], *thinking, *harness, *language;
  Game game, previous;
  GRand *dice;
  GString *history;
  GCancellable *cancel;
  guint timer, generation;
  gint64 animation_start;
  int animation_source, animation_dest, animation_player;
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
static void coordinates(int side, int pos, int *col, int *row) {
  *row = side == 1 ? 0 : 2;
  if (pos <= 4)
    *col = 4 - pos;
  else if (pos <= 12) {
    *col = pos - 5;
    *row = 1;
  } else
    *col = 20 - pos;
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
  double tile = MIN(width / 8., height / 3.), ox = (width - 8 * tile) / 2,
         oy = (height - 3 * tile) / 2;
  double progress =
      app.animating
          ? MIN(1., (g_get_monotonic_time() - app.animation_start) / 300000.)
          : 1.;
  for (int row = 0; row < 3; row++)
    for (int col = 0; col < 8; col++) {
      if (row != 1 && (col == 4 || col == 5))
        continue;
      double x = ox + col * tile, y = oy + row * tile;
      cairo_set_source_rgb(cr, .77, .59, .33);
      cairo_rectangle(cr, x + 2, y + 2, tile - 4, tile - 4);
      cairo_fill(cr);
      int pos = row == 1 ? col + 5 : col < 4 ? 4 - col : 20 - col;
      if (game_rosette(pos)) {
        cairo_set_source_rgb(cr, .4, .22, .12);
        for (int petal = 0; petal < 8; petal++) {
          double a = petal * G_PI / 4;
          cairo_arc(cr, x + tile / 2 + cos(a) * tile * .2,
                    y + tile / 2 + sin(a) * tile * .2, tile * .075, 0,
                    2 * G_PI);
          cairo_fill(cr);
        }
      }
      cairo_set_source_rgb(cr, .2, .15, .1);
      cairo_set_font_size(cr, tile * .15);
      cairo_move_to(cr, x + tile * .08, y + tile * .2);
      char text[16];
      g_snprintf(text, sizeof text, "%d", pos);
      cairo_show_text(cr, text);
    }
  for (int s = 0; s < 2; s++)
    for (int p = 1; p <= 14; p++)
      if (app.game.piece[s][p]) {
        if (app.animating && s + 1 == app.animation_player &&
            p == app.animation_dest)
          continue;
        int c, r;
        coordinates(s + 1, p, &c, &r);
        disc(cr, ox + (c + .5) * tile, oy + (r + .5) * tile, tile * .29, s + 1);
      }
  if (app.animating) {
    int c1, r1, c2, r2;
    if (app.animation_source == 0) {
      c1 = 4;
      r1 = app.animation_player == 1 ? 0 : 2;
    } else
      coordinates(app.animation_player, app.animation_source, &c1, &r1);
    if (app.animation_dest == 15) {
      c2 = 5;
      r2 = app.animation_player == 1 ? 0 : 2;
    } else
      coordinates(app.animation_player, app.animation_dest, &c2, &r2);
    disc(cr, ox + (c1 + .5 + (c2 - c1) * progress) * tile,
         oy + (r1 + .5 + (r2 - r1) * progress) * tile, tile * .29,
         app.animation_player);
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
  int roll = app.game.roll, player = app.game.turn;
  app.previous = app.game;
  if (!game_play(&app.game, move))
    return;
  char label[16];
  game_move_label(move, label);
  g_string_append_printf(app.history, "%u: player %d roll %d source %s\n",
                         app.game.actions, player, roll, label);
  if (game_result(&app.game)) {
    g_string_append_printf(app.history, "Winner: player %d\n\n",
                           game_result(&app.game));
    GError *error = NULL;
    if (!game_append_report(GAME_HISTORY_FILE, app.history->str, &error)) {
      gtk_label_set_text(GTK_LABEL(app.error), error->message);
      g_clear_error(&error);
    }
  }
  if (move.cell >= 0) {
    app.animating = TRUE;
    app.animation_start = g_get_monotonic_time();
    app.animation_source = move.cell;
    app.animation_dest = move.cell + roll;
    app.animation_player = player;
    gtk_widget_add_tick_callback(app.board, animate, NULL, NULL);
  }
  update();
  if (!app.animating)
    schedule();
}
typedef struct {
  Game game;
  guint generation, seed, kind;
  char *model;
  OllamaOptions options;
  GameMove move;
  OllamaResult result;
} Work;
static void free_work(gpointer data) {
  Work *w = data;
  g_free(w->model);
  g_free(w);
}
static void worker(GTask *task, gpointer source, gpointer data,
                   GCancellable *cancel) {
  (void)source;
  Work *w = data;
  if (w->kind == 4) {
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
    } else
      w->move = search_choose(&w->game, w->kind == 2 ? 1 : 3, rng);
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
  if (w->kind == 4)
    ollama_record(app.history, w->model, &w->result);
  play(w->move);
}
static gboolean advance(gpointer data) {
  (void)data;
  app.timer = 0;
  if (app.paused || app.busy || app.animating || game_result(&app.game))
    return G_SOURCE_REMOVE;
  unsigned s = app.game.turn - 1;
  unsigned kind = gtk_drop_down_get_selected(GTK_DROP_DOWN(app.kind[s]));
  if (!kind)
    return G_SOURCE_REMOVE;
  if (app.game.roll < 0)
    game_roll(&app.game, app.dice);
  Work *w = g_new0(Work, 1);
  w->game = app.game;
  w->generation = app.generation;
  /* Search RNG is independent of actual dice. */
  w->seed = app.game.actions * 2654435761u + app.game.turn;
  w->kind = kind;
  w->model = g_strdup(gtk_editable_get_text(GTK_EDITABLE(app.model[s])));
  w->options.thinking =
      gtk_check_button_get_active(GTK_CHECK_BUTTON(app.thinking));
  w->options.harness =
      gtk_check_button_get_active(GTK_CHECK_BUTTON(app.harness));
  app.cancel = g_cancellable_new();
  app.busy = TRUE;
  if (kind == 4) {
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
  if (game_result(&app.game))
    g_snprintf(text, sizeof text, _("Winner: player %d"),
               game_result(&app.game));
  else if (app.game.roll < 0)
    g_snprintf(text, sizeof text, _("Player %d — roll the dice"),
               app.game.turn);
  else
    g_snprintf(text, sizeof text,
               _("Player %d — roll %d; select a source square or reserve"),
               app.game.turn, app.game.roll);
  gtk_label_set_text(GTK_LABEL(app.status), text);
  g_snprintf(text, sizeof text, _("Reserve: %u / %u     Finished: %u / %u"),
             app.game.reserve[0], app.game.reserve[1], app.game.finished[0],
             app.game.finished[1]);
  gtk_button_set_label(GTK_BUTTON(app.reserve), text);
  gtk_button_set_label(GTK_BUTTON(app.roll),
                       _(app.game.roll < 0 ? "Roll dice" : "Pass"));
  gtk_button_set_label(GTK_BUTTON(app.pause),
                       _(app.paused ? "Resume" : "Pause"));
  gboolean human = gtk_drop_down_get_selected(
                       GTK_DROP_DOWN(app.kind[app.game.turn - 1])) == 0;
  GameMoves moves;
  game_legal_moves(&app.game, &moves);
  gboolean forced = moves.count == 1 && moves.moves[0].cell == -1;
  gtk_widget_set_sensitive(
      app.roll, human && !app.busy && !app.animating && !app.paused &&
                    !game_result(&app.game) && (app.game.roll < 0 || forced));
  gtk_widget_set_sensitive(
      app.reserve, human && !app.busy && !app.animating && !app.paused &&
                       game_move_legal(&app.game, (GameMove){0}));
  gtk_widget_queue_draw(app.board);
}
static void roll_clicked(GtkButton *b, gpointer d) {
  (void)b;
  (void)d;
  if (app.game.roll < 0) {
    game_roll(&app.game, app.dice);
    update();
  } else
    play((GameMove){-1});
}
static void reserve_clicked(GtkButton *b, gpointer d) {
  (void)b;
  (void)d;
  play((GameMove){0});
}
static void clicked(GtkGestureClick *gesture, int presses, double x, double y,
                    gpointer data) {
  (void)gesture;
  (void)presses;
  (void)data;
  if (app.busy || app.paused || app.animating ||
      gtk_drop_down_get_selected(GTK_DROP_DOWN(app.kind[app.game.turn - 1])))
    return;
  double width = gtk_widget_get_width(app.board),
         height = gtk_widget_get_height(app.board);
  double tile = MIN(width / 8., height / 3.);
  x -= (width - 8 * tile) / 2;
  y -= (height - 3 * tile) / 2;
  if (x < 0 || y < 0 || x >= tile * 8 || y >= tile * 3)
    return;
  int col = x / tile, row = y / tile;
  if (row != 1 && (row != (app.game.turn == 1 ? 0 : 2) || col == 4 || col == 5))
    return;
  int pos = row == 1 ? col + 5 : col < 4 ? 4 - col : 20 - col;
  if (game_move_legal(&app.game, (GameMove){pos}))
    play((GameMove){pos});
}
static void restart(GtkButton *b, gpointer data) {
  (void)b;
  (void)data;
  if (app.animating)
    return;
  stop();
  game_init(&app.game);
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
    const char *names[] = {_("Human"),          _("Random"), _("Heuristic"),
                           _("Expectiminimax"), "Ollama",    NULL};
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
  app.dice = g_rand_new();
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
    const char *kinds[] = {_("Human"),          _("Random"), _("Heuristic"),
                           _("Expectiminimax"), "Ollama",    NULL};
    app.kind[s] = gtk_drop_down_new_from_strings(kinds);
    gtk_box_append(GTK_BOX(row), app.kind[s]);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app.kind[s]), s == 0 ? 0 : 2);
    app.model[s] = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.model[s]), _("Ollama model"));
    gtk_widget_set_hexpand(app.model[s], TRUE);
    gtk_box_append(GTK_BOX(row), app.model[s]);
    g_signal_connect(app.kind[s], "notify::selected", G_CALLBACK(changed),
                     NULL);
  }
  GtkWidget *messages = ollama_view_install(app.window, app.kind[0],
                                            app.kind[1], 4, i18n_translate);
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), messages);
  GtkWidget *options = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_box_append(GTK_BOX(box), options);
  app.thinking = gtk_check_button_new_with_label(_("Extended thinking"));
  gtk_box_append(GTK_BOX(options), app.thinking);
  app.harness = gtk_check_button_new_with_label(_("Reasoning harness"));
  gtk_box_append(GTK_BOX(options), app.harness);
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
  app.reserve = gtk_button_new();
  gtk_box_append(GTK_BOX(box), app.reserve);
  g_signal_connect(app.reserve, "clicked", G_CALLBACK(reserve_clicked), NULL);
  app.roll = gtk_button_new();
  gtk_box_append(GTK_BOX(box), app.roll);
  g_signal_connect(app.roll, "clicked", G_CALLBACK(roll_clicked), NULL);
  app.status = gtk_label_new("");
  app.error = gtk_label_new("");
  gtk_label_set_wrap(GTK_LABEL(app.status), TRUE);
  gtk_label_set_wrap(GTK_LABEL(app.error), TRUE);
  gtk_box_append(GTK_BOX(box), ui_message_panel(app.status, app.error));
  app.rules = gtk_label_new(
      _("Race seven pieces to exit 15 with an exact roll. Route: private 1–4, "
        "shared 5–12, private 13–14. Rosettes 4, 8, 14 give another turn; 8 is "
        "safe. Capture opponents on other shared squares. Zero or blocked "
        "rolls must pass. Click a piece to move, or Reserve to enter."));
  gtk_label_set_wrap(GTK_LABEL(app.rules), TRUE);
  gtk_box_append(GTK_BOX(box), ui_fixed_text_panel(app.rules, 70));
  g_signal_connect(app.window, "close-request", G_CALLBACK(closing), NULL);
  restart(NULL, NULL);
  gtk_window_present(app.window);
}
#ifndef UR_UI_TEST
int main(int argc, char **argv) {
  g_autoptr(AdwApplication) application = adw_application_new(
      "io.github.aigames.RoyalGameOfUr", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(application, "activate", G_CALLBACK(activate), NULL);
  return g_application_run(G_APPLICATION(application), argc, argv);
}
#endif
