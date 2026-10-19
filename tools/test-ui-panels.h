// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <gtk/gtk.h>

static void panel_test_pump(void)
{
    gint64 end = g_get_monotonic_time() + 80000;
    do {
        while (g_main_context_iteration(NULL, FALSE)) {}
        g_usleep(1000);
    } while (g_get_monotonic_time() < end);
}

static void panel_test_labels(GtkWidget *widget, GPtrArray *labels)
{
    /* Skip button labels: this regression exercises text, not controls. */
    if (GTK_IS_BUTTON(widget)) return;
    if (GTK_IS_LABEL(widget)) g_ptr_array_add(labels, widget);
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child))
        panel_test_labels(child, labels);
}

static void panel_test_geometry(GtkWidget *board, const graphene_rect_t *before)
{
    graphene_rect_t after;
    g_assert_true(gtk_widget_compute_bounds(board, GTK_WIDGET(gtk_widget_get_root(board)), &after));
    g_assert_cmpfloat(after.origin.x, ==, before->origin.x);
    g_assert_cmpfloat(after.origin.y, ==, before->origin.y);
    g_assert_cmpfloat(after.size.width, ==, before->size.width);
    g_assert_cmpfloat(after.size.height, ==, before->size.height);
}

static unsigned panel_test_walk(GtkWidget *widget, GtkWidget *board)
{
    unsigned count = 0;
    if (g_object_get_data(G_OBJECT(widget), "fixed-text-panel")) {
        panel_test_pump();
        graphene_rect_t before;
        g_assert_true(gtk_widget_compute_bounds(board, GTK_WIDGET(gtk_widget_get_root(board)), &before));
        g_assert_cmpfloat(before.size.width, >, 0);
        g_assert_cmpfloat(before.size.height, >, 0);
        g_autoptr(GPtrArray) labels = g_ptr_array_new();
        panel_test_labels(widget, labels);
        g_assert_cmpuint(labels->len, >, 0);
        g_autoptr(GString) overflow = g_string_new(NULL);
        for (unsigned i = 0; i < 80; i++)
            g_string_append(overflow, "Long status / erro / información / istruzioni. Words wrap and overflow.\n");
        for (unsigned i = 0; i < labels->len; i++) {
            GtkWidget *label = g_ptr_array_index(labels, i);
            g_autofree char *original = g_strdup(gtk_label_get_text(GTK_LABEL(label)));
            g_autoptr(GPtrArray) ancestors = g_ptr_array_new();
            GArray *visible = g_array_new(FALSE, FALSE, sizeof(gboolean));
            for (GtkWidget *at = label; at != widget; at = gtk_widget_get_parent(at)) {
                gboolean was_visible = gtk_widget_get_visible(at);
                g_ptr_array_add(ancestors, at);
                g_array_append_val(visible, was_visible);
                gtk_widget_set_visible(at, TRUE);
            }
            gtk_label_set_text(GTK_LABEL(label), overflow->str);
            panel_test_pump();
            panel_test_geometry(board, &before);
            GtkAdjustment *adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(widget));
            g_assert_cmpfloat(gtk_adjustment_get_upper(adjustment), >, gtk_adjustment_get_page_size(adjustment));
            gtk_adjustment_set_value(adjustment, gtk_adjustment_get_upper(adjustment));
            panel_test_pump();
            panel_test_geometry(board, &before);
            gtk_label_set_text(GTK_LABEL(label), "");
            gtk_widget_set_visible(label, FALSE);
            panel_test_pump();
            panel_test_geometry(board, &before);
            gtk_label_set_text(GTK_LABEL(label), original);
            for (unsigned j = 0; j < ancestors->len; j++)
                gtk_widget_set_visible(g_ptr_array_index(ancestors, j), g_array_index(visible, gboolean, j));
            g_array_unref(visible);
            gtk_adjustment_set_value(adjustment, 0);
            panel_test_pump();
            panel_test_geometry(board, &before);
        }
        count++;
        return count;
    }
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child;
         child = gtk_widget_get_next_sibling(child))
        count += panel_test_walk(child, board);
    return count;
}

static void ui_assert_stable_text_panels(GtkWidget *board)
{
    panel_test_pump();
    g_assert_cmpuint(panel_test_walk(GTK_WIDGET(gtk_widget_get_root(board)), board), >, 0);
}
