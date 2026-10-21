// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ollama-controls.h"
#include "test-ui-panels.h"
static unsigned ui_test_harness_walk(GtkWidget *widget) {
  unsigned count = 0;
  GtkWidget *check = g_object_get_data(G_OBJECT(widget), "ollama-harness");
  if (check) {
    count++;
    g_assert_false(gtk_check_button_get_active(GTK_CHECK_BUTTON(check)));
    GtkWidget *assist = g_object_get_data(G_OBJECT(check), "ollama-assistance");
    gboolean assisted =
        assist && gtk_check_button_get_active(GTK_CHECK_BUTTON(assist));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check), TRUE);
    g_assert_true(ui_ollama_harness_active(widget));
    if (assist) {
      g_assert_false(gtk_check_button_get_active(GTK_CHECK_BUTTON(assist)));
      gtk_check_button_set_active(GTK_CHECK_BUTTON(assist), TRUE);
      g_assert_false(ui_ollama_harness_active(widget));
      gtk_check_button_set_active(GTK_CHECK_BUTTON(check), TRUE);
      g_assert_false(gtk_check_button_get_active(GTK_CHECK_BUTTON(assist)));
    }
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check), FALSE);
    if (assist)
      gtk_check_button_set_active(GTK_CHECK_BUTTON(assist), assisted);
    gboolean visible = gtk_widget_get_visible(widget),
             sensitive = gtk_widget_get_sensitive(widget);
    gtk_widget_set_visible(widget, FALSE);
    g_assert_false(gtk_widget_get_visible(check));
    gtk_widget_set_visible(widget, TRUE);
    g_assert_true(gtk_widget_get_visible(check));
    gtk_widget_set_visible(widget, visible);
    gtk_widget_set_sensitive(widget, FALSE);
    g_assert_false(gtk_widget_get_sensitive(check));
    gtk_widget_set_sensitive(widget, TRUE);
    g_assert_true(gtk_widget_get_sensitive(check));
    gtk_widget_set_sensitive(widget, sensitive);
  }
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
       child = gtk_widget_get_next_sibling(child))
    count += ui_test_harness_walk(child);
  return count;
}
static void ui_assert_ollama_controls(GtkWidget *board) {
  g_assert_cmpuint(ui_test_harness_walk(GTK_WIDGET(gtk_widget_get_root(board))),
                   >, 0);
  ui_assert_stable_text_panels(board);
}
