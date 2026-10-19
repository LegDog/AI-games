// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"
#include <gio/gio.h>
#include <string.h>
static GPtrArray *parse(const char *s) {
  GError *error = NULL;
  GPtrArray *p = levels_parse(s, strlen(s), &error);
  g_assert_no_error(error);
  g_assert_nonnull(p);
  return p;
}
static void rules(void) {
  GPtrArray *pack = levels_builtin();
  g_assert_cmpuint(pack->len, ==, 8);
  Game g;
  game_init(&g, g_ptr_array_index(pack, 0));
  g_assert_true(game_valid(&g));
  Game before = g;
  g_assert_false(game_step(&g, UP, NULL));
  g_assert_cmpmem(&g.pos, sizeof g.pos, &before.pos, sizeof before.pos);
  gboolean pushed;
  g_assert_true(game_step(&g, RIGHT, &pushed));
  g_assert_false(pushed);
  g_assert_cmpuint(g.walking, ==, 1);
  g_assert_true(game_step(&g, RIGHT, &pushed));
  g_assert_true(pushed);
  g_assert_true(game_solved(&g));
  g_assert_cmpuint(g.pushes, ==, 1);
  g_assert_false(game_step(&g, RIGHT, NULL));
  g_ptr_array_unref(pack);
  pack = parse("########\n#@ $$..#\n########\n");
  game_init(&g, g_ptr_array_index(pack, 0));
  game_step(&g, RIGHT, NULL);
  g_assert_false(game_step(&g, RIGHT, NULL));
  g_ptr_array_unref(pack);
}
static void imports(void) {
  GPtrArray *pack = parse("; First\r\n #####\r\n #@$.#\r\n #####\r\n\r\n; "
                          "Second\r\n#####\r\n#@ *#\r\n#####\r\n");
  g_assert_cmpuint(pack->len, ==, 2);
  Game g;
  game_init(&g, g_ptr_array_index(pack, 1));
  g_assert_true(game_solved(&g));
  g_ptr_array_unref(pack);
  GString *unicode = g_string_new("; ");
  for (unsigned i = 0; i < 100; i++)
    g_string_append(unicode, "箱");
  g_string_append(unicode, "\n#####\n#@$.#\n#####");
  pack = parse(unicode->str);
  g_assert_true(
      g_utf8_validate(((Level *)g_ptr_array_index(pack, 0))->name, -1, NULL));
  g_ptr_array_unref(pack);
  g_string_free(unicode, TRUE);
  const char *bad[] = {"",
                       "###\n#@#\n###",
                       "#####\n#@@$#\n# . #\n#####",
                       "#####\n#@x$#\n# . #\n#####",
                       "#####\n#@$. \n#####",
                       "#####\n#@$$#\n# . #\n#####"};
  for (unsigned i = 0; i < G_N_ELEMENTS(bad); i++) {
    GError *e = NULL;
    g_assert_null(levels_parse(bad[i], strlen(bad[i]), &e));
    g_assert_nonnull(e);
    g_clear_error(&e);
  }
  GError *e = NULL;
  g_assert_null(levels_parse("x", 1024 * 1024 + 1, &e));
  g_clear_error(&e);
  const char nul[] = {'#', 0, '#'};
  g_assert_null(levels_parse(nul, sizeof nul, &e));
  g_clear_error(&e);
  GString *many = g_string_new(NULL);
  for (unsigned i = 0; i < 257; i++)
    g_string_append(many, "#####\n#@$.#\n#####\n\n");
  g_assert_null(levels_parse(many->str, many->len, &e));
  g_assert_nonnull(strstr(e->message, "257"));
  g_clear_error(&e);
  g_string_free(many, TRUE);
}
static void pushes(void) {
  GPtrArray *pack = levels_builtin();
  Game g;
  game_init(&g, g_ptr_array_index(pack, 1));
  MoveList moves = game_pushes(&g);
  g_assert_cmpuint(moves.count, >, 0);
  for (unsigned i = 0; i < moves.count; i++) {
    Game next = g;
    GByteArray *path = game_push_path(&g, moves.items[i]);
    g_assert_nonnull(path);
    unsigned pushes = 0;
    for (unsigned j = 0; j < path->len; j++) {
      gboolean pushed;
      g_assert_true(game_step(&next, path->data[j], &pushed));
      pushes += pushed;
      g_assert_true(game_valid(&next));
    }
    g_assert_cmpuint(pushes, ==, 1);
    g_assert_cmpuint(next.pos.player, ==, moves.items[i] / 4);
    g_byte_array_unref(path);
  }
  g_assert_null(game_push_path(&g, 65535));
  g_ptr_array_unref(pack);
}
static void deadlocks(void) {
  const char *maps[] = {"######\n#$@ .#\n#    #\n######",
                        "#######\n#@    #\n# $$  #\n# $$..#\n#   ..#\n#######"};
  for (unsigned i = 0; i < G_N_ELEMENTS(maps); i++) {
    GPtrArray *p = parse(maps[i]);
    Game g;
    game_init(&g, g_ptr_array_index(p, 0));
    Analysis a;
    game_analyze(g.level, &a);
    g_assert_true(game_deadlocked(&g, &a));
    g_ptr_array_unref(p);
  }
  GPtrArray *p = levels_builtin();
  for (unsigned i = 0; i < p->len; i++) {
    Game g;
    game_init(&g, g_ptr_array_index(p, i));
    Analysis a;
    game_analyze(g.level, &a);
    g_assert_false(game_deadlocked(&g, &a));
  }
  g_ptr_array_unref(p);
}
static void matching(void) {
  GPtrArray *p =
      parse("#########\n#@ $$ .##\n#      ##\n#########\n## . ####\n#########");
  Game g;
  game_init(&g, g_ptr_array_index(p, 0));
  Analysis a;
  game_analyze(g.level, &a);
  for (unsigned i = 0; i < g.level->boxes; i++)
    g_assert_false(a.dead[g.pos.boxes[i]]);
  gboolean impossible;
  g_assert_cmpuint(game_estimate(&g, &a, &impossible), ==, 0);
  g_assert_true(impossible);
  g_assert_true(game_deadlocked(&g, &a));
  g_ptr_array_unref(p);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/game/rules", rules);
  g_test_add_func("/game/imports", imports);
  g_test_add_func("/game/pushes", pushes);
  g_test_add_func("/game/deadlocks", deadlocks);
  g_test_add_func("/game/matching", matching);
  return g_test_run();
}
