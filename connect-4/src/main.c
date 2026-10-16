#include <adwaita.h>
#include <math.h>

#include "game.h"

typedef struct {
    GtkApplication *application;
    GtkWindow *window;
    GtkWidget *stack, *board, *status, *scale, *error_box, *error_label;
    GtkDropDown *humans, *side;
    Game game;
    GRand *random;
    GString *history;
    GQueue pending_reports;
    char *history_path;
    gboolean human_red, human_yellow, playing;
    guint source;
    double delay;
} App;

static void schedule_turn(App *app);
static void start_game(App *app);

static gboolean human_turn(const App *app)
{
    return app->game.turn == 1 ? app->human_red : app->human_yellow;
}

static void cancel_timer(App *app)
{
    if (app->source) {
        g_source_remove(app->source);
        app->source = 0;
    }
}

static void flush_reports(App *app)
{
    while (!g_queue_is_empty(&app->pending_reports)) {
        GError *error = NULL;
        if (!game_append_report(app->history_path, g_queue_peek_head(&app->pending_reports), &error)) {
            char *message = g_strdup_printf("%s\n%u partida(s) aguardando gravação nesta sessão.",
                                            error->message, app->pending_reports.length);
            gtk_label_set_text(GTK_LABEL(app->error_label), message);
            gtk_widget_set_visible(app->error_box, TRUE);
            g_free(message);
            g_clear_error(&error);
            return;
        }
        g_free(g_queue_pop_head(&app->pending_reports));
    }
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

static void play_column(App *app, unsigned cell)
{
    if (!app->playing || game_result(&app->game) || cell >= GAME_COLS ||
        (human_turn(app) && game_drop_row(&app->game, cell) < 0))
        return;
    Score scores[GAME_COLS];
    game_evaluate(&app->game, app->random, scores);
    if (!human_turn(app))
        cell = (unsigned) game_best_move(scores);
    game_record(app->history, &app->game, scores, cell, human_turn(app));
    game_play(&app->game, cell);
    gtk_widget_queue_draw(app->board);

    int result = game_result(&app->game);
    if (!result) {
        schedule_turn(app);
        return;
    }
    char *message = result == 2 ? g_strdup("Empate! Nova partida em 3 segundos.") :
        g_strdup_printf("%s venceu! Nova partida em 3 segundos.", game_player_name(result));
    gtk_label_set_text(GTK_LABEL(app->status), message);
    g_free(message);
    if (result != 2) {
        GDateTime *now = g_date_time_new_now_local();
        char *configuration = g_strdup_printf("Vermelho: %s; Amarelo: %s. Delay ao encerrar: %.1f s.",
            app->human_red ? "humano" : "automático", app->human_yellow ? "humano" : "automático",
            app->delay);
        g_queue_push_tail(&app->pending_reports,
                         game_report(&app->game, app->history->str, configuration, now));
        g_free(configuration);
        g_date_time_unref(now);
        flush_reports(app);
    }
    app->source = g_timeout_add(3000, restart_game, app);
}

static gboolean automatic_move(gpointer data)
{
    App *app = data;
    app->source = 0;
    if (app->playing && !human_turn(app) && !game_result(&app->game))
        play_column(app, 0);
    return G_SOURCE_REMOVE;
}

static void schedule_turn(App *app)
{
    cancel_timer(app);
    char *message = g_strdup_printf("Vez de %s · %s", game_player_name(app->game.turn),
                                    human_turn(app) ? "humano" : "automático");
    gtk_label_set_text(GTK_LABEL(app->status), message);
    g_free(message);
    if (!human_turn(app))
        app->source = app->delay == 0.0 ? g_idle_add(automatic_move, app) :
            g_timeout_add((guint) lround(app->delay * 1000), automatic_move, app);
}

static void start_game(App *app)
{
    cancel_timer(app);
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
    guint humans = gtk_drop_down_get_selected(app->humans);
    app->human_red = humans == 2 || (humans == 1 && gtk_drop_down_get_selected(app->side) == 0);
    app->human_yellow = humans == 2 || (humans == 1 && !app->human_red);
    start_game(app);
}

static void humans_changed(GObject *object, GParamSpec *pspec, gpointer data)
{
    (void) object;
    (void) pspec;
    App *app = data;
    gtk_widget_set_sensitive(GTK_WIDGET(app->side), gtk_drop_down_get_selected(app->humans) == 1);
}

static void configure_clicked(GtkButton *button, gpointer data)
{
    (void) button;
    App *app = data;
    cancel_timer(app);
    app->playing = FALSE;
    g_string_truncate(app->history, 0);
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "configuration");
}

static void delay_changed(GtkRange *range, gpointer data)
{
    App *app = data;
    app->delay = round(gtk_range_get_value(range) * 10) / 10;
    if (app->playing && !game_result(&app->game) && !human_turn(app))
        schedule_turn(app);
}

typedef struct {
    double unit, left, top;
} BoardGeometry;

