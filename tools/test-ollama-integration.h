// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <gtk/gtk.h>
static GtkTextView *test_find_transcript(GtkWidget *widget) {
  if (GTK_IS_TEXT_VIEW(widget) &&
      gtk_widget_has_css_class(widget, "ollama-transcript"))
    return GTK_TEXT_VIEW(widget);
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
       child = gtk_widget_get_next_sibling(child)) {
    GtkTextView *found = test_find_transcript(child);
    if (found)
      return found;
  }
  return NULL;
}
static void ui_assert_ollama_transcript(GtkWidget *board) {
  gint64 end = g_get_monotonic_time() + 100000;
  do {
    while (g_main_context_iteration(NULL, FALSE)) {
    }
    g_usleep(1000);
  } while (g_get_monotonic_time() < end);
  GtkWindow *parent = GTK_WINDOW(gtk_widget_get_root(board));
  GtkApplication *application = gtk_window_get_application(parent);
  GtkTextView *transcript = NULL;
  for (GList *at = gtk_application_get_windows(application); at;
       at = at->next) {
    if (gtk_window_get_transient_for(at->data) == parent) {
      transcript = test_find_transcript(GTK_WIDGET(at->data));
      if (transcript) {
        g_assert_true(gtk_widget_get_visible(GTK_WIDGET(at->data)));
        break;
      }
    }
  }
  g_assert_nonnull(transcript);
  g_assert_cmpint(
      gtk_text_buffer_get_char_count(gtk_text_view_get_buffer(transcript)), >,
      0);
}
