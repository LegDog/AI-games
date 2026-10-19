// SPDX-License-Identifier: GPL-3.0-or-later
#include "ollama-view.h"
#include <string.h>
typedef struct {
  gint refs;
  gboolean closed, following, appending;
  unsigned generation, ollama_kind;
  GtkWindow *parent, *window;
  GtkWidget *button, *follow, *copy, *text, *scroll;
  GtkWidget *controllers[2];
  GtkTextBuffer *buffer;
  OllamaTranslate translate;
  GtkCssProvider *css;
  GMainContext *context;
} View;
typedef struct {
  View *view;
  unsigned generation;
  char *actor, *model;
  unsigned turn;
} Binding;
typedef struct {
  View *view;
  unsigned generation;
  OllamaMessageKind kind;
  char *actor, *model, *id, *text;
  unsigned turn;
  gint64 timestamp;
} Delivery;
static const char *const roles[] = {"Sent", "Thinking", "Received", "Error"};
static const char *const tags[] = {"sent", "thinking", "received", "failure"};
static View *get_view(GtkWindow *parent) {
  return parent ? g_object_get_data(G_OBJECT(parent), "ollama-view") : NULL;
}
static View *view_ref(View *view) {
  g_atomic_int_inc(&view->refs);
  return view;
}
static void view_unref(gpointer data) {
  View *view = data;
  if (!g_atomic_int_dec_and_test(&view->refs))
    return;
  g_main_context_unref(view->context);
  g_free(view);
}
static const char *tr(View *view, const char *text) {
  return view->translate ? view->translate(text) : text;
}
static gboolean enabled(View *view) {
  for (unsigned i = 0; i < 2; i++)
    if (view->controllers[i] && gtk_drop_down_get_selected(GTK_DROP_DOWN(
                                    view->controllers[i])) == view->ollama_kind)
      return TRUE;
  return FALSE;
}
static void selection_changed(GObject *object, GParamSpec *spec,
                              gpointer data) {
  (void)object;
  (void)spec;
  View *view = data;
  if (view->closed)
    return;
  gtk_widget_set_visible(view->button, enabled(view));
}
static void set_follow(View *view, gboolean active) {
  view->following = active;
  if (view->follow)
    gtk_check_button_set_active(GTK_CHECK_BUTTON(view->follow), active);
}
static void scroll_bottom(View *view) {
  if (!view->scroll || !view->following || view->appending ||
      gtk_text_buffer_get_has_selection(view->buffer))
    return;
  view->appending = TRUE;
  GtkAdjustment *adjustment =
      gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(view->scroll));
  gtk_adjustment_set_value(adjustment,
                           MAX(gtk_adjustment_get_lower(adjustment),
                               gtk_adjustment_get_upper(adjustment) -
                                   gtk_adjustment_get_page_size(adjustment)));
  view->appending = FALSE;
}
static void layout_changed(GtkAdjustment *adjustment, gpointer data) {
  (void)adjustment;
  scroll_bottom(data);
}
static void follow_changed(GtkCheckButton *button, gpointer data) {
  View *view = data;
  view->following = gtk_check_button_get_active(button);
  scroll_bottom(view);
}
/* React to user input, not adjustment changes caused by GTK's own deferred
 * layout or animated scrolling. Otherwise a long reply disables its own follow.
 */
