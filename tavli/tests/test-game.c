// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"
static void empty(Game *g,Variant v)
{
    game_init(g,v,SESSION_SINGLE,5);memset(g->point,0,sizeof g->point);memset(g->reserve,0,sizeof g->reserve);g->phase=PRE_ROLL;g->active=g->actor=0;
}
static Step step(unsigned from,unsigned to,unsigned die){return (Step){ACTION_STEP,from,to,die};}
static void setup_rules(void)
{
    for(Variant v=PORTES;v<VARIANT_COUNT;v++) {
        Game g;g_assert_true(game_init(&g,v,SESSION_FIXED,5));g_assert_true(game_valid(&g));g_assert_cmpint(g.phase,==,OPENING);
        g_assert_true(game_opening(&g,3,3));g_assert_cmpint(g.phase,==,OPENING);game_opening(&g,6,1);
        g_assert_cmpint(g.active,==,0);g_assert_cmpint(g.phase,==,v==BACKGAMMON?PLAY:PRE_ROLL);
        if(v!=BACKGAMMON)game_roll(&g,2,2);
        unsigned count=0;for(unsigned d=1;d<=6;d++)count+=g.remaining[d];g_assert_cmpuint(count,==,v==RUNNING || v==BACKGAMMON?2:4);
        g_assert_cmpint(game_point_at(&g,0,0),==,23);g_assert_cmpint(game_point_at(&g,1,0),==,v==FEVGA?11:0);
    }
}
static void running(void)
{
    Game g;game_init(&g,RUNNING,SESSION_SINGLE,5);game_opening(&g,6,1);game_roll(&g,1,2);
    g_assert_true(game_step(&g,step(SOURCE_RESERVE,23,1)));g_assert_true(game_step(&g,step(SOURCE_RESERVE,22,2)));g_assert_true(game_end_turn(&g));
    g_assert_cmpuint(g.reserve[0],==,13);g_assert_true(game_valid(&g));
    game_roll(&g,1,1);g_assert_true(game_step(&g,step(SOURCE_RESERVE,0,1)));g_assert_true(game_step(&g,step(SOURCE_RESERVE,0,1)));g_assert_true(game_end_turn(&g));g_assert_cmpuint(g.point[0][1],==,2);
    empty(&g,RUNNING);g.reserve[0]=14;g.reserve[1]=15;g.point[0][0]=1;game_roll(&g,1,6);
    g_assert_true(game_step(&g,step(0,DEST_OFF,1)));g_assert_cmpuint(g.off[0],==,1);g_assert_cmpuint(g.reserve[0],==,14);
    empty(&g,RUNNING);g.point[1][0]=1;g.reserve[0]=14;g.reserve[1]=15;game_roll(&g,3,3);g_assert_false(game_step(&g,step(1,DEST_OFF,3)));
    empty(&g,RUNNING);g.point[23][0]=15;g.point[22][1]=15;game_roll(&g,1,1);GArray *turns=game_turns(&g,NULL);g_assert_cmpuint(turns->len,==,1);g_assert_cmpuint(g_array_index(turns,TurnPlan,0).length,==,0);g_assert_true(game_play_plan(&g,&g_array_index(turns,TurnPlan,0)));g_array_unref(turns);
}
static void running_clockwise(void)
{
    Game g;empty(&g,RUNNING);g.active=g.actor=1;g.reserve[0]=15;g.reserve[1]=15;
    for(unsigned i=0;i<24;i++)g_assert_cmpint(game_point_at(&g,1,i),==,(int)i);
    game_roll(&g,6,6);g_assert_true(game_step(&g,step(SOURCE_RESERVE,5,6)));g_assert_true(game_step(&g,step(SOURCE_RESERVE,5,6)));g_assert_true(game_valid(&g));
    empty(&g,RUNNING);g.active=g.actor=1;g.reserve[0]=15;g.reserve[1]=14;g.point[11][1]=1;
    game_roll(&g,1,1);g_assert_false(game_step(&g,step(11,10,1)));g_assert_true(game_step(&g,step(11,12,1)));
    empty(&g,RUNNING);g.active=g.actor=1;g.reserve[0]=14;g.point[5][0]=1;g.reserve[1]=15;
    game_roll(&g,6,6);g_assert_false(game_step(&g,step(SOURCE_RESERVE,5,6)));
    empty(&g,RUNNING);g.active=g.actor=1;g.reserve[0]=15;g.reserve[1]=14;g.point[23][1]=1;
    game_roll(&g,1,2);g_assert_false(game_step(&g,step(23,DEST_OFF,2)));g_assert_true(game_step(&g,step(23,DEST_OFF,1)));g_assert_cmpuint(g.off[1],==,1);g_assert_cmpuint(g.reserve[1],==,14);
    empty(&g,RUNNING);g.active=g.actor=1;g.reserve[0]=15;g.off[1]=14;g.point[23][1]=1;
    game_roll(&g,1,1);g_assert_true(game_step(&g,step(23,DEST_OFF,1)));g_assert_cmpint(g.winner,==,1);g_assert_cmpuint(g.award,==,1);
}
static void dice_order(void)
{
    Game g;empty(&g,RUNNING);g.point[5][0]=1;g.off[0]=14;g.point[4][1]=15;game_roll(&g,1,2);
    GArray *list=game_turns(&g,NULL);g_assert_cmpuint(list->len,>,0);
    for(unsigned i=0;i<list->len;i++){TurnPlan t=g_array_index(list,TurnPlan,i);g_assert_cmpuint(t.length,==,2);g_assert_cmpuint(t.steps[0].die,==,2);}
    g_array_unref(list);
    empty(&g,RUNNING);g.point[0][0]=1;g.off[0]=14;g.point[1][1]=15;game_roll(&g,1,2);list=game_turns(&g,NULL);g_assert_cmpuint(g_array_index(list,TurnPlan,0).length,==,1);g_assert_true(game_play_plan(&g,&g_array_index(list,TurnPlan,0)));g_assert_cmpuint(g.award,==,1);g_array_unref(list);
}
static void hits_and_home(void)
{
    Game g;empty(&g,BACKGAMMON);g.point[8][0]=1;g.off[0]=14;g.point[7][1]=1;g.point[0][1]=14;game_roll(&g,1,2);
    g_assert_true(game_step(&g,step(8,7,1)));g_assert_cmpuint(g.bar[1],==,1);g_assert_cmpuint(g.point[7][1],==,0);g_assert_true(game_valid(&g));
    g_assert_true(game_step(&g,step(7,5,2)));game_end_turn(&g);game_roll(&g,1,2);g_assert_false(game_step(&g,step(0,1,1)));g_assert_true(game_step(&g,step(SOURCE_BAR,0,1)));g_assert_cmpuint(g.bar[1],==,0);
    empty(&g,BACKGAMMON);g.point[1][0]=1;g.point[5][0]=1;g.off[0]=13;g.point[23][1]=15;game_roll(&g,3,3);g_assert_false(game_step(&g,step(1,DEST_OFF,3)));
    empty(&g,BACKGAMMON);g.point[1][0]=1;g.off[0]=14;g.point[23][1]=15;game_roll(&g,3,3);g_assert_true(game_step(&g,step(1,DEST_OFF,3)));g_assert_cmpuint(g.award,==,2);
    empty(&g,BACKGAMMON);g.point[0][0]=1;g.off[0]=14;g.point[5][1]=15;game_roll(&g,1,1);g_assert_true(game_step(&g,step(0,DEST_OFF,1)));g_assert_cmpuint(g.award,==,3);
}
static void pinning(void)
{
    Game g;empty(&g,PLAKOTO);g.point[8][0]=1;g.point[10][0]=14;g.point[7][1]=1;g.point[0][1]=14;game_roll(&g,1,2);
    g_assert_true(game_step(&g,step(8,7,1)));g_assert_cmpint(g.pinned[7],==,1);g_assert_cmpuint(g.bar[1],==,0);
    g_assert_true(game_step(&g,step(7,5,2)));g_assert_cmpint(g.pinned[7],==,-1);g_assert_true(game_valid(&g));
    empty(&g,PLAKOTO);g.point[0][0]=1;g.point[2][0]=14;g.point[0][1]=1;g.point[23][1]=14;g.pinned[0]=1;g.phase=PRE_ROLL;game_roll(&g,1,1);
    g_assert_true(game_step(&g,step(2,1,1)));g_assert_cmpint(g.winner,==,0);g_assert_cmpint(g.reason,==,REASON_MOTHER);g_assert_cmpuint(g.award,==,2);
}
static void fevga(void)
{
    Game g;game_init(&g,FEVGA,SESSION_SINGLE,5);game_opening(&g,6,1);game_roll(&g,6,6);
    GArray *list=game_turns(&g,NULL);g_assert_cmpuint(list->len,==,1);g_assert_cmpuint(g_array_index(list,TurnPlan,0).length,==,1);g_assert_true(game_play_plan(&g,&g_array_index(list,TurnPlan,0)));g_array_unref(list);
    g_assert_cmpuint(g.point[17][0],==,1);g_assert_cmpuint(g.point[23][0],==,14);g_assert_false(g.escaped[0]);
    empty(&g,FEVGA);g.point[23][0]=14;g.point[10][0]=1;g.point[11][1]=15;g.escaped[0]=TRUE;game_roll(&g,1,1);g_assert_true(game_step(&g,step(23,22,1)));
    empty(&g,FEVGA);for(unsigned i=0;i<5;i++)g.point[game_point_at(&g,0,i)][0]=1;g.point[19][0]=2;g.point[game_point_at(&g,0,6)][0]=9;g.point[11][1]=15;g.escaped[0]=TRUE;game_roll(&g,1,1);
    g_assert_false(game_step(&g,step(19,18,1)));
}
static void cube_matches(void)
{
    Game g;game_init(&g,BACKGAMMON,SESSION_FIXED,5);game_opening(&g,6,1);g_assert_false(game_can_double(&g));
    TurnPlan p;GArray *list=game_turns(&g,NULL);p=g_array_index(list,TurnPlan,0);game_play_plan(&g,&p);g_array_unref(list);
    g_assert_true(game_can_double(&g));int owner=g.active;game_control(&g,ACTION_DOUBLE);g_assert_cmpint(g.actor,==,1-owner);game_control(&g,ACTION_TAKE);g_assert_cmpint(g.cube_owner,==,1-owner);g_assert_cmpuint(g.cube,==,2);g_assert_false(game_can_double(&g));
    g.active=g.actor=1-owner;game_control(&g,ACTION_DOUBLE);game_control(&g,ACTION_DROP);g_assert_cmpuint(g.award,==,2);g_assert_cmpuint(g.match.score[1-owner],==,2);
    g.match.score[0]=4;g.match.score[1]=2;g.phase=GAME_OVER;g.winner=0;game_next(&g);g_assert_true(g.crawford);game_opening(&g,6,1);g_assert_false(game_can_double(&g));
    g.phase=GAME_OVER;g.winner=1;game_next(&g);g_assert_false(g.crawford);g.phase=PRE_ROLL;g.active=g.actor=0;g_assert_false(game_can_double(&g));g.active=g.actor=1;g_assert_true(game_can_double(&g));
    game_init(&g,PORTES,SESSION_TRADITIONAL,5);g.phase=GAME_OVER;g.winner=1;g.match.score[1]=1;game_next(&g);g_assert_cmpint(g.variant,==,PLAKOTO);g_assert_cmpint(g.active,==,1);g_assert_cmpint(g.phase,==,PRE_ROLL);
}
static void mothers_and_primes(void)
{
    Game g;empty(&g,PLAKOTO);g.point[23][0]=1;g.point[10][0]=14;g.point[23][1]=1;g.point[0][1]=14;g.pinned[23]=0;
    game_roll(&g,1,2);g_assert_true(game_step(&g,step(10,9,1)));g_assert_false(game_finished(&g));
    /* The second mother is pinned before the opponent has emptied its own head. */
    g.point[9][0]=0;g.point[0][0]=1;g.point[0][1]=1;g.point[1][1]=13;g.pinned[0]=1;
    g_assert_true(game_step(&g,step(10,8,2)));g_assert_true(game_finished(&g));g_assert_cmpint(g.winner,==,-1);g_assert_cmpuint(g.award,==,0);g_assert_cmpint(g.reason,==,REASON_DRAW);
    empty(&g,FEVGA);for(unsigned q=14;q<=19;q++)g.point[q][0]=1;g.point[23][0]=9;g.point[20][1]=15;g.escaped[0]=TRUE;
    g_assert_true(game_trapping_prime(&g,0));game_roll(&g,1,2);GArray *list=game_turns(&g,NULL);g_assert_nonnull(list);g_assert_cmpuint(list->len,>,0);
    for(unsigned i=0;i<list->len;i++){Game next=g;g_assert_true(game_play_plan(&next,&g_array_index(list,TurnPlan,i)));g_assert_false(game_trapping_prime(&next,0));}
    g_array_unref(list);RuleControl control={.memory_bytes=4096};g_assert_null(game_turns(&g,&control));
}
static void random_play(void)
{
    GRand *random=g_rand_new_with_seed(123);
    for(Variant v=PORTES;v<VARIANT_COUNT;v++)for(unsigned run=0;run<5;run++) {
        Game g;game_init(&g,v,SESSION_SINGLE,5);
        for(unsigned turn=0;turn<2000 && !game_finished(&g);turn++) {
            if(g.phase==OPENING){game_opening(&g,g_rand_int_range(random,1,7),g_rand_int_range(random,1,7));continue;}
            if(g.phase==PRE_ROLL)game_roll(&g,g_rand_int_range(random,1,7),g_rand_int_range(random,1,7));
            GArray *list=game_turns(&g,NULL);g_assert_nonnull(list);g_assert_cmpuint(list->len,>,0);TurnPlan t=g_array_index(list,TurnPlan,g_rand_int_range(random,0,list->len));
            Game prefix=g;for(unsigned i=0;i<t.length;i++)g_assert_true(game_step(&prefix,t.steps[i]));
            g_assert_true(game_play_plan(&g,&t));g_assert_true(game_valid(&g));g_assert_cmpmem(g.point,sizeof g.point,prefix.point,sizeof prefix.point);g_array_unref(list);
        }g_assert_true(game_finished(&g));
    }g_rand_free(random);
}
int main(int argc,char **argv)
{
    g_test_init(&argc,&argv,NULL);g_test_add_func("/rules/setup-opening",setup_rules);g_test_add_func("/rules/running",running);g_test_add_func("/rules/running-clockwise",running_clockwise);g_test_add_func("/rules/dice-order",dice_order);g_test_add_func("/rules/hits-home",hits_and_home);g_test_add_func("/rules/pins",pinning);g_test_add_func("/rules/fevga",fevga);g_test_add_func("/rules/cube-matches",cube_matches);g_test_add_func("/rules/mothers-prime-memory",mothers_and_primes);g_test_add_func("/rules/random-legal-replay",random_play);return g_test_run();
}
