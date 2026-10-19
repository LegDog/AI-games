// SPDX-License-Identifier: GPL-3.0-or-later
#include "search-symmetry.h"
static void equivalence(void)
{
    g_autoptr(GRand) random=g_rand_new_with_seed(19);SearchLimits limits={0,16*1024*1024,0};
    guint64 prunes=0,base_nodes=0,variant_nodes=0;
    for(unsigned trial=0;trial<14;trial++) {
        Game game;game_init(&game);
        while(game.moves<trial*2 && !game_result(&game))game_play(&game,game_random_move(game_legal_moves(&game),random));
        if(game_result(&game))continue;
        for(unsigned depth=1;depth<=3;depth++) {
            SearchResult a,b;SymmetryStats stats;g_assert_true(search_evaluate(&game,depth,&limits,NULL,&a));
            g_assert_true(search_symmetry_evaluate(&game,depth,&limits,NULL,&b,&stats));
            g_assert_cmpint(a.status,==,b.status);g_assert_cmpuint(a.completed_depth,==,b.completed_depth);
            g_assert_cmpmem(a.values,sizeof a.values,b.values,sizeof b.values);
            g_assert_cmpuint(a.best.count,==,b.best.count);g_assert_cmpmem(a.best.items,a.best.count*sizeof(unsigned),b.best.items,b.best.count*sizeof(unsigned));
            g_assert_cmpuint(b.peak_memory,<=,limits.memory_bytes);prunes+=stats.sibling_prunes;base_nodes+=a.nodes;variant_nodes+=b.nodes;
        }
    }
    g_assert_cmpuint(prunes,>,0);g_assert_cmpuint(variant_nodes,<,base_nodes);
}
static gboolean cancelled(gpointer data){(void)data;return TRUE;}
static void limits_test(void)
{
    Game game;game_init(&game);SearchResult first,result;SymmetryStats stats;SearchLimits limits={0,1024*1024,0};
    g_assert_true(search_symmetry_evaluate(&game,1,&limits,NULL,&first,&stats));limits.node_limit=first.nodes+1;
    g_assert_true(search_symmetry_evaluate(&game,35,&limits,NULL,&result,&stats));g_assert_cmpuint(result.completed_depth,==,1);
    g_assert_cmpint(result.status,==,SEARCH_LIMIT);g_assert_cmpmem(first.values,sizeof first.values,result.values,sizeof result.values);
    limits=(SearchLimits){0,1,0};g_assert_false(search_symmetry_evaluate(&game,3,&limits,NULL,&result,&stats));g_assert_cmpint(result.status,==,SEARCH_LIMIT);
    limits=search_defaults();limits.seconds=1e-12;g_assert_false(search_symmetry_evaluate(&game,3,&limits,NULL,&result,&stats));g_assert_cmpint(result.status,==,SEARCH_LIMIT);
    SearchControl control={.cancelled=cancelled};g_assert_false(search_symmetry_evaluate(&game,3,NULL,&control,&result,&stats));g_assert_cmpint(result.status,==,SEARCH_CANCELLED);
}
int main(int argc,char **argv){g_test_init(&argc,&argv,NULL);g_test_add_func("/symmetry/equivalence",equivalence);g_test_add_func("/symmetry/limits",limits_test);return g_test_run();}
