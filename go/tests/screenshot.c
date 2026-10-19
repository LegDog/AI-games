// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui-fixture.h"
static gboolean capture(App *a, const char *directory, const char *name) {
  GdkPaintable *paintable = gtk_widget_paintable_new(a->window);
  /* Observe a rendered frame before snapshotting a paused animation. */
  gtk_widget_queue_draw(a->window);
  pump(100);
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
  g_mkdir_with_parents(directory, 0755);
  g_autofree char *old = g_get_current_dir();
  g_autofree char *temporary = g_dir_make_tmp("go-shots-XXXXXX", NULL);
  g_chdir(temporary);
  App *a = fixture();
  g_assert_true(capture(a, directory, "go-setup.png"));
  humans(a);
  place_fixture(a, WHITE, "A1");
  place_fixture(a, BLACK, "B1");
  if (a->game.state.turn != BLACK)
    game_play(&a->game, GO_PASS);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->delay), 3);
  g_assert_true(start_move(a, game_point("A2")));
  a->animation.started = g_get_monotonic_time() - 2250000;
  pause_clicked(NULL, a);
  g_assert_true(capture(a, directory, "go-capture.png"));
  restart_clicked(NULL, a);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->delay), 0);
  start_move(a, game_point("B1"));
  start_move(a, game_point("J9"));
  start_move(a, game_point("A2"));
  start_move(a, GO_PASS);
  start_move(a, GO_PASS);
  g_assert_true(game_finished(&a->game));
  g_assert_true(capture(a, directory, "go-result.png"));
  dispose(a);
  g_remove(GAME_HISTORY_FILE);
  g_chdir(old);
  g_rmdir(temporary);
  g_clear_object(&test_application);
  return 0;
}
