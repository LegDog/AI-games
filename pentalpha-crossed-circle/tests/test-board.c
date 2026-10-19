// SPDX-License-Identifier: GPL-3.0-or-later
#include "board.h"
#include <math.h>
static void geometry(void)
{
    for(unsigned m=0;m<GAME_CHOICES;m++) {
        Move move=game_moves[m];BoardPoint a=board_move_position(m,0),b=board_move_position(m,1);
        g_assert_cmpfloat(hypot(a.x-board_points[move.entry].x,a.y-board_points[move.entry].y),<,1e-6);
        g_assert_cmpfloat(hypot(b.x-board_points[move.destination].x,b.y-board_points[move.destination].y),<,1e-6);
        double nearest=1e9,total=0,maxstep=0;BoardPoint previous=a;
        for(unsigned j=1;j<=1000;j++) {
            BoardPoint point=board_move_position(m,j/1000.);
            nearest=MIN(nearest,hypot(point.x-board_points[move.middle].x,point.y-board_points[move.middle].y));
            double length=hypot(point.x-previous.x,point.y-previous.y);total+=length;maxstep=MAX(maxstep,length);previous=point;
            if(move.path==10)g_assert_cmpfloat(fabs(hypot(point.x-251,point.y-251)-218*.76),<,1e-6);
        }
        g_assert_cmpfloat(nearest,<,.3);g_assert_cmpfloat(maxstep,<,total/1000*1.02);
    }
}
int main(int argc,char **argv){g_test_init(&argc,&argv,NULL);g_test_add_func("/board/trajectories",geometry);return g_test_run();}
