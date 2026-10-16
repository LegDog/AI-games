#pragma once
#include <gtk/gtk.h>
#include "i18n.h"

/* Bind original message IDs to widgets so both visible and hidden pages can
 * be translated in place, preserving the board, selections and pending jobs. */
static GtkWidget *ui_label(const char *message)
{
    GtkWidget *widget = gtk_label_new(_(message));
    g_object_set_data_full(G_OBJECT(widget), "i18n-label", g_strdup(message), g_free);
    return widget;
}

static GtkWidget *ui_button(const char *message)
{
    GtkWidget *widget = gtk_button_new_with_label(_(message));
    g_object_set_data_full(G_OBJECT(widget), "i18n-label", g_strdup(message), g_free);
    return widget;
}

static GtkWidget *ui_check(const char *message)
{
    GtkWidget *widget = gtk_check_button_new_with_label(_(message));
    g_object_set_data_full(G_OBJECT(widget), "i18n-label", g_strdup(message), g_free);
    return widget;
}

static void ui_tooltip(GtkWidget *widget, const char *message)
{
    gtk_widget_set_tooltip_text(widget, _(message));
    g_object_set_data_full(G_OBJECT(widget), "i18n-tooltip", g_strdup(message), g_free);
}

static void ui_title(GtkWindow *window, const char *message)
{
    gtk_window_set_title(window, _(message));
    g_object_set_data_full(G_OBJECT(window), "i18n-title", g_strdup(message), g_free);
}

static void ui_retranslate_tree(GtkWidget *widget)
{
    const char *message = g_object_get_data(G_OBJECT(widget), "i18n-label");
    if (message) {
        if (GTK_IS_LABEL(widget)) gtk_label_set_text(GTK_LABEL(widget), _(message));
        else if (GTK_IS_CHECK_BUTTON(widget)) gtk_check_button_set_label(GTK_CHECK_BUTTON(widget), _(message));
        else if (GTK_IS_BUTTON(widget)) gtk_button_set_label(GTK_BUTTON(widget), _(message));
    }
    message = g_object_get_data(G_OBJECT(widget), "i18n-tooltip");
    if (message) gtk_widget_set_tooltip_text(widget, _(message));
    message = g_object_get_data(G_OBJECT(widget), "i18n-title");
    if (message && GTK_IS_WINDOW(widget)) gtk_window_set_title(GTK_WINDOW(widget), _(message));
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
        ui_retranslate_tree(child);
}
