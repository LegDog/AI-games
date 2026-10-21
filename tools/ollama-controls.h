// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <gtk/gtk.h>
static inline void ui_ollama_exclusive(GtkCheckButton *active,
                                       GtkCheckButton *other) {
  if (gtk_check_button_get_active(active))
    gtk_check_button_set_active(other, FALSE);
}
/* Keep harness configuration alongside each existing thinking control. */
static inline GtkWidget *ui_ollama_harness_add(GtkWidget *thinking,
                                               GtkWidget *assisted,
                                               const char *label) {
  GtkWidget *check = gtk_check_button_new_with_label(label);
  g_object_set_data(G_OBJECT(check), "message", "Reasoning harness");
  g_object_set_data(G_OBJECT(check), "translation", "Reasoning harness");
  g_object_set_data(G_OBJECT(check), "i18n-label", "Reasoning harness");
  g_object_set_data(G_OBJECT(thinking), "ollama-harness", check);
  g_object_bind_property(thinking, "visible", check, "visible",
                         G_BINDING_SYNC_CREATE);
  g_object_bind_property(thinking, "sensitive", check, "sensitive",
                         G_BINDING_SYNC_CREATE);
  g_object_set_data(G_OBJECT(check), "ollama-assistance", assisted);
  if (assisted) {
    g_signal_connect_object(check, "toggled", G_CALLBACK(ui_ollama_exclusive),
                            assisted, 0);
    g_signal_connect_object(assisted, "toggled",
                            G_CALLBACK(ui_ollama_exclusive), check, 0);
  }
  GtkWidget *parent = gtk_widget_get_parent(thinking);
  gtk_box_append(GTK_BOX(parent), check);
  /* In horizontal option rows, let the three controls wrap onto separate lines.
   */
  if (gtk_orientable_get_orientation(GTK_ORIENTABLE(parent)) ==
      GTK_ORIENTATION_HORIZONTAL)
    gtk_orientable_set_orientation(GTK_ORIENTABLE(parent),
                                   GTK_ORIENTATION_VERTICAL);
  return check;
}
static inline gboolean ui_ollama_harness_active(GtkWidget *thinking) {
  GtkWidget *check = g_object_get_data(G_OBJECT(thinking), "ollama-harness");
  return check && gtk_check_button_get_active(GTK_CHECK_BUTTON(check));
}
