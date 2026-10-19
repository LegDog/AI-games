// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
/* Exhaustive reference in root-player perspective, without negamax/pruning. */
static int exact(const Game *g,unsigned depth,int player)
{
    if(!depth || game_result(g)){PositionValue v;game_position(g,player,&v);return v.total;}
    GameMoves legal;game_legal_moves(g,&legal);int best=g->turn==player?G_MININT:G_MAXINT;
    for(unsigned i=0;i<legal.count;i++){Game next=*g;g_assert_true(game_play(&next,legal.moves[i]));int value=exact(&next,depth-1,player);best=g->turn==player?MAX(best,value):MIN(best,value);}return best;
}
static gboolean cancel(gpointer data){(void)data;return TRUE;}
static gboolean cancel_later(gpointer data){unsigned *checks=data;return ++*checks>400;}
static void compare(const Game *g,unsigned depth,gboolean reference,gboolean *pruned)
{
    SearchOptions o=search_defaults();o.seconds=0;o.depth=depth;SearchResult a,b;
    g_assert_true(minimax_evaluate(g,FALSE,&o,NULL,&a));g_assert_true(minimax_evaluate(g,TRUE,&o,NULL,&b));
    g_assert_cmpuint(a.completed,==,depth);g_assert_cmpuint(b.completed,==,depth);g_assert_cmpuint(a.cutoffs,==,0);g_assert_cmpuint(b.nodes,<=,a.nodes);
    if(b.nodes<a.nodes)*pruned=TRUE;
    g_test_message("depth %u: plain %" G_GUINT64_FORMAT " nodes; alpha-beta %" G_GUINT64_FORMAT " nodes, %" G_GUINT64_FORMAT " cutoffs",depth,a.nodes,b.nodes,b.cutoffs);
    int best=G_MININT;for(unsigned i=0;i<a.legal.count;i++){
        g_assert_cmpint(a.values[i],==,b.values[i]);g_assert_cmpint(a.best[i],==,b.best[i]);best=MAX(best,a.values[i]);
        if(reference){Game next=*g;game_play(&next,a.legal.moves[i]);g_assert_cmpint(a.values[i],==,exact(&next,depth-1,g->turn));}
    }
    for(unsigned i=0;i<a.legal.count;i++)g_assert_cmpint(a.best[i],==,a.values[i]==best);
    GRand *x=g_rand_new_with_seed(42),*y=g_rand_new_with_seed(42);g_assert_cmpint(search_choose(&a,x),==,search_choose(&b,y));g_rand_free(x);g_rand_free(y);
}
static void searches(void)
{
    Game g;game_init(&g);gboolean pruned=FALSE;compare(&g,1,TRUE,&pruned);compare(&g,2,TRUE,&pruned);
    /* A fixed dense ongoing position keeps depth-three reference inexpensive. */
    g.black=G_GUINT64_CONSTANT(0x520329b4a);g.white=G_GUINT64_CONSTANT(0x89d4c4405);g.turn=1;
    g_assert_cmpint(game_result(&g),==,GAME_ONGOING);compare(&g,3,TRUE,&pruned);g_assert_true(pruned);
    g.black=15;g.white=SQUARE_BIT(30)|SQUARE_BIT(32)|SQUARE_BIT(34);g.turn=1;SearchOptions o=search_defaults();o.depth=1;o.seconds=0;SearchResult r;
    g_assert_true(minimax_evaluate(&g,TRUE,&o,NULL,&r));GRand *rng=g_rand_new_with_seed(3);int chosen=search_choose(&r,rng);g_assert_cmpint(r.values[chosen],==,GAME_WIN_SCORE);g_rand_free(rng);
}
static void limits(void)
{
    Game g;game_init(&g);SearchOptions o=search_defaults();SearchResult r;SearchControl c={.cancelled=cancel};
    g_assert_false(minimax_evaluate(&g,TRUE,&o,&c,&r));g_assert_cmpint(r.status,==,SEARCH_CANCELLED);g_assert_cmpuint(r.completed,==,0);
    o.memory_bytes=1;g_assert_false(minimax_evaluate(&g,FALSE,&o,NULL,&r));g_assert_cmpint(r.status,==,SEARCH_LIMIT);
    o=search_defaults();o.seconds=0;o.node_limit=290;
    for(unsigned mode=0;mode<2;mode++){
        g_assert_true(minimax_evaluate(&g,mode,&o,NULL,&r));g_assert_cmpint(r.status,==,SEARCH_LIMIT);g_assert_cmpuint(r.completed,==,1);g_assert_cmpuint(r.nodes,==,290);
        SearchOptions one=o;one.depth=1;one.node_limit=0;SearchResult baseline;g_assert_true(minimax_evaluate(&g,mode,&one,NULL,&baseline));g_assert_cmpmem(r.values,sizeof r.values,baseline.values,sizeof baseline.values);
    }
    o.node_limit=1;g_assert_false(minimax_evaluate(&g,TRUE,&o,NULL,&r));g_assert_cmpuint(r.completed,==,0);GRand *rng=g_rand_new();g_assert_cmpint(search_choose(&r,rng),==,-1);g_rand_free(rng);
    o=search_defaults();o.seconds=1e-12;g_assert_false(minimax_evaluate(&g,TRUE,&o,NULL,&r));g_assert_cmpint(r.status,==,SEARCH_LIMIT);
    unsigned checks=0;SearchControl later={.cancelled=cancel_later,.data=&checks};o=search_defaults();o.seconds=0;
    g_assert_false(minimax_evaluate(&g,TRUE,&o,&later,&r));g_assert_cmpint(r.status,==,SEARCH_CANCELLED);g_assert_cmpuint(r.completed,==,1);
    rng=g_rand_new();g_assert_cmpint(search_choose(&r,rng),==,-1);g_rand_free(rng);
    o=search_defaults();o.depth=0;g_assert_false(minimax_evaluate(&g,TRUE,&o,NULL,&r));g_assert_cmpint(r.status,==,SEARCH_ERROR);
}
int main(int argc,char **argv){g_test_init(&argc,&argv,NULL);g_test_add_func("/search/plain-alpha-beta-reference",searches);g_test_add_func("/search/limits-cancellation-completed-iteration",limits);return g_test_run();}