static BoardGeometry board_geometry(double width, double height)
{
    double unit = MIN(width / GAME_COLS, height / (GAME_ROWS + .45));
    return (BoardGeometry) {unit, (width - GAME_COLS * unit) / 2,
                           (height - (GAME_ROWS + .45) * unit) / 2 + .45 * unit};
}

static void draw_board(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer data)
{
    App *app = data;
    BoardGeometry b = board_geometry(width, height);
    if (b.unit <= 0)
        return;
    GdkRGBA foreground;
    gtk_widget_get_color(GTK_WIDGET(area), &foreground);
    cairo_save(cr);
    cairo_translate(cr, b.left, b.top);
    cairo_set_source_rgba(cr, foreground.red, foreground.green, foreground.blue, foreground.alpha);
    cairo_set_font_size(cr, b.unit * .25);
    for (unsigned col = 0; col < GAME_COLS; col++) {
        char label[] = {(char) ('1' + col), '\0'};
        cairo_text_extents_t extents;
        cairo_text_extents(cr, label, &extents);
        cairo_move_to(cr, (col + .5) * b.unit - extents.width / 2 - extents.x_bearing, -.13 * b.unit);
        cairo_show_text(cr, label);
    }
    cairo_new_path(cr);
    cairo_set_source_rgb(cr, .10, .28, .65);
    cairo_rectangle(cr, 0, 0, GAME_COLS * b.unit, GAME_ROWS * b.unit);
    cairo_fill(cr);
    for (unsigned row = 0; row < GAME_ROWS; row++) {
        for (unsigned col = 0; col < GAME_COLS; col++) {
            int piece = app->game.cells[row][col];
            double x = (col + .5) * b.unit, y = (row + .5) * b.unit;
            if (piece == 1)
                cairo_set_source_rgb(cr, .92, .18, .23);
            else if (piece == -1)
                cairo_set_source_rgb(cr, 1, .80, .12);
            else
                cairo_set_source_rgb(cr, .08, .12, .21);
            cairo_new_sub_path(cr);
            cairo_arc(cr, x, y, .40 * b.unit, 0, 2 * G_PI);
            cairo_fill(cr);
            if (!piece)
                continue;
            cairo_set_source_rgba(cr, 0, 0, 0, .65);
            cairo_set_line_width(cr, MAX(1.0, .035 * b.unit));
            if (piece == 1) {
                cairo_move_to(cr, x - .13 * b.unit, y);
                cairo_line_to(cr, x + .13 * b.unit, y);
                cairo_move_to(cr, x, y - .13 * b.unit);
                cairo_line_to(cr, x, y + .13 * b.unit);
            } else {
                cairo_new_sub_path(cr);
                cairo_arc(cr, x, y, .15 * b.unit, 0, 2 * G_PI);
            }
            cairo_stroke(cr);
        }
    }
    WinningLine lines[GAME_MAX_LINES];
    unsigned count = game_winning_lines(&app->game, lines);
    for (unsigned i = 0; i < count; i++) {
        WinningLine line = lines[i];
        double x1 = (line.col1 + .5) * b.unit, y1 = (line.row1 + .5) * b.unit;
        double x2 = (line.col2 + .5) * b.unit, y2 = (line.row2 + .5) * b.unit;
        double dx = x2 == x1 ? 0 : copysign(.30 * b.unit, x2 - x1);
        double dy = y2 == y1 ? 0 : copysign(.30 * b.unit, y2 - y1);
        cairo_new_path(cr);
        cairo_move_to(cr, x1 - dx, y1 - dy);
        cairo_line_to(cr, x2 + dx, y2 + dy);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_source_rgb(cr, .02, .15, .07);
        cairo_set_line_width(cr, MAX(4.0, b.unit * .13));
        cairo_stroke_preserve(cr);
        cairo_set_source_rgb(cr, .25, 1, .48);
        cairo_set_line_width(cr, MAX(2.0, b.unit * .07));
        cairo_stroke(cr);
    }
    cairo_restore(cr);
}

static void board_clicked(GtkGestureClick *gesture, int presses, double x, double y, gpointer data)
{
    (void) gesture;
    (void) presses;
    App *app = data;
    if (!app->playing || !human_turn(app) || game_result(&app->game))
        return;
    BoardGeometry b = board_geometry(gtk_widget_get_width(app->board), gtk_widget_get_height(app->board));
    x -= b.left;
    y -= b.top;
    if (b.unit <= 0 || x < 0 || y < 0 || x >= GAME_COLS * b.unit || y >= GAME_ROWS * b.unit)
        return;
    gtk_widget_grab_focus(app->board);
    play_column(app, (unsigned) (x / b.unit));
}

static gboolean key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode,
                            GdkModifierType state, gpointer data)
{
    (void) controller;
    (void) keycode;
    App *app = data;
    if (!app->playing || !human_turn(app) || game_result(&app->game) ||
        (state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)))
        return FALSE;
    int number = 0;
    if (keyval >= GDK_KEY_1 && keyval <= GDK_KEY_7)
        number = keyval - GDK_KEY_0;
    else if (keyval >= GDK_KEY_KP_1 && keyval <= GDK_KEY_KP_7)
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
        default: return FALSE;
        }
    }
    play_column(app, (unsigned) (number - 1));
    return TRUE;
}