static gboolean user_scroll(GtkEventControllerScroll *controller, double dx,
                            double dy, gpointer data) {
  (void)controller;
  (void)dx;
  if (dy < 0)
    set_follow(data, FALSE);
  return FALSE;
}
static void scrollbar_pressed(GtkGestureClick *gesture, int count, double x,
                              double y, gpointer data) {
  (void)gesture;
  (void)count;
  (void)x;
  (void)y;
  set_follow(data, FALSE);
}
static gboolean user_key(GtkEventControllerKey *controller, guint key,
                         guint code, GdkModifierType state, gpointer data) {
  (void)controller;
  (void)code;
  (void)state;
  if (key == GDK_KEY_Up || key == GDK_KEY_Page_Up || key == GDK_KEY_Home)
    set_follow(data, FALSE);
  return FALSE;
}
static void selected(GObject *object, GParamSpec *spec, gpointer data) {
  (void)spec;
  View *view = data;
  if (gtk_text_buffer_get_has_selection(GTK_TEXT_BUFFER(object)))
    set_follow(view, FALSE);
}
static void copy_all(GtkButton *button, gpointer data) {
  (void)button;
  View *view = data;
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds(view->buffer, &start, &end);
  g_autofree char *text =
      gtk_text_buffer_get_text(view->buffer, &start, &end, FALSE);
  gdk_clipboard_set_text(gtk_widget_get_clipboard(view->text), text);
}
static gboolean hide_window(GtkWindow *window, gpointer data) {
  (void)data;
  gtk_widget_set_visible(GTK_WIDGET(window), FALSE);
  return TRUE;
}
static void ensure_window(View *view) {
  if (view->closed || view->window)
    return;
  view->window = GTK_WINDOW(
      adw_application_window_new(gtk_window_get_application(view->parent)));
  gtk_window_set_transient_for(view->window, view->parent);
  gtk_window_set_modal(view->window, FALSE);
  gtk_window_set_default_size(view->window, 900, 650);
  gtk_widget_set_size_request(GTK_WIDGET(view->window), 500, 350);
  g_signal_connect(view->window, "close-request", G_CALLBACK(hide_window),
                   view);
  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0),
            *header = adw_header_bar_new();
  gtk_box_append(GTK_BOX(root), header);
  view->copy = gtk_button_new();
  g_signal_connect(view->copy, "clicked", G_CALLBACK(copy_all), view);
  adw_header_bar_pack_end(ADW_HEADER_BAR(header), view->copy);
  view->follow = gtk_check_button_new();
  gtk_check_button_set_active(GTK_CHECK_BUTTON(view->follow), view->following);
  g_signal_connect(view->follow, "toggled", G_CALLBACK(follow_changed), view);
  adw_header_bar_pack_start(ADW_HEADER_BAR(header), view->follow);
  view->text = gtk_text_view_new_with_buffer(view->buffer);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(view->text), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view->text), GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_monospace(GTK_TEXT_VIEW(view->text), TRUE);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view->text), 20);
  gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view->text), 20);
  gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view->text), 16);
  gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view->text), 16);
  gtk_text_view_set_pixels_above_lines(GTK_TEXT_VIEW(view->text), 3);
  gtk_widget_add_css_class(view->text, "ollama-transcript");
  view->scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(view->scroll),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(view->scroll, TRUE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(view->scroll), view->text);
  gtk_box_append(GTK_BOX(root), view->scroll);
  g_signal_connect(
      gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(view->scroll)),
      "changed", G_CALLBACK(layout_changed), view);
  GtkEventController *wheel =
      gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  gtk_event_controller_set_name(wheel, "ollama-follow-scroll");
  gtk_event_controller_set_propagation_phase(wheel, GTK_PHASE_CAPTURE);
  g_signal_connect(wheel, "scroll", G_CALLBACK(user_scroll), view);
  gtk_widget_add_controller(view->scroll, wheel);
  GtkGesture *drag = gtk_gesture_click_new();
  gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(drag),
                                             GTK_PHASE_CAPTURE);
  g_signal_connect(drag, "pressed", G_CALLBACK(scrollbar_pressed), view);
  gtk_widget_add_controller(
      gtk_scrolled_window_get_vscrollbar(GTK_SCROLLED_WINDOW(view->scroll)),
      GTK_EVENT_CONTROLLER(drag));
  GtkEventController *keys = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
  g_signal_connect(keys, "key-pressed", G_CALLBACK(user_key), view);
  gtk_widget_add_controller(view->text, keys);
  g_signal_connect(view->buffer, "notify::has-selection", G_CALLBACK(selected),
                   view);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(view->window),
                                     root);
  ollama_view_retranslate(view->parent);
}
static void show_window(GtkButton *button, gpointer data) {
  (void)button;
  View *view = data;
  ensure_window(view);
  if (!view->closed)
    gtk_window_present(view->window);
}
static void shutdown_view(GtkWidget *parent, gpointer data) {
  (void)parent;
  View *view = data;
  if (view->closed)
    return;
  view->closed = TRUE;
  view->generation++;
  if (view->window)
    gtk_window_destroy(view->window);
  view->window = NULL;
  view->parent = NULL;
  view->text = NULL;
  view->follow = NULL;
  view->copy = NULL;
  g_clear_object(&view->buffer);
  if (view->css) {
    gtk_style_context_remove_provider_for_display(
        gtk_widget_get_display(parent), GTK_STYLE_PROVIDER(view->css));
    g_clear_object(&view->css);
  }
}
GtkWidget *ollama_view_install(GtkWindow *parent, GtkWidget *first,
                               GtkWidget *second, unsigned kind,
                               OllamaTranslate translate) {
  View *view = g_new0(View, 1);
  view->refs = 1;
  view->parent = parent;
  view->translate = translate;
  view->controllers[0] = first;
  view->controllers[1] = second;
  view->ollama_kind = kind;
  view->following = TRUE;
  view->context = g_main_context_ref(g_main_context_default());
  view->buffer = gtk_text_buffer_new(NULL);
  const char *colors[] = {"#93C5FD", "#FDE68A", "#86EFAC", "#FCA5A5"};
  for (unsigned i = 0; i < 4; i++)
    gtk_text_buffer_create_tag(view->buffer, tags[i], "foreground", colors[i],
                               NULL);
  gtk_text_buffer_create_tag(view->buffer, "heading", "weight",
                             PANGO_WEIGHT_BOLD, "scale", 1.1, NULL);
  view->css = gtk_css_provider_new();
  const char *style =
      "textview.ollama-transcript, textview.ollama-transcript text "
      "{background-color:#111827; color:#E5E7EB;} textview.ollama-transcript "
      "text selection {background-color:#1E40AF; color:#FFFFFF;}";
#if GTK_CHECK_VERSION(4, 12, 0)
  gtk_css_provider_load_from_string(view->css, style);
#else
  gtk_css_provider_load_from_data(view->css, style, -1);
#endif
  gtk_style_context_add_provider_for_display(
      gtk_widget_get_display(GTK_WIDGET(parent)), GTK_STYLE_PROVIDER(view->css),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  view->button = gtk_button_new_with_label(tr(view, "Ollama messages"));
  g_signal_connect(view->button, "clicked", G_CALLBACK(show_window), view);
  for (unsigned i = 0; i < 2; i++)
    if (view->controllers[i])
      g_signal_connect(view->controllers[i], "notify::selected",
                       G_CALLBACK(selection_changed), view);
  g_object_set_data_full(G_OBJECT(parent), "ollama-view", view, view_unref);
  g_signal_connect(parent, "destroy", G_CALLBACK(shutdown_view), view);
  selection_changed(NULL, NULL, view);
  return view->button;
}
void ollama_view_retranslate(GtkWindow *parent) {
  View *view = get_view(parent);
  if (!view || view->closed)
    return;
  gtk_button_set_label(GTK_BUTTON(view->button), tr(view, "Ollama messages"));
  if (view->window) {
    gtk_window_set_title(view->window, tr(view, "Ollama messages"));
    gtk_button_set_label(GTK_BUTTON(view->copy), tr(view, "Copy all"));
    gtk_check_button_set_label(GTK_CHECK_BUTTON(view->follow),
                               tr(view, "Follow latest"));
  }
}
void ollama_view_invalidate(GtkWindow *parent) {
  View *view = get_view(parent);
  if (view && !view->closed)
    view->generation++;
}
void ollama_view_clear(GtkWindow *parent) {
  View *view = get_view(parent);
  if (!view || view->closed)
    return;
  view->generation++;
  gtk_text_buffer_set_text(view->buffer, "", -1);
  set_follow(view, TRUE);
}
void ollama_view_begin(GtkWindow *parent) {
  ollama_view_clear(parent);
  View *view = get_view(parent);
  if (view && enabled(view))
    show_window(NULL, view);
}
void ollama_view_undo(GtkWindow *parent) {
  View *view = get_view(parent);
  if (!view || view->closed)
    return;
  view->generation++;
  GtkTextIter end;
  gtk_text_buffer_get_end_iter(view->buffer, &end);
  g_autofree char *text = g_strdup_printf("\n—— %s ——\n\n", tr(view, "Undo"));
  gtk_text_buffer_insert_with_tags_by_name(view->buffer, &end, text, -1,
                                           "heading", NULL);
}
static void delivery_free(gpointer data) {
  Delivery *delivery = data;
  view_unref(delivery->view);
  g_free(delivery->actor);
  g_free(delivery->model);
  g_free(delivery->id);
  g_free(delivery->text);
  g_free(delivery);
}
static gboolean deliver(gpointer data) {
  Delivery *d = data;
  View *view = d->view;
  if (view->closed || d->generation != view->generation)
    return G_SOURCE_REMOVE;
  GtkTextIter end;
  gtk_text_buffer_get_end_iter(view->buffer, &end);
  view->appending = TRUE;
  g_autoptr(GDateTime) when =
      g_date_time_new_from_unix_local(d->timestamp / G_USEC_PER_SEC);
  g_autofree char *time = g_date_time_format(when, "%H:%M:%S");
  g_autofree char *title = g_strdup_printf(
      "%s · %s · %s · %s %u · %s\n", tr(view, roles[d->kind]),
      tr(view, d->actor), d->model, tr(view, "Turn"), d->turn, time);
  gtk_text_buffer_insert_with_tags_by_name(view->buffer, &end, title, -1,
                                           tags[d->kind], "heading", NULL);
  gtk_text_buffer_insert_with_tags_by_name(view->buffer, &end, d->text, -1,
                                           tags[d->kind], NULL);
  gtk_text_buffer_insert(view->buffer, &end, "\n\n", -1);
  view->appending = FALSE;
  scroll_bottom(view);
  return G_SOURCE_REMOVE;
}
static void observed(OllamaMessageKind kind, const char *id, const char *text,
                     gpointer data) {
  Binding *binding = data;
  Delivery *d = g_new0(Delivery, 1);
  *d = (Delivery){.view = view_ref(binding->view),
                  .generation = binding->generation,
                  .kind = kind,
                  .actor = g_strdup(binding->actor),
                  .model = g_strdup(binding->model),
                  .id = g_strdup(id),
                  .text = g_strdup(text),
                  .turn = binding->turn,
                  .timestamp = g_get_real_time()};
  /* An idle source, unlike invoke(), never calls GTK inline on a worker. */
  GSource *source = g_idle_source_new();
  g_source_set_callback(source, deliver, d, delivery_free);
  g_source_attach(source, binding->view->context);
  g_source_unref(source);
}
static void binding_free(gpointer data) {
  Binding *binding = data;
  view_unref(binding->view);
  g_free(binding->actor);
  g_free(binding->model);
  g_free(binding);
}
void ollama_view_bind(GtkWindow *parent, GCancellable *operation,
                      const char *actor, unsigned turn, const char *model) {
  View *view = get_view(parent);
  if (!view || view->closed)
    return;
  Binding *binding = g_new0(Binding, 1);
  *binding = (Binding){view_ref(view), view->generation, g_strdup(actor),
                       g_strdup(model ? model : ""), turn};
  ollama_observe(operation, observed, binding, binding_free);
}
