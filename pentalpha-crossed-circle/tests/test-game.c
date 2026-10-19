// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"
static void topology(void)
{
    Game game;game_init(&game);MoveList all=game_legal_moves(&game);g_assert_cmpuint(all.count,==,120);
    gboolean pairs[36][36]={{0}};unsigned counts[11]={0};
    for(unsigned m=0;m<GAME_CHOICES;m++) {
        Move move=game_moves[m];g_assert_false(pairs[move.entry][move.destination]);pairs[move.entry][move.destination]=TRUE;counts[move.path]++;
        g_assert_true(game_play(&game,m));g_assert_cmpuint(game.occupied,==,POINT_BIT(move.destination));
        g_assert_true(game_undo(&game));g_assert_cmpuint(game.occupied,==,0);
        game.occupied=POINT_BIT(move.entry);g_assert_false(game_legal(&game,m));
        game.occupied=POINT_BIT(move.destination);g_assert_false(game_legal(&game,m));
        game.occupied=POINT_BIT(move.middle);game.moves=1;g_assert_true(game_legal(&game,m));
        game_init(&game);
        g_assert_cmpint(game_find_move(move.entry,move.destination),==,(int)m);
    }
    for(unsigned p=0;p<11;p++)g_assert_cmpuint(counts[p],==,p<5?10:p<10?6:40);
    g_assert_cmpint(game_find_move(5+19,5+1),>=,0); /* Circle crosses r0. */
    g_assert_cmpint(game_find_move(5+18,5+0),>=,0);
    g_assert_cmpint(game_find_move(5,35),>=,0); /* Three collinear points r0,i0,c. */
    g_assert_cmpint(game_find_move(5,26),==,-1); /* No switch from r0,i0 to i1. */
    game=(Game){.occupied=(POINT_BIT(36)-1)^POINT_BIT(0),.moves=35};g_assert_cmpint(game_result(&game),==,1);
    game.moves=34;game.occupied^=POINT_BIT(35);g_assert_cmpint(game_result(&game),==,2);
    game_init(&game);g_assert_cmpint(game_heuristic(&game,119),==,256+(int)game_legal_moves(&(Game){.occupied=POINT_BIT(game_moves[119].destination),.moves=1}).count);
}
static guint64 transform(guint64 mask,int sign,int k)
{
    guint64 out=0;for(int p=0;p<36;p++)if(mask&POINT_BIT(p)) {
        int q=p<5?(sign*p+k+10)%5:p<25?5+(sign*(p-5)+4*k+40)%20:p<35?25+(sign*(p-25)+2*k+20)%10:35;out|=POINT_BIT(q);
    }return out;
}
static void symmetry(void)
{
    for(int sign=-1;sign<=1;sign+=2)for(int k=0;k<5;k++)for(unsigned m=0;m<120;m++) {
        Move move=game_moves[m];guint64 a=transform(POINT_BIT(move.entry),sign,k),b=transform(POINT_BIT(move.middle),sign,k),c=transform(POINT_BIT(move.destination),sign,k);
        gboolean found=FALSE;for(unsigned n=0;n<120;n++)if(POINT_BIT(game_moves[n].entry)==a && POINT_BIT(game_moves[n].middle)==b && POINT_BIT(game_moves[n].destination)==c)found=TRUE;
        g_assert_true(found);
    }
    GRand *random=g_rand_new_with_seed(17);
    for(unsigned trial=0;trial<200;trial++) {
        guint64 mask=((guint64)g_rand_int(random)<<32|g_rand_int(random))&(POINT_BIT(36)-1);
        for(int sign=-1;sign<=1;sign+=2)for(int k=0;k<5;k++)g_assert_cmpuint(game_canonical(mask),==,game_canonical(transform(mask,sign,k)));
    }g_rand_free(random);
}
int main(int argc,char **argv){g_test_init(&argc,&argv,NULL);g_test_add_func("/game/topology-rules",topology);g_test_add_func("/game/symmetries",symmetry);return g_test_run();}
