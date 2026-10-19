// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
static gboolean cancelled(gpointer data){(void)data;return TRUE;}
static void decisions(void)
{
    Game g;game_init(&g,RUNNING,SESSION_SINGLE,5);game_opening(&g,6,1);game_roll(&g,2,3);GRand *rng=g_rand_new_with_seed(3);SearchOptions o=search_defaults();o.depth=1;o.iterations=12;o.seconds=0;
    for(PlayerKind p=PLAYER_RANDOM;p<=PLAYER_MCTS;p++) {
        SearchResult r={0};g_assert_true(search_decide(&g,p,&o,NULL,rng,&r));g_assert_true(r.choice.checker);Game next=g;g_assert_true(decision_apply(&next,&r.choice));g_assert_true(game_valid(&next));g_assert_cmpuint(r.peak_memory,<=,o.memory_bytes);search_result_clear(&r);
    }
    SearchResult r={0};SearchControl control={.cancelled=cancelled};g_assert_false(search_decide(&g,PLAYER_EXPECTI,&o,&control,rng,&r));g_assert_cmpint(r.status,==,SEARCH_CANCELLED);search_result_clear(&r);
    o.memory_bytes=1;g_assert_false(search_decide(&g,PLAYER_MCTS,&o,NULL,rng,&r));g_assert_cmpint(r.status,==,SEARCH_LIMIT);search_result_clear(&r);g_rand_free(rng);
}
static void seed_and_cube(void)
{
    Game g;game_init(&g,BACKGAMMON,SESSION_FIXED,5);g.phase=PRE_ROLL;SearchOptions o=search_defaults();o.iterations=20;o.depth=1;o.seconds=0;
    GRand *a=g_rand_new_with_seed(17),*b=g_rand_new_with_seed(17);SearchResult x={0},y={0};
    g_assert_true(search_decide(&g,PLAYER_MCTS,&o,NULL,a,&x));g_assert_true(search_decide(&g,PLAYER_MCTS,&o,NULL,b,&y));g_assert_true(decision_equal(&x.choice,&y.choice));g_assert_cmpuint(x.nodes,==,y.nodes);search_result_clear(&x);search_result_clear(&y);
    game_control(&g,ACTION_DOUBLE);g_assert_cmpint(g.actor,!=,g.active);g_assert_true(search_decide(&g,PLAYER_HEURISTIC,&o,NULL,a,&x));g_assert_true(x.choice.action==ACTION_TAKE || x.choice.action==ACTION_DROP);search_result_clear(&x);g_rand_free(a);g_rand_free(b);
}
/* Independent unpruned traversal uses all 36 ordered rolls, not the search's
 * 21 weighted outcomes or its final-position deduplication. */
static double reference(const Game *g,unsigned depth,int root)
{
    if(!depth || game_finished(g))return game_value(g,root);
    GArray *choices=game_choices(g,NULL);double best=g->actor==root?-2:2;
    if(g->phase==PLAY) {
        GArray *plans=game_turns(g,NULL);
        for(unsigned i=0;i<plans->len;i++){Game next=*g;game_play_plan(&next,&g_array_index(plans,TurnPlan,i));double value=reference(&next,depth-1,root);best=g->actor==root?MAX(best,value):MIN(best,value);}
        g_array_unref(plans);
    }else for(unsigned i=0;i<choices->len;i++) {
        Step choice=g_array_index(choices,Step,i);double value=0;
        if(choice.kind==ACTION_ROLL){for(unsigned a=1;a<=6;a++)for(unsigned b=1;b<=6;b++){Game next=*g;game_roll(&next,a,b);value+=reference(&next,depth,root)/36;}}
        else{Game next=*g;game_control(&next,choice.kind);value=reference(&next,depth,root);}
        best=g->actor==root?MAX(best,value):MIN(best,value);
    }
    g_array_unref(choices);return best;
}
static void exact_chance_and_partial(void)
{
    Game g;game_init(&g,BACKGAMMON,SESSION_FIXED,5);memset(g.point,0,sizeof g.point);g.off[0]=g.off[1]=14;g.point[2][0]=1;g.point[21][1]=1;g.phase=PRE_ROLL;
    SearchOptions o=search_defaults();o.seconds=0;o.depth=1;GRand *rng=g_rand_new_with_seed(4);SearchResult r={0};
    g_assert_true(search_decide(&g,PLAYER_EXPECTI,&o,NULL,rng,&r));double best=-2;
    for(unsigned i=0;i<r.candidates->len;i++) {
        CandidateValue *c=&g_array_index(r.candidates,CandidateValue,i);double expected=0;
        if(c->decision.action==ACTION_ROLL){for(unsigned a=1;a<=6;a++)for(unsigned b=1;b<=6;b++){Game next=g;game_roll(&next,a,b);expected+=reference(&next,1,g.actor)/36;}}
        else{Game next=g;game_control(&next,c->decision.action);expected=reference(&next,1,g.actor);}
        g_assert_cmpfloat_with_epsilon(c->value,expected,1e-10);best=MAX(best,expected);
    }
    g_assert_cmpfloat_with_epsilon(best,reference(&g,1,g.actor),1e-10);search_result_clear(&r);
    game_init(&g,RUNNING,SESSION_SINGLE,5);g.phase=PRE_ROLL;game_roll(&g,1,2);
    g_assert_true(search_decide(&g,PLAYER_EXPECTI,&o,NULL,rng,&r));guint64 first_nodes=r.nodes;GArray *values=g_array_ref(r.candidates);search_result_clear(&r);
    o.depth=2;o.node_limit=first_nodes+3;g_assert_true(search_decide(&g,PLAYER_EXPECTI,&o,NULL,rng,&r));g_assert_cmpint(r.status,==,SEARCH_LIMIT);g_assert_cmpuint(r.completed,==,1);
    g_assert_cmpuint(values->len,==,r.candidates->len);for(unsigned i=0;i<values->len;i++)g_assert_cmpfloat(g_array_index(values,CandidateValue,i).value,==,g_array_index(r.candidates,CandidateValue,i).value);
    g_array_unref(values);search_result_clear(&r);g_rand_free(rng);
}
int main(int argc,char **argv){g_test_init(&argc,&argv,NULL);g_test_add_func("/search/decisions-limits",decisions);g_test_add_func("/search/seed-cube-actor",seed_and_cube);g_test_add_func("/search/36-roll-reference-partial-depth",exact_chance_and_partial);return g_test_run();}
