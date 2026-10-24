// SPDX-License-Identifier: GPL-3.0-or-later
#define ALQUERQUE_UI_TEST
#include "../src/main.c"
static gboolean settled(gpointer loop) {
  g_main_loop_quit(loop);
  return G_SOURCE_REMOVE;
}
int main(int argc, char **argv) {
  if (argc != 2 || g_mkdir_with_parents(argv[1], 0755) < 0)
    return 2;
  adw_init();
  g_autoptr(AdwApplication) application = adw_application_new(
      "io.github.aigames.AlquerquePreview", G_APPLICATION_NON_UNIQUE);
  if (!g_application_register(G_APPLICATION(application), NULL, NULL))
    return 1;
  activate(GTK_APPLICATION(application), NULL);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(app.kind[1]), 0);
  stop();
  game_init(&app.game);
  app.game = (Game){.pieces = {1u << 0, (1u << 6) | (1u << 18) | (1u << 4)},
                    .turn = 1};
  app.selected = 0;
  app.draft = (GameMove){.from = 0};
  update();
  g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
  g_timeout_add(500, settled, loop);
  g_main_loop_run(loop);
  GtkWidget *widget = GTK_WIDGET(app.window);
  int width = gtk_widget_get_width(widget),
      height = gtk_widget_get_height(widget);
  g_autoptr(GdkPaintable) paintable = gtk_widget_paintable_new(widget);
  GtkSnapshot *snapshot = gtk_snapshot_new();
  gdk_paintable_snapshot(paintable, snapshot, width, height);
  GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
  if (!node)
    return 1;
  graphene_rect_t bounds = GRAPHENE_RECT_INIT(0, 0, width, height);
  GskRenderer *renderer = gtk_native_get_renderer(GTK_NATIVE(widget));
  g_autoptr(GdkTexture) texture =
      gsk_renderer_render_texture(renderer, node, &bounds);
  gsk_render_node_unref(node);
  g_autofree char *path = g_build_filename(argv[1], "alquerque.png", NULL);
  gboolean ok = gdk_texture_save_to_png(texture, path);
  stop();
  gtk_window_destroy(app.window);
  g_string_free(app.history, TRUE);
  return ok ? 0 : 1;
}
