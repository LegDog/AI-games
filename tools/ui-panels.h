// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <gtk/gtk.h>

/* Reserve the panel even when its message is empty or hidden. A fixed height
 * and non-propagating natural size keep text updates out of board allocation. */
static inline GtkWidget *ui_fixed_text_panel(GtkWidget *content, int height)
{
    GtkWidget *panel = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(panel),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(panel), FALSE);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(panel), FALSE);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(panel), height);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(panel), height);
    gtk_widget_set_vexpand(panel, FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(panel), content);
    g_object_set_data(G_OBJECT(panel), "fixed-text-panel", GINT_TO_POINTER(height));
    return panel;
}

static inline GtkWidget *ui_message_panel(GtkWidget *status, GtkWidget *error)
{
    GtkWidget *messages = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *labels[] = {status, error};
    for (unsigned i = 0; i < G_N_ELEMENTS(labels); i++) {
        gtk_label_set_selectable(GTK_LABEL(labels[i]), TRUE);
        gtk_label_set_wrap_mode(GTK_LABEL(labels[i]), PANGO_WRAP_WORD_CHAR);
        gtk_box_append(GTK_BOX(messages), labels[i]);
    }
    return ui_fixed_text_panel(messages, 120);
}
