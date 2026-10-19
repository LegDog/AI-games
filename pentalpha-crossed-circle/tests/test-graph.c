// SPDX-License-Identifier: GPL-3.0-or-later
#include "graph.h"
/* Full legal sequence enumeration on small empty subsets; no graph bound. */
static unsigned exhaustive(Game *game)
{
    unsigned best=game->moves;MoveList legal=game_legal_moves(game);guint64 seen=0;
    for (unsigned i=0;i<legal.count;i++) {
        unsigned m=legal.items[i];guint64 bit=POINT_BIT(game_moves[m].destination);if (seen&bit) continue;seen|=bit;
        Game child=*game;g_assert_true(game_play(&child,m));best=MAX(best,exhaustive(&child));
    }
    return best;
}
static void structure(void)
{
    Game game;game_init(&game);GraphAnalysis analysis;g_assert_true(graph_analyze(&game,&analysis));
    g_assert_cmpuint(analysis.component_count,==,2);g_assert_cmpuint(analysis.edge_count,==,60);g_assert_cmpuint(analysis.max_final_pieces,==,34);
    g_assert_cmpint(analysis.matrix_rank,==,-1);
    guint64 center=POINT_BIT(35);for(unsigned r=0;r<20;r+=2)center|=POINT_BIT(5+r);
    g_assert_cmpuint(analysis.components[1],==,center);g_assert_cmpuint(game_count(analysis.components[0]),==,25);
}
static void verify_plan(const Game *game,const GraphResult *result)
{
    Game replay=*game;
    for(unsigned i=0;i<result->length;i++)g_assert_true(game_play(&replay,result->plan[i]));
    g_assert_cmpuint(replay.moves,==,result->analysis.max_final_pieces);
    g_assert_cmpuint(replay.moves,==,exhaustive(&replay)); /* Terminal exact check. */
    g_assert_cmpint(game_result(&replay),!=,0);
    if(result->analysis.matrix_rank>=0)g_assert_cmpuint(result->length,==,(unsigned)result->analysis.matrix_rank);
}
static void forests(void)
{
    Game game;game_init(&game);
    for(unsigned gf2=0;gf2<2;gf2++)for(unsigned seed=0;seed<20;seed++) {
        g_autoptr(GRand) random=g_rand_new_with_seed(seed),again=g_rand_new_with_seed(seed);GraphResult a,b;
        g_assert_true(graph_forest(&game,gf2,random,NULL,&a));g_assert_true(graph_forest(&game,gf2,again,NULL,&b));
        g_assert_cmpuint(a.length,==,34);verify_plan(&game,&a);
        g_assert_cmpmem(a.plan,a.length*sizeof(unsigned),b.plan,b.length*sizeof(unsigned));
        if(gf2){g_assert_cmpint(a.analysis.matrix_rank,==,34);g_assert_cmpuint(a.xor_operations,>,0);}
    }
}
static void subsets(void)
{
    g_autoptr(GRand) random=g_rand_new_with_seed(39);guint64 full=POINT_BIT(36)-1;
    for(unsigned trial=0;trial<180;trial++) {
        guint64 empty=0;unsigned count=1+trial%10;
        while(game_count(empty)<count)empty|=POINT_BIT(g_rand_int_range(random,0,36));
        Game game={.occupied=full^empty,.moves=36-count};GraphAnalysis a;g_assert_true(graph_analyze(&game,&a));
        g_assert_cmpuint(a.max_final_pieces,==,exhaustive(&game));
        GraphResult result;g_assert_true(graph_connectivity(&game,NULL,&result));
        for(unsigned i=0;i<result.best.count;i++) {
            Game child=game;g_assert_true(game_play(&child,result.best.items[i]));g_assert_cmpuint(exhaustive(&child),==,a.max_final_pieces);
        }
        MoveList legal=game_legal_moves(&game);
        for(unsigned i=0;i<legal.count;i++) {
            unsigned m=legal.items[i];Game child=game;game_play(&child,m);
            g_assert_cmpint(result.values[m],==,(int)exhaustive(&child));
        }
        for(unsigned gf2=0;gf2<2;gf2++){g_assert_true(graph_forest(&game,gf2,random,NULL,&result));verify_plan(&game,&result);}
    }
}
static gboolean cancel(gpointer data){unsigned *calls=data;return ++*calls>=3;}
static void cancellation(void)
{
    Game game;game_init(&game);GraphResult result;unsigned calls=0;SearchControl control={.cancelled=cancel,.data=&calls};
    g_assert_false(graph_connectivity(&game,&control,&result));g_assert_cmpint(result.status,==,GRAPH_CANCELLED);g_assert_cmpuint(result.best.count,==,0);
    for(unsigned gf2=0;gf2<2;gf2++){
        calls=0;g_autoptr(GRand) random=g_rand_new_with_seed(0);g_assert_false(graph_forest(&game,gf2,random,&control,&result));
        g_assert_cmpint(result.status,==,GRAPH_CANCELLED);g_assert_cmpuint(result.length,==,0);
    }
}
int main(int argc,char **argv){g_test_init(&argc,&argv,NULL);g_test_add_func("/graph/structure",structure);g_test_add_func("/graph/forests",forests);g_test_add_func("/graph/exhaustive-subsets",subsets);g_test_add_func("/graph/cancellation",cancellation);return g_test_run();}
