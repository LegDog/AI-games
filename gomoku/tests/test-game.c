// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"
static int reference(const Game *g) {
  unsigned occupied = 0;
  for (int p = 0; p < 225; p++) {
    int who = g->cells[p];
    if (!who)
      continue;
    occupied++;
    const int dy[] = {0, 1, 1, 1}, dx[] = {1, 0, 1, -1};
    for (int d = 0; d < 4; d++) {
      gboolean five = TRUE;
      for (int k = 1; k < 5; k++) {
        int y = p / 15 + k * dy[d], x = p % 15 + k * dx[d];
        if (y >= 15 || x < 0 || x >= 15 || g->cells[y * 15 + x] != who) {
          five = FALSE;
          break;
        }
      }
      if (five)
        return who;
    }
  }
  return occupied == 225 ? GAME_DRAW : GAME_ONGOING;
}
static void rules(void) {
  Game g;
  game_init(&g);
  GameMoves legal;
  game_legal_moves(&g, &legal);
  g_assert_cmpuint(legal.count, ==, 225);
  for (unsigned i = 0; i < legal.count; i++) {
    char label[16];
    GameMove parsed;
    game_move_label(legal.moves[i], label);
    g_assert_true(game_parse_move(label, &parsed));
    g_assert_cmpint(parsed.cell, ==, (int)i);
    Game next = g;
    g_assert_true(game_play(&next, parsed));
    g_assert_cmpint(next.cells[i], ==, 1);
    g_assert_cmpint(next.turn, ==, -1);
    g_assert_false(game_play(&next, parsed));
  }
  const char *bad[] = {
      "",   "a0",     "a01", "a16", "p1",
      "A1", "a1:win", "a1 ", "a+1", "a999999999999999999999999"};
  for (unsigned i = 0; i < G_N_ELEMENTS(bad); i++)
    g_assert_cmpint(game_point(bad[i]), ==, -1);
  g_assert_cmpint(game_point(NULL), ==, -1);
  g_assert_false(game_play(&g, (GameMove){-1}));
  g_assert_false(game_play(&g, (GameMove){225}));
  const int start[] = {0, 0, 0, 14}, step[] = {1, 15, 16, 14};
  for (unsigned d = 0; d < 4; d++)
    for (int owner = -1; owner <= 1; owner += 2) {
      game_init(&g);
      for (int i = 0; i < 4; i++)
        g.cells[start[d] + step[d] * i] = owner;
      g_assert_cmpint(game_result(&g), ==, 0);
      g.turn = owner;
      g_assert_true(game_play(&g, (GameMove){start[d] + step[d] * 4}));
      g_assert_cmpint(game_result(&g), ==, owner);
      for (int i = 0; i < 5; i++)
        g_assert_true(game_winning_point(&g, start[d] + step[d] * i));
      g_assert_false(game_play(&g, (GameMove){224}));
      game_legal_moves(&g, &legal);
      g_assert_cmpuint(legal.count, ==, 0);
      g.cells[start[d] + step[d] * 5] = owner;
      g_assert_cmpint(game_result(&g), ==, owner); /* overline */
    }
  game_init(&g);
  for (int i = 13; i < 18; i++)
    g.cells[i] = 1;
  g_assert_cmpint(game_result(&g), ==, 0); /* no row wrapping */
  for (int r = 0; r < 15; r++)
    for (int c = 0; c < 15; c++)
      g.cells[r * 15 + c] = (r + 2 * c) % 4 < 2 ? 1 : -1;
  g_assert_cmpint(game_result(&g), ==, GAME_DRAW);
  g.cells[224] = 0;
  g.turn = -1;
  g_assert_cmpint(game_result(&g), ==, 0);
  g_assert_true(game_play(&g, (GameMove){224}));
  g_assert_cmpint(game_result(&g), ==, GAME_DRAW);
}
static void reference_games(void) {
  GRand *rng = g_rand_new_with_seed(727);
  for (unsigned n = 0; n < 100; n++) {
    Game g;
    game_init(&g);
    while (!game_result(&g)) {
      GameMoves legal;
      game_legal_moves(&g, &legal);
      g_assert_cmpuint(legal.count, ==, 225 - g.actions);
      unsigned chosen = game_random_move(&legal, rng);
      Game before = g;
      g_assert_true(game_play(&g, legal.moves[chosen]));
      for (int p = 0; p < 225; p++)
        g_assert_cmpint(g.cells[p], ==,
                        p == legal.moves[chosen].cell ? before.turn
                                                      : before.cells[p]);
      g_assert_cmpint(game_result(&g), ==, reference(&g));
      g_assert_cmpuint(game_count(&g, 1) + game_count(&g, -1), ==, g.actions);
    }
  }
  g_rand_free(rng);
}
static void tactics(void) {
  Game g;
  game_init(&g);
  GRand *rng = g_rand_new_with_seed(4);
  int values[GAME_CHOICES];
  GameMoves legal;
  for (int i = 0; i < 4; i++)
    g.cells[i] = 1;
  int move = game_positional_move(&g, rng, values);
  game_legal_moves(&g, &legal);
  g_assert_cmpint(legal.moves[move].cell, ==, 4);
  g.turn = -1;
  move = game_positional_move(&g, rng, values);
  g_assert_cmpint(legal.moves[move].cell, ==, 4);
  PositionValue a, b;
  game_position(&g, 1, &a);
  game_position(&g, -1, &b);
  g_assert_cmpint(a.total, ==, -b.total);
  g_rand_free(rng);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/game/rules-coordinates-overlines-draw", rules);
  g_test_add_func("/game/independent-random-replay", reference_games);
  g_test_add_func("/game/win-block-heuristic", tactics);
  return g_test_run();
}