static void set_margins(GtkWidget *widget, int margin)
{
    gtk_widget_set_margin_start(widget, margin);
    gtk_widget_set_margin_end(widget, margin);
    gtk_widget_set_margin_top(widget, margin);
    gtk_widget_set_margin_bottom(widget, margin);
}

static GtkWidget *make_configuration(App *app)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    set_margins(box, 24);
    GtkWidget *title = gtk_label_new("Lig-4");
    gtk_widget_add_css_class(title, "title-1");
    gtk_box_append(GTK_BOX(box), title);
    gtk_box_append(GTK_BOX(box), gtk_label_new("Quantas pessoas vão jogar?"));
    const char *players[] = {"0 — dois jogadores automáticos", "1 — humano e computador", "2 — dois humanos", NULL};
    app->humans = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(players));
    gtk_drop_down_set_selected(app->humans, 1);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(app->humans));
    gtk_box_append(GTK_BOX(box), gtk_label_new("No modo com um humano, jogar como:"));
    const char *sides[] = {"Vermelho — joga primeiro", "Amarelo — joga depois", NULL};
    app->side = GTK_DROP_DOWN(gtk_drop_down_new_from_strings(sides));
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(app->side));
    g_signal_connect(app->humans, "notify::selected", G_CALLBACK(humans_changed), app);
    GtkWidget *button = gtk_button_new_with_label("Iniciar partida");
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
    GtkWidget *configure = gtk_button_new_with_label("Nova configuração");
    g_signal_connect(configure, "clicked", G_CALLBACK(configure_clicked), app);
    gtk_box_append(GTK_BOX(controls), configure);
    gtk_box_append(GTK_BOX(box), controls);

    GtkWidget *delay_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(delay_box, 12);
    gtk_widget_set_margin_end(delay_box, 12);
    gtk_box_append(GTK_BOX(delay_box), gtk_label_new("Delay (s)"));
    app->scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 3, .1);
    gtk_scale_set_digits(GTK_SCALE(app->scale), 1);
    gtk_scale_set_draw_value(GTK_SCALE(app->scale), TRUE);
    gtk_scale_set_value_pos(GTK_SCALE(app->scale), GTK_POS_RIGHT);
    gtk_range_set_round_digits(GTK_RANGE(app->scale), 1);
    gtk_range_set_value(GTK_RANGE(app->scale), app->delay);
    gtk_widget_set_hexpand(app->scale, TRUE);
    gtk_widget_set_tooltip_text(app->scale, "Espera antes de cada jogada automática, em segundos");
    g_signal_connect(app->scale, "value-changed", G_CALLBACK(delay_changed), app);
    gtk_box_append(GTK_BOX(delay_box), app->scale);
    gtk_box_append(GTK_BOX(box), delay_box);

    app->board = gtk_drawing_area_new();
    gtk_widget_set_hexpand(app->board, TRUE);
    gtk_widget_set_vexpand(app->board, TRUE);
    gtk_widget_set_focusable(app->board, TRUE);
    gtk_widget_set_tooltip_text(app->board, "Clique na coluna ou use as teclas 1–7");
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
    cancel_timer(app);
    app->playing = FALSE;
    app->window = NULL;
}

static void activate(GtkApplication *application, gpointer data)
{
    App *app = data;
    if (app->window) {
        gtk_window_present(app->window);
        return;
    }
    app->window = GTK_WINDOW(adw_application_window_new(application));
    gtk_window_set_title(app->window, "Lig-4");
    gtk_window_set_default_size(app->window, 840, 820);
    g_signal_connect(app->window, "destroy", G_CALLBACK(window_destroyed), app);
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(root), adw_header_bar_new());

    app->error_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    set_margins(app->error_box, 12);
    gtk_widget_add_css_class(app->error_box, "error");
    app->error_label = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(app->error_label), TRUE);
    gtk_widget_set_hexpand(app->error_label, TRUE);
    gtk_box_append(GTK_BOX(app->error_box), app->error_label);
    GtkWidget *retry = gtk_button_new_with_label("Tentar gravar");
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
    app.delay = .5;
    app.random = g_rand_new();
    app.history = g_string_new(NULL);
    char *cwd = g_get_current_dir();
    app.history_path = g_build_filename(cwd, "histórico-connect-4.md", NULL);
    g_free(cwd);
    app.application = GTK_APPLICATION(adw_application_new("io.github.aigames.Connect4",
                                                        G_APPLICATION_NON_UNIQUE));
    g_signal_connect(app.application, "activate", G_CALLBACK(activate), &app);
    int result = g_application_run(G_APPLICATION(app.application), argc, argv);
    cancel_timer(&app);
    g_object_unref(app.application);
    g_queue_clear_full(&app.pending_reports, g_free);
    g_string_free(app.history, TRUE);
    g_rand_free(app.random);
    g_free(app.history_path);
    return result;
}
