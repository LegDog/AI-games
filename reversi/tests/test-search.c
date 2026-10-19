// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
static int exact(Game game,unsigned depth)
{
    if(!depth || game_result(&game)){PositionValue value;game_position(&game,game.turn,&value);return value.total;}
    guint64 legal=game_legal_moves(&game);if(!legal){game_play(&game,GAME_PASS);return -exact(game,depth);}
    int best=G_MININT;for(unsigned p=0;p<64;p++)if(legal&SQUARE_BIT(p)){Game next=game;game_play(&next,p);best=MAX(best,-exact(next,depth-1));}return best;
}
static void minimax_reference(void)
{
    SearchOptions options=search_defaults();options.seconds=0;g_autoptr(GRand) random=g_rand_new_with_seed(11);
    for(unsigned trial=0;trial<10;trial++) {
        Game game;game_init(&game);
        while(!game_result(&game) && game.moves<(trial?51+trial%7:0))game_play(&game,game_random_move(game_legal_moves(&game),random));
        if(game_result(&game) || !game_legal_moves(&game))continue;
        for(unsigned depth=1;depth<=4;depth++) {
            options.depth=depth;SearchResult result;g_assert_true(minimax_evaluate(&game,&options,NULL,&result));g_assert_cmpuint(result.completed,==,depth);
            int best=G_MININT;guint64 best_moves=0;
            for(unsigned p=0;p<64;p++)if(result.legal&SQUARE_BIT(p)) {
                Game child=game;game_play(&child,p);int value=-exact(child,depth-1);g_assert_cmpint(result.values[p],==,value);
                if(value>best){best=value;best_moves=0;}if(value==best)best_moves|=SQUARE_BIT(p);
            }
            g_assert_cmpuint(result.best,==,best_moves);g_assert_cmpuint(result.peak_memory,<=,options.memory_bytes);
        }
    }
}
static gboolean cancel(gpointer data){(void)data;return TRUE;}
static void resources(void)
{
    Game game;game_init(&game);SearchOptions options=search_defaults();options.seconds=0;options.depth=1;SearchResult first,result;
    g_assert_true(minimax_evaluate(&game,&options,NULL,&first));options.depth=10;options.node_limit=first.nodes+1;
    g_assert_true(minimax_evaluate(&game,&options,NULL,&result));g_assert_cmpint(result.status,==,SEARCH_LIMIT);g_assert_cmpuint(result.completed,==,1);g_assert_cmpmem(first.values,sizeof first.values,result.values,sizeof result.values);
    options=search_defaults();options.seconds=1e-12;g_assert_false(minimax_evaluate(&game,&options,NULL,&result));g_assert_cmpint(result.status,==,SEARCH_LIMIT);
    options=search_defaults();options.memory_bytes=1;g_assert_false(minimax_evaluate(&game,&options,NULL,&result));g_assert_cmpint(result.status,==,SEARCH_LIMIT);
    SearchControl control={.cancelled=cancel};g_assert_false(minimax_evaluate(&game,NULL,&control,&result));g_assert_cmpint(result.status,==,SEARCH_CANCELLED);
    g_autoptr(GRand) random=g_rand_new_with_seed(5);g_assert_false(mcts_evaluate(&game,NULL,&control,random,&result));g_assert_cmpint(result.status,==,SEARCH_CANCELLED);
    options=search_defaults();options.seconds=0;options.node_limit=200;
    g_assert_true(mcts_evaluate(&game,&options,NULL,random,&result));g_assert_cmpint(result.status,==,SEARCH_LIMIT);
    g_assert_cmpuint(result.completed,>,0);g_assert_cmpuint(result.best&~game_legal_moves(&game),==,0);
    options=search_defaults();options.memory_bytes=1;g_assert_false(mcts_evaluate(&game,&options,NULL,random,&result));g_assert_cmpint(result.status,==,SEARCH_LIMIT);
}
static void mcts_repeatable(void)
{
    Game game;game_init(&game);SearchOptions options=search_defaults();options.seconds=0;options.iterations=200;
    g_autoptr(GRand) first=g_rand_new_with_seed(7),second=g_rand_new_with_seed(7);SearchResult a,b;
    g_assert_true(mcts_evaluate(&game,&options,NULL,first,&a));g_assert_true(mcts_evaluate(&game,&options,NULL,second,&b));
    g_assert_cmpuint(a.completed,==,200);g_assert_cmpuint(a.nodes,==,b.nodes);g_assert_cmpuint(a.best,==,b.best);g_assert_cmpmem(a.visits,sizeof a.visits,b.visits,sizeof b.visits);
    g_assert_cmpuint(a.best&~game_legal_moves(&game),==,0);g_assert_cmpuint(a.peak_memory,<=,options.memory_bytes);
    game=(Game){.black=SQUARE_BIT(1),.white=SQUARE_BIT(0),.turn=-1};
    g_assert_true(mcts_evaluate(&game,&options,NULL,first,&a));g_assert_cmpuint(a.best,==,SQUARE_BIT(2));g_assert_cmpint(a.values[2],==,10000);
    options.depth=4;g_assert_true(minimax_evaluate(&game,&options,NULL,&a));g_assert_cmpuint(a.best,==,SQUARE_BIT(2));g_assert_cmpint(a.values[2],>,100000);
}
int main(int argc,char **argv){g_test_init(&argc,&argv,NULL);g_test_add_func("/search/minimax-exact",minimax_reference);g_test_add_func("/search/resources",resources);g_test_add_func("/search/mcts-repeatable",mcts_repeatable);return g_test_run();}
