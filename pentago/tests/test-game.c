// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"
#include <string.h>
/* Deliberately coordinate-based reference: no bitboard rotation or line masks. */
static int reference_result(const Game *g)
{
    int board[6][6];for(unsigned r=0;r<6;r++)for(unsigned c=0;c<6;c++)board[r][c]=g->black&SQUARE_BIT(r*6+c)?1:g->white&SQUARE_BIT(r*6+c)?-1:0;
    gboolean wins[2]={FALSE,FALSE};unsigned filled=0;
    for(int r=0;r<6;r++)for(int c=0;c<6;c++){
        if(!board[r][c])continue;
        filled++;
        for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
            if(!dy && !dx)continue;
            int length=0;
            for(int y=r,x=c;y>=0 && y<6 && x>=0 && x<6 && board[y][x]==board[r][c];y+=dy,x+=dx)length++;
            if(length>=5)wins[board[r][c]==1?0:1]=TRUE;
        }
    }
    return wins[0] && wins[1]?GAME_DRAW:wins[0]?GAME_BLACK:wins[1]?GAME_WHITE:filled==36?GAME_DRAW:GAME_ONGOING;
}
static guint64 reference_rotation(guint64 bits,unsigned q,unsigned d)
{
    gboolean old[3][3],next[3][3]={0};unsigned top=q/2*3,left=q%2*3;guint64 result=bits;
    for(unsigned y=0;y<3;y++)for(unsigned x=0;x<3;x++){unsigned p=(top+y)*6+left+x;old[y][x]=(bits&SQUARE_BIT(p))!=0;result&=~SQUARE_BIT(p);}
    for(unsigned y=0;y<3;y++)for(unsigned x=0;x<3;x++)if(d)next[2-x][y]=old[y][x];else next[x][2-y]=old[y][x];
    for(unsigned y=0;y<3;y++)for(unsigned x=0;x<3;x++)if(next[y][x])result|=SQUARE_BIT((top+y)*6+left+x);
    return result;
}
static void rotations(void)
{
    GRand *rng=g_rand_new_with_seed(97);
    for(unsigned n=0;n<500;n++){
        guint64 bits=(((guint64)g_rand_int(rng)<<32)|g_rand_int(rng))&GAME_BOARD_MASK;
        for(unsigned q=0;q<4;q++)for(unsigned d=0;d<2;d++){
            guint64 got=game_rotate(bits,q,d);g_assert_cmpuint(got,==,reference_rotation(bits,q,d));g_assert_cmpuint(game_count(got),==,game_count(bits));
            g_assert_cmpuint(game_rotate(got,q,!d),==,bits);
            guint64 four=bits;for(unsigned k=0;k<4;k++)four=game_rotate(four,q,d);g_assert_cmpuint(four,==,bits);
        }
    }
    g_assert_cmpint(game_rotated_cell(0,0,0),==,2);g_assert_cmpint(game_rotated_cell(0,0,1),==,12);g_assert_cmpint(game_rotated_cell(3,0,0),==,3);
    g_rand_free(rng);
}
static void rules(void)
{
    Game g;game_init(&g);GameMoves legal;game_legal_moves(&g,&legal);g_assert_cmpuint(legal.count,==,288);
    for(unsigned i=0;i<legal.count;i++){
        char label[16];GameMove parsed;game_move_label(legal.moves[i],label);g_assert_true(game_parse_move(label,&parsed));g_assert_true(game_move_equal(parsed,legal.moves[i]));
        Game next=g;g_assert_true(game_play(&next,parsed));g_assert_cmpuint(game_count(next.black),==,1);g_assert_cmpint(next.turn,==,-1);
    }
    const GameMove bad[]={{-1,0,0},{36,0,0},{0,-1,0},{0,4,0},{0,0,2},{0,0,-1}};
    for(unsigned i=0;i<G_N_ELEMENTS(bad);i++){Game before=g;g_assert_false(game_play(&g,bad[i]));g_assert_cmpmem(&g,sizeof g,&before,sizeof before);}
    const char *invalid[]={"","a1","a1:win:extra","g1:A:cw","a7:A:cw","a1:E:cw","a1:A:left","a1:A:cw extra"};
    GameMove parsed;for(unsigned i=0;i<G_N_ELEMENTS(invalid);i++)g_assert_false(game_parse_move(invalid[i],&parsed));
    g.black=15;g.white=SQUARE_BIT(30)|SQUARE_BIT(32)|SQUARE_BIT(34);g.turn=1;
    g_assert_false(game_play(&g,(GameMove){4,0,0}));g_assert_true(game_play(&g,(GameMove){4,-1,0}));g_assert_cmpint(game_result(&g),==,GAME_BLACK);
    g_assert_cmpuint(game_winning_path(&g,1),==,31);game_legal_moves(&g,&legal);g_assert_cmpuint(legal.count,==,0);
    g_assert_false(game_play(&g,(GameMove){5,0,0}));
    g.black=63;g_assert_cmpuint(game_winning_path(&g,1),==,63);
    /* Four directional lines and no wrapping at the right edge. */
    const int starts[]={0,0,0,5},steps[]={1,6,7,5};
    for(unsigned d=0;d<4;d++){game_init(&g);for(int i=0;i<5;i++)g.black|=SQUARE_BIT(starts[d]+steps[d]*i);g_assert_cmpint(game_result(&g),==,GAME_BLACK);PositionValue v;game_position(&g,-1,&v);g_assert_cmpint(v.total,==,-GAME_WIN_SCORE);}
    game_init(&g);g.black=SQUARE_BIT(4)|SQUARE_BIT(5)|SQUARE_BIT(6)|SQUARE_BIT(7)|SQUARE_BIT(8);g_assert_cmpint(game_result(&g),==,GAME_ONGOING);
}
static void independent(void)
{
    GRand *rng=g_rand_new_with_seed(1234);gboolean simultaneous=FALSE,opponent=FALSE,full_draw=FALSE;
    for(unsigned n=0;n<8000;n++){
        Game g;game_init(&g);for(unsigned p=0;p<36;p++){unsigned v=g_rand_int_range(rng,0,n%2?2:3);if(v==0)g.black|=SQUARE_BIT(p);else if(v==1)g.white|=SQUARE_BIT(p);}
        g_assert_cmpint(game_result(&g),==,reference_result(&g));
        if((g.black|g.white)==GAME_BOARD_MASK && game_result(&g)==GAME_DRAW && !game_winning_path(&g,1) && !game_winning_path(&g,-1))full_draw=TRUE;
        GameMoves legal;game_legal_moves(&g,&legal);
        for(unsigned k=0;k<MIN(legal.count,8);k++){
            unsigned i=g_rand_int_range(rng,0,legal.count);GameMove m=legal.moves[i];Game next=g;g_assert_true(game_play(&next,m));
            guint64 b=g.black|SQUARE_BIT(m.cell),w=g.white;if(m.quadrant>=0){b=reference_rotation(b,m.quadrant,m.direction);w=reference_rotation(w,m.quadrant,m.direction);}
            g_assert_cmpuint(next.black,==,b);g_assert_cmpuint(next.white,==,w);g_assert_cmpint(game_result(&next),==,reference_result(&next));
            if(m.quadrant>=0 && game_winning_path(&next,1) && game_winning_path(&next,-1))simultaneous=TRUE;
            if(game_result(&next)==GAME_WHITE)opponent=TRUE;
        }
    }
    g_assert_true(simultaneous);g_assert_true(opponent);g_assert_true(full_draw);g_rand_free(rng);
}
static void evaluation(void)
{
    Game g;game_init(&g);g.black=SQUARE_BIT(0);PositionValue a,b;game_position(&g,1,&a);game_position(&g,-1,&b);g_assert_cmpint(a.total,==,-b.total);g_assert_cmpint(a.total,>,0);
    g.white=SQUARE_BIT(35);game_position(&g,1,&a);g_assert_cmpint(a.total,==,0);
}
int main(int argc,char **argv)
{
    g_test_init(&argc,&argv,NULL);g_test_add_func("/game/rotation-reference",rotations);g_test_add_func("/game/rules-input-terminal",rules);g_test_add_func("/game/independent-result-successors",independent);g_test_add_func("/game/evaluation",evaluation);return g_test_run();
}
