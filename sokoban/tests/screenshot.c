// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-fixture.h"
static gboolean capture(App *a, const char *directory, const char *name) {
  pump(100);
  GdkPaintable *paintable = gtk_widget_paintable_new(a->window);
  GtkSnapshot *snapshot = gtk_snapshot_new();
  int width = gtk_widget_get_width(a->window),
      height = gtk_widget_get_height(a->window);
  gdk_paintable_snapshot(GDK_PAINTABLE(paintable), GDK_SNAPSHOT(snapshot),
                         width, height);
  GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
  if (!node) {
    g_object_unref(paintable);
    return FALSE;
  }
  GskRenderer *renderer = gtk_native_get_renderer(GTK_NATIVE(a->window));
  graphene_rect_t viewport = GRAPHENE_RECT_INIT(0, 0, width, height);
  GdkTexture *texture = gsk_renderer_render_texture(renderer, node, &viewport);
  g_autofree char *path = g_build_filename(directory, name, NULL);
  gboolean ok = gdk_texture_save_to_png(texture, path);
  g_object_unref(texture);
  gsk_render_node_unref(node);
  g_object_unref(paintable);
  return ok;
}
int main(int argc, char **argv) {
  gtk_init();
  adw_init();
  g_autofree char *directory =
      g_canonicalize_filename(argc > 1 ? argv[1] : "screenshots", NULL);
  g_autofree char *old = g_get_current_dir();
  g_autofree char *temporary = g_dir_make_tmp("sokoban-shots-XXXXXX", NULL);
  g_chdir(temporary);
  g_mkdir_with_parents(directory, 0755);
  App *a = fixture();
  g_assert_true(capture(a, directory, "sokoban-setup.png"));
  gtk_drop_down_set_selected(GTK_DROP_DOWN(a->level), 5);
  begin_clicked(NULL, a);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->delay), 3);
  MoveList moves = game_pushes(&a->game);
  g_assert_cmpuint(moves.count, >, 0);
  GByteArray *path = game_push_path(&a->game, moves.items[0]);
  for (unsigned i = 0; i + 1 < path->len; i++)
    g_assert_true(game_step(&a->game, path->data[i], NULL));
  g_assert_true(start_step(a, path->data[path->len - 1]));
  g_byte_array_unref(path);
  g_assert_true(a->pushing);
  a->animation.started = g_get_monotonic_time() - 2250000;
  pause_clicked(NULL, a);
  g_assert_true(capture(a, directory, "sokoban-push.png"));
  configure_clicked(NULL, a);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(a->controller), PLAYER_ASTAR);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->delay), 0);
  begin_clicked(NULL, a);
  gint64 end = g_get_monotonic_time() + 5000000;
  while (!a->terminal && g_get_monotonic_time() < end)
    pump(2);
  g_assert_true(game_solved(&a->game));
  g_assert_true(capture(a, directory, "sokoban-result.png"));
  dispose(a);
  g_remove(GAME_HISTORY_FILE);
  g_chdir(old);
  g_rmdir(temporary);
  g_clear_object(&test_application);
  return 0;
}
