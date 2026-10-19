// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include "graph.h"
#include "search-symmetry.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
int main(int argc,char **argv)
{
    gint runs=10,seed=0,depth=3,memory=256;double seconds=60;char *controller=NULL;
    GOptionEntry options[]={
        {"runs",'n',0,G_OPTION_ARG_INT,&runs,"Number of attempts","N"},
        {"seed",0,0,G_OPTION_ARG_INT,&seed,"First seed","N"},
        {"depth",0,0,G_OPTION_ARG_INT,&depth,"Search depth (1-35)","N"},
        {"time-limit",0,0,G_OPTION_ARG_DOUBLE,&seconds,"Seconds per decision (0 unlimited)","SECONDS"},
        {"memory-limit",0,0,G_OPTION_ARG_INT,&memory,"Search memory per decision","MIB"},
        {"controller",0,0,G_OPTION_ARG_STRING,&controller,"random, heuristic, search, connectivity, spanning-forest, gf2-forest, search-symmetry","NAME"},{NULL}};
    g_autoptr(GOptionContext) context=g_option_context_new("— Pentalfa Cruzado Circulado; CSV on stdout");
    g_option_context_add_main_entries(context,options,NULL);GError *error=NULL;
    if(!g_option_context_parse(context,&argc,&argv,&error)) {g_printerr("%s\n",error->message);g_clear_error(&error);g_free(controller);return 2;}
    if(!controller)controller=g_strdup("search");
    if(argc!=1 || runs<1 || seed<0 || (guint64)seed+runs-1>G_MAXINT || depth<1 || depth>35 || memory<1 || memory>4096 || !isfinite(seconds) || seconds<0 ||
       (!g_str_equal(controller,"random") && !g_str_equal(controller,"heuristic") && !g_str_equal(controller,"search") && !g_str_equal(controller,"connectivity") && !g_str_equal(controller,"spanning-forest") && !g_str_equal(controller,"gf2-forest") && !g_str_equal(controller,"search-symmetry"))) {
        g_printerr("Invalid arguments. Use --help.\n");g_free(controller);return 2;
    }
    printf("game,seed,controller,depth,time_limit,memory_limit_mib,result,pieces,sequence,nodes,elapsed_us,completed_depth_min,completed_depth_last,search_status,limited_decisions,peak_memory_bytes,proven_max_initial,empty_components,matrix_rank_initial,proof_status,optimum_reached,sibling_prunes,cache_hits,gf2_xor_operations\n");
    for(int run=0;run<runs;run++) {
        g_autoptr(GRand) random=g_rand_new_with_seed(seed+run);Game game;game_init(&game);
        g_autoptr(GString) sequence=g_string_new(NULL);SearchLimits limits={seconds,(gsize)memory*1024*1024,0};
        guint64 nodes=0;gsize peak=0;unsigned minimum=35,last=0,limited=0;const char *status="not-used";
        GraphResult plan={0};unsigned plan_index=0;int proven_max=-1,components=-1,matrix_rank=-1;
        guint64 sibling_prunes=0,cache_hits=0,xor_operations=0;const char *proof_status="not-computed";
        gboolean connectivity=g_str_equal(controller,"connectivity"),forest=g_str_equal(controller,"spanning-forest") || g_str_equal(controller,"gf2-forest"),symmetry=g_str_equal(controller,"search-symmetry");
        if(connectivity || forest || symmetry) {
            GraphAnalysis a;if(!graph_analyze(&game,&a)){g_free(controller);return 1;}
            proven_max=a.max_final_pieces;proof_status="proven";
        }
        gint64 start=g_get_monotonic_time();
        while(!game_result(&game)) {
            MoveList choices=game_legal_moves(&game);
            if(connectivity) {
                GraphResult result;
                if(!graph_connectivity(&game,NULL,&result)){proof_status="error";break;}
                choices=result.best;peak=MAX(peak,result.peak_memory);
            } else if(forest) {
                if(!plan.length) {
                    if(!graph_forest(&game,g_str_equal(controller,"gf2-forest"),random,NULL,&plan)){proof_status="error";break;}
                    matrix_rank=plan.analysis.matrix_rank;xor_operations=plan.xor_operations;peak=MAX(peak,plan.peak_memory);
                }
                if(plan_index>=plan.length || !game_legal(&game,plan.plan[plan_index])){proof_status="error";break;}
                choices=(MoveList){.count=1,.items={plan.plan[plan_index++]}};
            } else if(symmetry) {
                SearchResult result;SymmetryStats stats;gboolean ok=search_symmetry_evaluate(&game,depth,&limits,NULL,&result,&stats);
                nodes+=result.nodes;peak=MAX(peak,result.peak_memory);last=result.completed_depth;
                minimum=MIN(minimum,last);status=search_status_name(result.status);limited+=result.status==SEARCH_LIMIT;
                sibling_prunes+=stats.sibling_prunes;cache_hits+=stats.cache_hits;
                if(!ok)break;
                choices=result.best;
            } else if(g_str_equal(controller,"search")) {
                SearchResult result;gboolean ok=search_evaluate(&game,depth,&limits,NULL,&result);
                nodes+=result.nodes;peak=MAX(peak,result.peak_memory);last=result.completed_depth;
                minimum=MIN(minimum,last);status=search_status_name(result.status);limited+=result.status==SEARCH_LIMIT;
                if(!ok)break;
                choices=result.best;
            } else if(g_str_equal(controller,"heuristic")) {
                int best=-1;choices.count=0;
                for(unsigned m=0;m<GAME_CHOICES;m++)if(game_legal(&game,m)) {
                    int score=game_heuristic(&game,m);
                    if(score>best){best=score;choices.count=0;}
                    if(score==best)choices.items[choices.count++]=m;
                }
            }
            int move=game_random_move(choices,random);
            if(move<0 || !game_play(&game,move)){g_free(controller);return 1;}
            Move m=game_moves[move];g_string_append_printf(sequence,"%s%s>%s>%s@p%u",sequence->len?" ":"",game_labels[m.entry],game_labels[m.middle],game_labels[m.destination],m.path+1);
        }
        if(!g_str_equal(controller,"search") && !symmetry)minimum=0;
        if(proven_max>=0) {GraphAnalysis final;graph_analyze(&game,&final);components=final.component_count;}
        printf("%s,%d,%s,%d,%.9g,%d,%s,%u,%s,%" G_GUINT64_FORMAT ",%" G_GINT64_FORMAT ",%u,%u,%s,%u,%zu,%d,%d,%d,%s,%s,%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT "\n",GAME_ID,seed+run,controller,depth,seconds,memory,
            game_result(&game)==1?"solved":game_result(&game)==2?"stuck":"ongoing",game.moves,sequence->str,nodes,g_get_monotonic_time()-start,minimum,last,status,limited,peak,proven_max,components,matrix_rank,proof_status,proven_max>=0 && game.moves==(unsigned)proven_max && game_result(&game)?"yes":"no",sibling_prunes,cache_hits,xor_operations);
    }
    g_free(controller);return 0;
}
