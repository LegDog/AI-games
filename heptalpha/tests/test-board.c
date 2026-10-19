// SPDX-License-Identifier: GPL-3.0-or-later
#include "board.h"
#include <math.h>
static double distance(BoardPoint a, BoardPoint b) { return hypot(a.x-b.x, a.y-b.y); }
static void geometry(void)
{
    unsigned memberships[GAME_POINTS] = {0};
    for (unsigned path = 0; path < GAME_PATHS; path++) {
        double previous = -1;
        for (unsigned p = 0; p < GAME_PATH_POINTS; p++) {
            unsigned point = game_lines[path][p]; memberships[point]++;
            double t = board_paths[path].parameters[p];
            g_assert_cmpfloat(t, >, previous); previous = t;
            g_assert_cmpfloat(distance(board_evaluate(path, t), board_points[point]), <, 1e-8);
        }
    }
    for (unsigned p = 0; p < GAME_POINTS; p++) g_assert_cmpuint(memberships[p], ==, 2);
    for (unsigned a = 0; a < GAME_POINTS; a++) for (unsigned b = 0; b < a; b++)
        g_assert_cmpfloat(distance(board_points[a], board_points[b]), >, 20);
}
static void motion(void)
{
    for (unsigned m = 0; m < GAME_CHOICES; m++) {
        Move move = game_moves[m];
        g_assert_cmpfloat(distance(board_move_position(m, 0), board_points[move.entry]), <, 1e-8);
        g_assert_cmpfloat(distance(board_move_position(m, 1), board_points[move.destination]), <, 1e-8);
        double shortest = G_MAXDOUBLE, longest = 0;
        BoardPoint previous = board_move_position(m, 0);
        double middle_progress = 0, best = G_MAXDOUBLE;
        for (unsigned step = 1; step <= 100; step++) {
            BoardPoint point = board_move_position(m, step/100.0);
            double delta = distance(point, previous); shortest = MIN(shortest, delta); longest = MAX(longest, delta);
            double middle = distance(point, board_points[move.middle]);
            if (middle < best) { best = middle; middle_progress = step/100.0; }
            previous = point;
        }
        g_assert_cmpfloat(longest / shortest, <, 1.015);
        /* Refine around the nearest sample; the arc must pass through the middle. */
        for (unsigned step = 0; step <= 200; step++) {
            double progress = middle_progress - .01 + step * .0001;
            best = MIN(best, distance(board_move_position(m, progress), board_points[move.middle]));
        }
        g_assert_cmpfloat(best, <, .025);
        int reversed = game_find_move(move.destination, move.entry);
        for (unsigned step = 0; step <= 10; step++)
            g_assert_cmpfloat(distance(board_move_position(m, step/10.0), board_move_position(reversed, 1-step/10.0)), <, 1e-7);
        BoardPoint halfway = board_move_position(m, .5);
        BoardPoint chord = {(board_points[move.entry].x+board_points[move.destination].x)/2,
                            (board_points[move.entry].y+board_points[move.destination].y)/2};
        g_assert_cmpfloat(distance(halfway, chord), >, 1); /* Never slide straight across a curved path. */
    }
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/board/geometry", geometry); g_test_add_func("/board/curved-motion", motion);
    return g_test_run();
}
