// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
/* Independent enumeration, deliberately without caching or destination merging. */
static int reference(Game *game,unsigned depth)
{
    if(game->moves==35)return 35*256;
    unsigned legal=0;int best=-1;
    for(unsigned p=0;p<GAME_PATHS;p++) {
        unsigned count=game_path_lengths[p];
        for(unsigned j=0;j<(p==10?count:count-2);j++)for(unsigned reverse=0;reverse<2;reverse++) {
            unsigned a=game_lines[p][(j+(reverse?2:0))%count],c=game_lines[p][(j+(reverse?0:2))%count];
            if(game->occupied&(POINT_BIT(a)|POINT_BIT(c)))continue;
            legal++;
            if(depth) {Game child=*game;child.occupied|=POINT_BIT(c);child.moves++;best=MAX(best,reference(&child,depth-1));}
        }
    }
    return !legal || !depth ? (int)game->moves*256+(depth?0:(int)legal):best;
}
static void evaluations(void)
{
    SearchLimits limits={0,8*1024*1024,0};GRand *random=g_rand_new_with_seed(31);
    for(unsigned trial=0;trial<14;trial++) {
        Game game;game_init(&game);
        unsigned count=trial?12+trial:0;
        while(game.moves<count && !game_result(&game))game_play(&game,game_random_move(game_legal_moves(&game),random));
        if(game_result(&game))continue;
        for(unsigned depth=1;depth<=3;depth++) {
            SearchResult result;g_assert_true(search_evaluate(&game,depth,&limits,NULL,&result));g_assert_cmpint(result.status,==,SEARCH_COMPLETE);
            g_assert_cmpuint(result.completed_depth,==,MIN(depth,35-game.moves));
            int best=-1;
            for(unsigned i=0;i<result.legal.count;i++) {
                unsigned m=result.legal.items[i];Game child=game;game_play(&child,m);int value=reference(&child,depth-1);
                g_assert_cmpint(value,==,result.values[m]);best=MAX(best,value);
            }
            for(unsigned i=0;i<result.legal.count;i++){unsigned m=result.legal.items[i];g_assert_cmpint(move_list_contains(&result.best,m),==,result.values[m]==best);}
            g_assert_cmpuint(result.peak_memory,<=,limits.memory_bytes);
        }
    }g_rand_free(random);
}
static gboolean cancelled(gpointer data){(void)data;return TRUE;}
static void limits_test(void)
{
    Game game;game_init(&game);SearchResult first,limited;SearchLimits limits={0,1024*1024,0};
    g_assert_true(search_evaluate(&game,1,&limits,NULL,&first));limits.node_limit=first.nodes+1;
    g_assert_true(search_evaluate(&game,35,&limits,NULL,&limited));g_assert_cmpint(limited.status,==,SEARCH_LIMIT);g_assert_cmpuint(limited.completed_depth,==,1);
    for(unsigned m=0;m<120;m++)g_assert_cmpint(first.values[m],==,limited.values[m]);
    SearchLimits memory_limit={0,256*1024,0};SearchResult memory_result;
    g_assert_true(search_evaluate(&game,35,&memory_limit,NULL,&memory_result));
    g_assert_cmpint(memory_result.status,==,SEARCH_LIMIT);g_assert_cmpuint(memory_result.completed_depth,>=,1);
    g_assert_cmpuint(memory_result.completed_depth,<,35);g_assert_cmpuint(memory_result.peak_memory,<=,memory_limit.memory_bytes);
    limits.node_limit=1;g_assert_false(search_evaluate(&game,35,&limits,NULL,&limited));g_assert_cmpuint(limited.best.count,==,0);
    limits=(SearchLimits){0,1,0};g_assert_false(search_evaluate(&game,35,&limits,NULL,&limited));g_assert_cmpint(limited.status,==,SEARCH_LIMIT);
    limits=search_defaults();limits.seconds=1e-12;g_assert_false(search_evaluate(&game,3,&limits,NULL,&limited));g_assert_cmpint(limited.status,==,SEARCH_LIMIT);
    SearchControl control={.cancelled=cancelled};g_assert_false(search_evaluate(&game,3,NULL,&control,&limited));g_assert_cmpint(limited.status,==,SEARCH_CANCELLED);
    g_assert_cmpint(search_choose(&limited,NULL),==,-1);
}
int main(int argc,char **argv){g_test_init(&argc,&argv,NULL);g_test_add_func("/search/reference",evaluations);g_test_add_func("/search/limits",limits_test);return g_test_run();}
