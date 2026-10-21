// SPDX-License-Identifier: GPL-3.0-or-later
#include "ollama-harness.h"
#include "ollama-view.h"
#include <math.h>
#include <string.h>
typedef struct {
  GtkApplication *app;
  GtkWindow *parent;
  GtkWidget *first, *second, *button, *board;
} Fixture;
static void pump(void) {
  gint64 end = g_get_monotonic_time() + 100000;
  do {
    while (g_main_context_iteration(NULL, FALSE)) {
    }
    g_usleep(1000);
  } while (g_get_monotonic_time() < end);
}
static GtkWidget *find(GtkWidget *root, GType type, const char *label) {
  if (g_type_is_a(G_OBJECT_TYPE(root), type) &&
      (!label ||
       (GTK_IS_BUTTON(root) &&
        g_strcmp0(gtk_button_get_label(GTK_BUTTON(root)), label) == 0)))
    return root;
  for (GtkWidget *child = gtk_widget_get_first_child(root); child;
       child = gtk_widget_get_next_sibling(child)) {
    GtkWidget *result = find(child, type, label);
    if (result)
      return result;
  }
  return NULL;
}
static GtkWindow *secondary(Fixture *f) {
  for (GList *at = gtk_application_get_windows(f->app); at; at = at->next)
    if (at->data != f->parent)
      return at->data;
  return NULL;
}
static void setup(Fixture *f, gconstpointer data) {
  (void)data;
  static unsigned number;
  g_autofree char *id =
      g_strdup_printf("io.github.aigames.OllamaViewer.Test%u", ++number);
  f->app = GTK_APPLICATION(adw_application_new(id, G_APPLICATION_NON_UNIQUE));
  g_assert_true(g_application_register(G_APPLICATION(f->app), NULL, NULL));
  f->parent = GTK_WINDOW(adw_application_window_new(f->app));
  gtk_window_set_default_size(f->parent, 700, 600);
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  const char *names[] = {"Human", "Ollama", NULL};
  f->first = gtk_drop_down_new_from_strings(names);
  f->second = gtk_drop_down_new_from_strings(names);
  gtk_box_append(GTK_BOX(box), f->first);
  gtk_box_append(GTK_BOX(box), f->second);
  f->button = ollama_view_install(f->parent, f->first, f->second, 1, NULL);
  gtk_box_append(GTK_BOX(box), f->button);
  f->board = gtk_drawing_area_new();
  gtk_widget_set_vexpand(f->board, TRUE);
  gtk_box_append(GTK_BOX(box), f->board);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(f->parent), box);
  gtk_window_present(f->parent);
  pump();
}
static void teardown(Fixture *f, gconstpointer data) {
  (void)data;
  if (f->parent)
    gtk_window_destroy(f->parent);
  pump();
  g_assert_null(gtk_application_get_windows(f->app));
  g_object_unref(f->app);
}
static char *contents(GtkWindow *window) {
  GtkWidget *text = find(GTK_WIDGET(window), GTK_TYPE_TEXT_VIEW, NULL);
  g_assert_nonnull(text);
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));
  GtkTextIter a, b;
  gtk_text_buffer_get_bounds(buffer, &a, &b);
  return gtk_text_buffer_get_text(buffer, &a, &b, FALSE);
}
static void emit_reply(GCancellable *operation, const char *json) {
  GBytes *bytes = g_bytes_new(json, strlen(json));
  ollama_event_response(operation, "request-1", "/api/generate", FALSE, bytes);
  g_bytes_unref(bytes);
}
static gpointer worker(gpointer data) {
  GCancellable *operation = data;
  g_autoptr(JsonParser) parser = json_parser_new();
  g_assert_true(json_parser_load_from_data(
      parser,
      "{\"prompt\":\"Play the current board.\\nChoose a legal "
      "move.\",\"stream\":false,\"format\":{\"type\":\"object\"}}",
      -1, NULL));
  ollama_event_request(operation, "request-1", "/api/generate",
                       json_node_get_object(json_parser_get_root(parser)));
  emit_reply(operation, "{\"thinking\":\"Check the winning lines before "
                        "choosing.\",\"response\":\"{\\\"move\\\":\\\"c4:A:"
                        "cw\\\"}\",\"done\":true}");
  return NULL;
}
static char *copied;
static gboolean copy_done;
static void clipboard_done(GObject *source, GAsyncResult *result,
                           gpointer data) {
  (void)data;
  copied = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), result, NULL);
  copy_done = TRUE;
}
static void assert_clipboard(GtkWidget *text, const char *expected) {
  copy_done = FALSE;
  gdk_clipboard_read_text_async(gtk_widget_get_clipboard(text), NULL,
                                clipboard_done, NULL);
  gint64 end = g_get_monotonic_time() + 2000000;
  while (!copy_done && g_get_monotonic_time() < end)
    pump();
  g_assert_true(copy_done);
  g_assert_cmpstr(copied, ==, expected);
  g_clear_pointer(&copied, g_free);
}
static void maybe_screenshot(GtkWindow *window) {
  const char *path = g_getenv("OLLAMA_VIEW_SCREENSHOT");
  if (!path)
    return;
  g_autofree char *directory = g_path_get_dirname(path);
  g_assert_cmpint(g_mkdir_with_parents(directory, 0755), ==, 0);
  g_autoptr(GdkPaintable) paintable =
      gtk_widget_paintable_new(GTK_WIDGET(window));
  gtk_widget_queue_draw(GTK_WIDGET(window));
  pump();
  int width = gtk_widget_get_width(GTK_WIDGET(window)),
      height = gtk_widget_get_height(GTK_WIDGET(window));
  GtkSnapshot *snapshot = gtk_snapshot_new();
  gdk_paintable_snapshot(paintable, snapshot, width, height);
  GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
  g_assert_nonnull(node);
  graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, width, height);
  g_autoptr(GdkTexture) texture = gsk_renderer_render_texture(
      gtk_native_get_renderer(GTK_NATIVE(window)), node, &bounds);
  gsk_render_node_unref(node);
  g_assert_true(gdk_texture_save_to_png(texture, path));
}
static void lifecycle(Fixture *f, gconstpointer data) {
  (void)data;
  g_assert_false(gtk_widget_get_visible(f->button));
  ollama_view_begin(f->parent);
  g_assert_null(secondary(f));
  gtk_drop_down_set_selected(GTK_DROP_DOWN(f->second), 1);
  pump();
  g_assert_true(gtk_widget_get_visible(f->button));
  graphene_rect_t before, after;
  g_assert_true(
      gtk_widget_compute_bounds(f->board, GTK_WIDGET(f->parent), &before));
  ollama_view_begin(f->parent);
  pump();
  GtkWindow *window = secondary(f);
  g_assert_nonnull(window);
  g_assert_true(gtk_widget_get_visible(GTK_WIDGET(window)));
  g_assert_false(gtk_window_get_modal(window));
  g_autoptr(GCancellable) operation = g_cancellable_new();
  ollama_view_bind(f->parent, operation, "White", 2, "mock:1b");
  GThread *thread = g_thread_new("observer-worker", worker, operation);
  g_thread_join(thread);
  pump();
  g_autofree char *text = contents(window);
  g_assert_nonnull(strstr(text, "Sent · White · mock:1b · Turn 2"));
  g_assert_nonnull(strstr(text, "Thinking · White"));
  g_assert_nonnull(strstr(text, "Received · White"));
  g_assert_nonnull(strstr(text, "\n  \"move\""));
  g_assert_nonnull(strstr(text, "Play the current board.\nChoose"));
  g_assert_true(
      gtk_widget_compute_bounds(f->board, GTK_WIDGET(f->parent), &after));
  g_assert_true(graphene_rect_equal(&before, &after));
  GtkWidget *view = find(GTK_WIDGET(window), GTK_TYPE_TEXT_VIEW, NULL);
  g_assert_false(gtk_text_view_get_editable(GTK_TEXT_VIEW(view)));
  GtkWidget *copy = find(GTK_WIDGET(window), GTK_TYPE_BUTTON, "Copy all");
  g_assert_nonnull(copy);
  g_signal_emit_by_name(copy, "clicked");
  assert_clipboard(view, text);
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
  GtkTextIter a, b;
  gtk_text_buffer_get_bounds(buffer, &a, &b);
  gtk_text_buffer_select_range(buffer, &a, &b);
  g_signal_emit_by_name(view, "copy-clipboard");
  assert_clipboard(view, text);
  GtkWidget *follow = find(GTK_WIDGET(window), GTK_TYPE_CHECK_BUTTON, NULL);
  g_assert_false(gtk_check_button_get_active(GTK_CHECK_BUTTON(follow)));
  gtk_text_buffer_place_cursor(buffer, &b);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(follow), TRUE);
  pump();
  maybe_screenshot(window);
  gtk_window_close(window);
  pump();
  g_assert_false(gtk_widget_get_visible(GTK_WIDGET(window)));
  g_signal_emit_by_name(f->button, "clicked");
  pump();
  g_assert_true(gtk_widget_get_visible(GTK_WIDGET(window)));
  g_autofree char *reopened = contents(window);
  g_assert_cmpstr(text, ==, reopened);
  emit_reply(operation, "{\"response\":\"queued stale reply\"}");
  ollama_view_clear(f->parent);
  pump();
  g_autofree char *empty = contents(window);
  g_assert_cmpstr(empty, ==, "");
  ollama_view_bind(f->parent, operation, "Black", 1, "other:1b");
  emit_reply(operation, "{\"response\":\"current reply\"}");
  pump();
  ollama_view_undo(f->parent);
  emit_reply(operation, "{\"response\":\"undone late reply\"}");
  pump();
  g_autofree char *undone = contents(window);
  g_assert_nonnull(strstr(undone, "current reply"));
  g_assert_nonnull(strstr(undone, "Undo"));
  g_assert_null(strstr(undone, "undone late reply"));
  ollama_view_bind(f->parent, operation, "White", 3, "mock:1b");
  emit_reply(operation, "{\"response\":\"after destroy\"}");
  gtk_window_destroy(f->parent);
  f->parent = NULL;
  pump();
  g_assert_null(gtk_application_get_windows(f->app));
}
static void scrolling(Fixture *f, gconstpointer data) {
  (void)data;
  gtk_drop_down_set_selected(GTK_DROP_DOWN(f->first), 1);
  ollama_view_begin(f->parent);
  pump();
  GtkWindow *window = secondary(f);
  g_autoptr(GCancellable) operation = g_cancellable_new();
  ollama_view_bind(f->parent, operation, "Black", 1, "test:1b");
  GString *large = g_string_new(NULL);
  for (unsigned i = 0; i < 150; i++)
    g_string_append(large, "A complete and readable line of model output.\n");
  JsonObject *response = json_object_new();
  json_object_set_string_member(response, "response", large->str);
  JsonNode *node = json_node_new(JSON_NODE_OBJECT);
  json_node_take_object(node, response);
  g_autofree char *json = json_to_string(node, FALSE);
  json_node_free(node);
  g_string_free(large, TRUE);
  emit_reply(operation, json);
  pump();
  pump();
  pump();
  GtkWidget *follow = find(GTK_WIDGET(window), GTK_TYPE_CHECK_BUTTON, NULL);
  g_assert_true(gtk_check_button_get_active(GTK_CHECK_BUTTON(follow)));
  GtkWidget *scroll = find(GTK_WIDGET(window), GTK_TYPE_SCROLLED_WINDOW, NULL);
  GtkAdjustment *adjustment =
      gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
  g_assert_cmpfloat(gtk_adjustment_get_upper(adjustment), >,
                    gtk_adjustment_get_page_size(adjustment));
  GtkTextView *text_view =
      GTK_TEXT_VIEW(find(GTK_WIDGET(window), GTK_TYPE_TEXT_VIEW, NULL));
  GtkTextIter last;
  gtk_text_buffer_get_end_iter(gtk_text_view_get_buffer(text_view), &last);
  GdkRectangle location, visible;
  gtk_text_view_get_iter_location(text_view, &last, &location);
  gtk_text_view_get_visible_rect(text_view, &visible);
  g_assert_cmpint(location.y, >=, visible.y);
  g_assert_cmpint(location.y + location.height, <=, visible.y + visible.height);
  g_autoptr(GListModel) controllers = gtk_widget_observe_controllers(scroll);
  gboolean emitted = FALSE;
  for (unsigned i = 0; i < g_list_model_get_n_items(controllers); i++) {
    g_autoptr(GtkEventController) controller =
        g_list_model_get_item(controllers, i);
    if (g_strcmp0(gtk_event_controller_get_name(controller),
                  "ollama-follow-scroll") == 0) {
      gboolean handled = FALSE;
      g_signal_emit_by_name(controller, "scroll", 0., -1., &handled);
      emitted = TRUE;
    }
  }
  g_assert_true(emitted);
  gtk_adjustment_set_value(adjustment, 0);
  pump();
  g_assert_false(gtk_check_button_get_active(GTK_CHECK_BUTTON(follow)));
  emit_reply(operation, "{\"response\":\"new reply while reading\"}");
  pump();
  g_assert_cmpfloat(gtk_adjustment_get_value(adjustment), ==, 0);
}
static double luminance(GdkRGBA c) {
  double v[] = {c.red, c.green, c.blue}, out = 0,
         weight[] = {.2126, .7152, .0722};
  for (unsigned i = 0; i < 3; i++)
    out += weight[i] *
           (v[i] <= .04045 ? v[i] / 12.92 : pow((v[i] + .055) / 1.055, 2.4));
  return out;
}
static void contrast(Fixture *f, gconstpointer data) {
  (void)data;
  gtk_drop_down_set_selected(GTK_DROP_DOWN(f->first), 1);
  ollama_view_begin(f->parent);
  pump();
  GtkWidget *view = find(GTK_WIDGET(secondary(f)), GTK_TYPE_TEXT_VIEW, NULL);
  GtkTextTagTable *table = gtk_text_buffer_get_tag_table(
      gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)));
  const char *names[] = {"sent", "thinking", "received", "failure"};
  GdkRGBA bg;
  gdk_rgba_parse(&bg, "#111827");
  for (unsigned i = 0; i < 4; i++) {
    GdkRGBA *color = NULL;
    g_object_get(gtk_text_tag_table_lookup(table, names[i]), "foreground-rgba",
                 &color, NULL);
    g_assert_nonnull(color);
    g_assert_cmpfloat((luminance(*color) + .05) / (luminance(bg) + .05), >=, 7);
    gdk_rgba_free(color);
  }
}
static void harness_stages(Fixture *f, gconstpointer data) {
  (void)data;
  gtk_drop_down_set_selected(GTK_DROP_DOWN(f->first), 1);
  ollama_view_begin(f->parent);
  g_autoptr(GCancellable) operation = g_cancellable_new();
  ollama_view_bind(f->parent, operation, "Black", 1, "mock:1b");
  OllamaRunStats stats;
  g_auto(OllamaRun) run = {0};
  ollama_run_begin(&run, &stats, TRUE);
  g_autoptr(JsonObject) body = json_object_new();
  json_object_set_string_member(body, "prompt",
                                "Original board. Choose a legal move.");
  run.stage = "Propose";
  ollama_event_request(operation, "proposal", "/api/generate", body);
  emit_reply(operation,
             "{\"thinking\":\"Check immediate threats.\",\"response\":\"I "
             "propose the center.\",\"done\":true}");
  run.stage = "Review";
  ollama_event_request(operation, "review", "/api/generate", body);
  emit_reply(operation,
             "{\"response\":\"{\\\"move\\\":\\\"h8\\\"}\",\"done\":true}");
  pump();
  GtkWindow *window = secondary(f);
  g_assert_nonnull(window);
  g_autofree char *text = contents(window);
  g_assert_nonnull(strstr(text, "Propose\n"));
  g_assert_nonnull(strstr(text, "Review\n"));
  g_assert_nonnull(strstr(text, "Received · Black"));
  g_assert_nonnull(strstr(text, "Thinking · Black"));
  GtkWidget *copy = find(GTK_WIDGET(window), GTK_TYPE_BUTTON, "Copy all");
  g_signal_emit_by_name(copy, "clicked");
  assert_clipboard(find(GTK_WIDGET(window), GTK_TYPE_TEXT_VIEW, NULL), text);
  maybe_screenshot(window);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  gtk_init();
  adw_init();
  g_test_add("/ollama-view/lifecycle-worker-copy-stale", Fixture, NULL, setup,
             lifecycle, teardown);
  g_test_add("/ollama-view/follow-and-reading", Fixture, NULL, setup, scrolling,
             teardown);
  g_test_add("/ollama-view/contrast", Fixture, NULL, setup, contrast, teardown);
  g_test_add("/ollama-view/harness-stages", Fixture, NULL, setup,
             harness_stages, teardown);
  return g_test_run();
}
