// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
static int exact(const Game *g,unsigned depth)
{
    if(!depth || game_result(g)){PositionValue v;game_position(g,g->turn,&v);return v.total;}
    int best=G_MININT;guint64 legal=game_legal_moves(g);
    for(unsigned p=0;p<64;p++)if(legal&SQUARE_BIT(p)){Game next=*g;g_assert_true(game_play(&next,p));best=MAX(best,-exact(&next,depth-1));}
    return best;
}
static gboolean cancel(gpointer data){(void)data;return TRUE;}
static void minimax(void)
{
    Game g;game_init_variant(&g,3,TRUE);game_play(&g,1);SearchOptions options=search_defaults();options.seconds=0;options.depth=3;
    SearchResult r;g_assert_true(minimax_evaluate(&g,&options,NULL,&r));g_assert_cmpuint(r.completed,==,3);
    int best=G_MININT;guint64 choices=0;
    for(unsigned p=0;p<64;p++)if(r.legal&SQUARE_BIT(p)){Game next=g;game_play(&next,p);int value=-exact(&next,2);g_assert_cmpint(r.values[p],==,value);if(value>best){best=value;choices=0;}if(value==best)choices|=SQUARE_BIT(p);}
    g_assert_cmpuint(r.best,==,choices);
    options.node_limit=game_count(r.legal)+2;g_assert_true(minimax_evaluate(&g,&options,NULL,&r));g_assert_cmpint(r.status,==,SEARCH_LIMIT);g_assert_cmpuint(r.completed,==,1);
    options.node_limit=1;g_assert_false(minimax_evaluate(&g,&options,NULL,&r));g_assert_cmpuint(r.completed,==,0);g_assert_cmpuint(r.best,==,0);
}
static void limits(void)
{
    Game g;game_init(&g);SearchOptions o=search_defaults();SearchResult r;GRand *rng=g_rand_new_with_seed(1);SearchControl c={.cancelled=cancel};
    g_assert_false(minimax_evaluate(&g,&o,&c,&r));g_assert_cmpint(r.status,==,SEARCH_CANCELLED);
    g_assert_false(mcts_evaluate(&g,&o,&c,rng,&r));g_assert_cmpint(r.status,==,SEARCH_CANCELLED);
    o.memory_bytes=1;g_assert_false(minimax_evaluate(&g,&o,NULL,&r));g_assert_cmpint(r.status,==,SEARCH_LIMIT);g_assert_false(mcts_evaluate(&g,&o,NULL,rng,&r));
    o=search_defaults();o.seconds=1e-12;g_assert_false(mcts_evaluate(&g,&o,NULL,rng,&r));g_assert_cmpint(r.status,==,SEARCH_LIMIT);
    o.depth=0;g_assert_false(minimax_evaluate(&g,&o,NULL,&r));g_assert_cmpint(r.status,==,SEARCH_ERROR);g_rand_free(rng);
}
static void mcts(void)
{
    Game g;game_init_variant(&g,3,TRUE);game_play(&g,1);SearchOptions o=search_defaults();o.seconds=0;o.iterations=200;
    GRand *a=g_rand_new_with_seed(123),*b=g_rand_new_with_seed(123);SearchResult x,y;
    g_assert_true(mcts_evaluate(&g,&o,NULL,a,&x));g_assert_true(mcts_evaluate(&g,&o,NULL,b,&y));
    g_assert_cmpuint(x.best,==,y.best);g_assert_cmpmem(x.values,sizeof x.values,y.values,sizeof y.values);g_assert_cmpmem(x.visits,sizeof x.visits,y.visits,sizeof y.visits);
    unsigned visits=0;for(unsigned p=0;p<64;p++)visits+=x.visits[p];g_assert_cmpuint(visits,==,200);g_assert_cmpuint(x.visits[GAME_SWAP],>,0);
    g_assert_true(game_legal_moves(&g)&SQUARE_BIT(search_choose(&x,a)));g_assert_cmpuint(x.peak_memory,<=,o.memory_bytes);
    game_init_variant(&g,3,FALSE);g.red=SQUARE_BIT(0)|SQUARE_BIT(7)|SQUARE_BIT(2)|SQUARE_BIT(9);g.blue=SQUARE_BIT(1)|SQUARE_BIT(8)|SQUARE_BIT(15)|SQUARE_BIT(16);
    g_assert_cmpint(game_result(&g),==,0);g_assert_true(mcts_evaluate(&g,&o,NULL,a,&x));g_assert_cmpuint(x.best,==,SQUARE_BIT(14));g_assert_cmpint(x.values[14],==,10000);
    g_rand_free(a);g_rand_free(b);
}
int main(int argc,char **argv){g_test_init(&argc,&argv,NULL);g_test_add_func("/search/minimax-reference-completed-depth",minimax);g_test_add_func("/search/limits-cancellation",limits);g_test_add_func("/search/mcts-seed-swap-forced-win",mcts);return g_test_run();}
