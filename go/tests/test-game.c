// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"
#include <string.h>
static void place(Game *g, unsigned color, const char *label) {
  if (g->state.turn != color)
    g_assert_true(game_play(g, GO_PASS));
  g_assert_true(game_play(g, game_point(label)));
}
static void basics(void) {
  Game g;
  game_init(&g, 7.5);
  g_assert_true(game_valid(&g));
  g_assert_cmpuint(game_legal(&g).count, ==, 82);
  g_assert_cmpstr(game_label(80), ==, "J9");
  g_assert_cmpint(game_point("I9"), ==, -1);
  g_assert_true(game_play(&g, 0));
  g_assert_cmpint(game_try(&g, 0, NULL), ==, MOVE_OCCUPIED);
  g_assert_cmpint(game_try(&g, 90, NULL), ==, MOVE_OUTSIDE);
  g_assert_cmpint(game_at(&g.state.board, 0), ==, BLACK);
  g_assert_true(game_play(&g, GO_PASS));
  g_assert_cmpuint(g.state.passes, ==, 1);
  g_assert_true(game_play(&g, 1));
  g_assert_cmpuint(g.state.passes, ==, 0);
  g_assert_true(game_play(&g, GO_PASS));
  g_assert_true(game_play(&g, GO_PASS));
  g_assert_true(game_finished(&g));
  g_assert_cmpint(game_try(&g, 2, NULL), ==, MOVE_FINISHED);
  g_assert_true(game_undo(&g));
  g_assert_false(game_finished(&g));
  g_assert_cmpuint(g.state.passes, ==, 1);
  g_assert_true(game_valid(&g));
  game_clear(&g);
}
static void captures(void) {
  Game g;
  game_init(&g, 0);
  place(&g, WHITE, "A1");
  place(&g, BLACK, "B1");
  place(&g, BLACK, "A2");
  g_assert_cmpint(game_at(&g.state.board, 0), ==, -1);
  g_assert_cmpuint(g.state.captures[BLACK], ==, 1);
  g_assert_cmpint(game_try(&g, 0, NULL), ==, MOVE_SUICIDE);
  g_assert_true(game_valid(&g));
  g_assert_true(game_undo(&g));
  g_assert_cmpuint(g.state.captures[BLACK], ==, 0);
  g_assert_cmpint(game_at(&g.state.board, 0), ==, WHITE);
  game_clear(&g);
  game_init(&g, 0);
  place(&g, WHITE, "B1");
  place(&g, WHITE, "A2");
  place(&g, BLACK, "C1");
  place(&g, BLACK, "B2");
  place(&g, BLACK, "A3");
  place(&g, BLACK, "A1");
  g_assert_cmpuint(g.state.captures[BLACK], ==, 2);
  g_assert_true(game_group(&g.state.board, 0).liberties > 0);
  g_assert_true(game_valid(&g));
  game_clear(&g);
}
static void ko(void) {
  Game g;
  game_init(&g, 7.5);
  place(&g, BLACK, "B3");
  place(&g, BLACK, "D3");
  place(&g, BLACK, "C4");
  place(&g, WHITE, "C3");
  place(&g, WHITE, "B2");
  place(&g, WHITE, "D2");
  place(&g, WHITE, "C1");
  g_assert_true(game_valid(&g));
  place(&g, BLACK, "C2");
  g_assert_cmpint(game_try(&g, game_point("C3"), NULL), ==, MOVE_SUPERKO);
  Game branch;
  game_copy(&branch, &g, 4);
  g_assert_true(game_play(&branch, GO_PASS));
  g_assert_true(game_play(&branch, game_point("J9")));
  g_assert_true(game_play(&branch, game_point("C3")));
  g_assert_true(game_valid(&branch));
  g_assert_cmpint(game_try(&g, game_point("C3"), NULL), ==, MOVE_SUPERKO);
  game_clear(&branch);
  g_assert_true(game_undo(&g));
  g_assert_true(game_play(&g, game_point("C2")));
  g_assert_cmpint(game_try(&g, game_point("C3"), NULL), ==, MOVE_SUPERKO);
  game_clear(&g);
}
static void snapback(void) {
  Game g;
  game_init(&g, 0);
  const char *black[] = {"C1", "B3", "D3", "C4"},
             *white[] = {"B1", "D1", "B2", "D2", "C3"};
  for (unsigned i = 0; i < G_N_ELEMENTS(black); i++)
    place(&g, BLACK, black[i]);
  for (unsigned i = 0; i < G_N_ELEMENTS(white); i++)
    place(&g, WHITE, white[i]);
  g_assert_true(game_valid(&g));
  g_assert_true(game_play(&g, game_point("C2")));
  g_assert_cmpuint(g.state.captures[BLACK], ==, 1);
  g_assert_cmpint(game_try(&g, game_point("C3"), NULL), ==, MOVE_OK);
  g_assert_true(game_play(&g, game_point("C3")));
  g_assert_cmpuint(g.state.captures[WHITE], ==, 2);
  g_assert_cmpint(game_at(&g.state.board, game_point("C1")), ==, -1);
  g_assert_true(game_valid(&g));
  game_clear(&g);
}
static void triple_ko(void) {
  Game g;
  game_init(&g, 7.5);
  const char *black[] = {"B3", "D3", "C4", "F2", "H2",
                         "G1", "G3", "D8", "F8", "E9"};
  const char *white[] = {"C3", "B2", "D2", "C1", "F3", "H3",
                         "G4", "E8", "D7", "F7", "E6"};
  for (unsigned i = 0; i < G_N_ELEMENTS(black); i++)
    place(&g, BLACK, black[i]);
  for (unsigned i = 0; i < G_N_ELEMENTS(white); i++)
    place(&g, WHITE, white[i]);
  g_assert_true(game_valid(&g));
  const char *sequence[] = {"C2", "G2", "E7", "C3", "G3"};
  for (unsigned i = 0; i < G_N_ELEMENTS(sequence); i++)
    g_assert_true(game_play(&g, game_point(sequence[i])));
  g_assert_cmpint(game_try(&g, game_point("E8"), NULL), ==, MOVE_SUPERKO);
  g_assert_true(game_valid(&g));
  game_clear(&g);
}
static void hash_collision(void) {
  Game g;
  game_init(&g, 7.5);
  State candidate;
  g_assert_cmpint(game_try(&g, 0, &candidate), ==, MOVE_OK);
  g_array_index(g.history, State, 0).hash = candidate.hash;
  for (unsigned i = 0; i < GO_BLOOM; i++)
    g.seen[i] = G_MAXUINT64;
  g_assert_cmpint(game_try(&g, 0, NULL), ==, MOVE_OK);
  game_clear(&g);
}
static void scoring(void) {
  Game g;
  game_init(&g, 7.5);
  Score s;
  game_score(&g, &s);
  g_assert_cmpuint(s.neutral, ==, 81);
  g_assert_cmpfloat(s.margin, ==, -7.5);
  g_assert_true(game_play(&g, 0));
  game_score(&g, &s);
  g_assert_cmpuint(s.territory[BLACK], ==, 80);
  g_assert_cmpfloat(s.area[BLACK], ==, 81);
  g_assert_true(game_play(&g, 80));
  game_score(&g, &s);
  g_assert_cmpuint(s.neutral, ==, 79);
  g_assert_cmpuint(s.territory[WHITE], ==, 0);
  g.state.captures[BLACK] = 100;
  game_score(&g, &s);
  g_assert_cmpfloat(s.margin, ==, -7.5);
  game_clear(&g);
  game_init(&g, 0);
  g_assert_true(game_play(&g, GO_PASS));
  g_assert_true(game_play(&g, GO_PASS));
  g_assert_cmpint(game_winner(&g), ==, 2);
  game_clear(&g);
  game_init(&g, 0);
  place(&g, BLACK, "B1");
  place(&g, BLACK, "A2");
  place(&g, WHITE, "J9");
  game_score(&g, &s);
  g_assert_cmpint(s.owner[0], ==, BLACK);
  g_assert_cmpuint(s.territory[BLACK], ==, 1);
  game_clear(&g);
}
static void random_games(void) {
  GRand *r = g_rand_new_with_seed(12);
  for (unsigned run = 0; run < 10; run++) {
    Game g;
    game_init(&g, 7.5);
    for (unsigned i = 0; i < 300 && !game_finished(&g); i++) {
      MoveList legal = game_legal(&g);
      g_assert_cmpuint(legal.count, >, 0);
      g_assert_true(game_play(&g, game_random(&legal, r)));
      g_assert_true(game_valid(&g));
    }
    Score s;
    game_score(&g, &s);
    g_assert_cmpuint(s.stones[0] + s.stones[1] + s.territory[0] +
                         s.territory[1] + s.neutral,
                     ==, 81);
    game_clear(&g);
  }
  g_rand_free(r);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/go/basics", basics);
  g_test_add_func("/go/captures-suicide", captures);
  g_test_add_func("/go/superko-branches-undo", ko);
  g_test_add_func("/go/snapback", snapback);
  g_test_add_func("/go/triple-ko", triple_ko);
  g_test_add_func("/go/hash-collision", hash_collision);
  g_test_add_func("/go/scoring", scoring);
  g_test_add_func("/go/random-valid", random_games);
  return g_test_run();
}
