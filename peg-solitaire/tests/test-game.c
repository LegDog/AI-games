// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"
static void rules(void)
{
    for (BoardKind kind = BOARD_ENGLISH; kind <= BOARD_TRIANGLE; kind++) {
        const Board *b = game_board(kind); Game game; g_assert_true(game_init(&game,kind,GOAL_ANY));
        g_assert_cmpuint(b->points,==,kind == BOARD_ENGLISH ? 33 : 15);
        g_assert_cmpuint(b->choices,==,kind == BOARD_ENGLISH ? 76 : 36);
        g_assert_cmpuint(game_count(game.occupied),==,b->points-1);
        g_assert_false((game.occupied & POINT_BIT(b->opening)) != 0);
        for (unsigned p = 0; p < b->points; p++) g_assert_cmpint(game_point(b,b->labels[p]),==,(int) p);
        for (unsigned i = 0; i < b->choices; i++) {
            Move m = b->jumps[i];
            g_assert_cmpint(b->row[m.source]+b->row[m.destination],==,2*b->row[m.middle]);
            g_assert_cmpint(b->column[m.source]+b->column[m.destination],==,2*b->column[m.middle]);
            game.occupied = POINT_BIT(m.source)|POINT_BIT(m.middle); game.moves = 0;
            g_assert_true(game_legal(&game,i)); g_assert_true(game_play(&game,i));
            g_assert_cmpuint(game.occupied,==,POINT_BIT(m.destination)); g_assert_cmpint(game_result(&game),==,GAME_SOLVED);
            g_assert_false(game_play(&game,i)); g_assert_true(game_undo(&game));
            g_assert_cmpuint(game.occupied,==,POINT_BIT(m.source)|POINT_BIT(m.middle));
            game.occupied = POINT_BIT(m.source); g_assert_false(game_legal(&game,i));
            game.occupied = POINT_BIT(m.middle); g_assert_false(game_legal(&game,i));
            game.occupied = POINT_BIT(m.source)|POINT_BIT(m.middle)|POINT_BIT(m.destination); g_assert_false(game_legal(&game,i));
            g_assert_cmpint(game_find_move(b,m.source,m.destination),==,(int) i);
        }
        g_assert_false(game_play(&game,b->choices)); game.moves = 0; g_assert_false(game_undo(&game));
        game.occupied = POINT_BIT(0)|POINT_BIT(b->points-1); g_assert_cmpint(game_result(&game),==,GAME_STUCK);
    }
    Game game; g_assert_false(game_init(&game,BOARD_TRIANGLE,GOAL_CENTER));
    game_init(&game,BOARD_ENGLISH,GOAL_CENTER); game.occupied = POINT_BIT(0);
    g_assert_cmpint(game_result(&game),==,GAME_WRONG_FINISH);
    game.occupied = POINT_BIT(game_board(BOARD_ENGLISH)->opening); g_assert_cmpint(game_result(&game),==,GAME_SOLVED);
    g_assert_cmpint(game_point(game_board(BOARD_ENGLISH),"a1"),==,-1);
}
static void triangle_exhaustive(void)
{
    const Board *b = game_board(BOARD_TRIANGLE); Game game; game_init(&game,BOARD_TRIANGLE,GOAL_ANY);
    /* Independent coordinate enumeration, including all six triangular axes. */
    const int direction[6][2] = {{0,1},{0,-1},{1,0},{-1,0},{1,1},{-1,-1}};
    for (guint64 mask = 1; mask < POINT_BIT(15); mask++) {
        game.occupied = mask; MoveList legal = game_legal_moves(&game); unsigned expected = 0;
        for (unsigned source = 0; source < 15; source++) for (unsigned d = 0; d < 6; d++) {
            int middle = -1, destination = -1;
            for (unsigned p = 0; p < 15; p++) {
                if (b->row[p] == b->row[source]+direction[d][0] && b->column[p] == b->column[source]+direction[d][1]) middle = p;
                if (b->row[p] == b->row[source]+2*direction[d][0] && b->column[p] == b->column[source]+2*direction[d][1]) destination = p;
            }
            if (middle >= 0 && destination >= 0 && (mask & POINT_BIT(source)) && (mask & POINT_BIT(middle)) && !(mask & POINT_BIT(destination))) expected++;
        }
        g_assert_cmpuint(legal.count,==,expected);
    }
}
static void symmetry(void)
{
    for (unsigned kind = 0; kind < 2; kind++) {
        const Board *b = game_board(kind); Game game; game_init(&game,kind,GOAL_ANY); guint64 key = game_canonical(&game);
        for (unsigned s = 0; s < b->symmetries; s++) {
            Game transformed = game; transformed.occupied = 0;
            for (unsigned p = 0; p < b->points; p++) if (game.occupied & POINT_BIT(p)) transformed.occupied |= POINT_BIT(b->transform[s][p]);
            g_assert_cmpuint(game_canonical(&transformed),==,key);
            for (unsigned m = 0; m < b->choices; m++) {
                Move move = b->jumps[m]; int mapped = game_find_move(b,b->transform[s][move.source],b->transform[s][move.destination]);
                g_assert_cmpint(mapped,>=,0); g_assert_cmpuint(b->jumps[mapped].middle,==,b->transform[s][move.middle]);
            }
        }
    }
}
int main(int argc,char **argv)
{
    g_test_init(&argc,&argv,NULL); g_test_add_func("/game/rules",rules);
    g_test_add_func("/game/triangle-exhaustive",triangle_exhaustive); g_test_add_func("/game/symmetries",symmetry); return g_test_run();
}
