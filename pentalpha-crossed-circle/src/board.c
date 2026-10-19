// SPDX-License-Identifier: GPL-3.0-or-later
#include "board.h"
#include <math.h>
#include "board-data.inc"
enum { ARC_STEPS = 256 };
typedef struct { double length[ARC_STEPS + 1], start, end; } Arc;
static Arc arcs[GAME_CHOICES];
static gsize initialized;
BoardPoint board_evaluate(unsigned path, double t)
{
    const BoardPath *p = &board_paths[path];
    if (p->circular) {
        double angle=t*2*G_PI-G_PI/2;
        const BoardPoint center=board_points[35];
        double radius=hypot(board_points[5].x-center.x,board_points[5].y-center.y);
        return (BoardPoint){center.x+radius*cos(angle),center.y+radius*sin(angle)};
    }
    double u = 1 - t;
    return (BoardPoint) {u*u*p->start.x + 2*u*t*p->control.x + t*t*p->end.x,
                        u*u*p->start.y + 2*u*t*p->control.y + t*t*p->end.y};
}
static void prepare(void)
{
    for (unsigned m = 0; m < GAME_CHOICES; m++) {
        Move move=game_moves[m];
        Arc *arc = &arcs[m];
        arc->start=move_parameters[m][0]; arc->end=move_parameters[m][1];
        BoardPoint previous = board_evaluate(move.path, arc->start);
        for (unsigned step = 1; step <= ARC_STEPS; step++) {
            double t = arc->start + (arc->end - arc->start) * step / ARC_STEPS;
            BoardPoint point = board_evaluate(move.path, t);
            arc->length[step] = arc->length[step - 1] + hypot(point.x - previous.x, point.y - previous.y);
            previous = point;
        }
    }
}
BoardPoint board_move_position(unsigned move, double progress)
{
    g_return_val_if_fail(move < GAME_CHOICES, ((BoardPoint) {0,0}));
    if (g_once_init_enter(&initialized)) { prepare(); g_once_init_leave(&initialized, 1); }
    const Arc *arc = &arcs[move];
    double target = CLAMP(progress, 0, 1) * arc->length[ARC_STEPS];
    unsigned low = 1, high = ARC_STEPS;
    while (low < high) { unsigned middle = (low + high) / 2; if (arc->length[middle] < target) low = middle + 1; else high = middle; }
    double part = (target - arc->length[low - 1]) / (arc->length[low] - arc->length[low - 1]);
    double t = arc->start + (arc->end - arc->start) * (low - 1 + part) / ARC_STEPS;
    return board_evaluate(game_moves[move].path, t);
}
