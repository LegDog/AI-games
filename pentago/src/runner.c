// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <stdio.h>
#include <math.h>
static int controller(const char *name)
{
    if(g_str_equal(name,"random"))return PLAYER_RANDOM;
    if(g_str_equal(name,"heuristic"))return PLAYER_POSITIONAL;
    if(g_str_equal(name,"minimax"))return PLAYER_MINIMAX;
    if(g_str_equal(name,"alpha-beta"))return PLAYER_ALPHA_BETA;
    return -1;
}
int main(int argc,char **argv)
{
    gint games=10,seed=0,depth[2]={3,3},memory[2]={64,64};double seconds[2]={5,5};gint64 node_limit=0;char *names[2]={NULL,NULL};
    GOptionEntry options[]={
        {"games",'n',0,G_OPTION_ARG_INT,&games,"Number of games","N"},
        {"seed",0,0,G_OPTION_ARG_INT,&seed,"First seed","N"},
        {"first",0,0,G_OPTION_ARG_STRING,&names[0],"Black: random, heuristic, minimax, alpha-beta","NAME"},
        {"second",0,0,G_OPTION_ARG_STRING,&names[1],"White controller","NAME"},
        {"depth-first",0,0,G_OPTION_ARG_INT,&depth[0],"Black search depth (1..6)","N"},
        {"depth-second",0,0,G_OPTION_ARG_INT,&depth[1],"White search depth (1..6)","N"},
        {"time-first",0,0,G_OPTION_ARG_DOUBLE,&seconds[0],"Black seconds per decision, zero unlimited","SECONDS"},
        {"time-second",0,0,G_OPTION_ARG_DOUBLE,&seconds[1],"White seconds per decision, zero unlimited","SECONDS"},
        {"memory-first",0,0,G_OPTION_ARG_INT,&memory[0],"Black MiB per decision","MIB"},
        {"memory-second",0,0,G_OPTION_ARG_INT,&memory[1],"White MiB per decision","MIB"},
        {"node-limit",0,0,G_OPTION_ARG_INT64,&node_limit,"Deterministic per-decision node limit, zero unlimited","N"},{NULL}};
    g_autoptr(GOptionContext) context=g_option_context_new("— reproducible Pentago tournaments; CSV on stdout");g_option_context_add_main_entries(context,options,NULL);GError *error=NULL;
    if(!g_option_context_parse(context,&argc,&argv,&error)){g_printerr("%s\n",error->message);g_clear_error(&error);g_free(names[0]);g_free(names[1]);return 2;}
    if(!names[0])names[0]=g_strdup("heuristic");
    if(!names[1])names[1]=g_strdup("alpha-beta");
    gboolean valid=argc==1 && games>=1 && seed>=0 && (guint64)seed+games-1<=G_MAXINT && node_limit>=0;
    for(unsigned i=0;i<2;i++)valid&=controller(names[i])>=0 && depth[i]>=1 && depth[i]<=GAME_MAX_DEPTH && isfinite(seconds[i]) && seconds[i]>=0 && memory[i]>=1 && memory[i]<=4096;
    if(!valid){g_printerr("Invalid arguments. Use --help.\n");g_free(names[0]);g_free(names[1]);return 2;}
    printf("game,seed,first,second,depth_first,depth_second,time_first,time_second,memory_first,memory_second,node_limit,result,actions,black,white,sequence,nodes,leaves,cutoffs,elapsed_us,status_first,status_second,completed_first,completed_second,limited_decisions,peak_memory_bytes\n");
    for(int run=0;run<games;run++){
        GRand *randoms[]={g_rand_new_with_seed(seed+run),g_rand_new_with_seed((seed+run)^0x9e3779b9u)};Game game;game_init(&game);
        g_autoptr(GString) sequence=g_string_new(NULL);guint64 nodes=0,leaves=0,cutoffs=0;gsize peak=0;unsigned completed[2]={0},limited=0;
        const char *statuses[]={"not-used","not-used"};gint64 start=g_get_monotonic_time();
        while(!game_result(&game) && game.actions<GAME_MAX_ACTIONS){
            unsigned i=game.turn==1?0:1;GameMoves legal;game_legal_moves(&game,&legal);int chosen=-1;int values[GAME_CHOICES];
            int kind=controller(names[i]);
            if(kind==PLAYER_RANDOM)chosen=game_random_move(&legal,randoms[i]);
            else if(kind==PLAYER_POSITIONAL)chosen=game_positional_move(&game,randoms[i],values);
            else {
                SearchOptions config={depth[i],seconds[i],(gsize)memory[i]*1024*1024,(guint64)node_limit};SearchResult result;
                gboolean ok=minimax_evaluate(&game,kind==PLAYER_ALPHA_BETA,&config,NULL,&result);
                nodes+=result.nodes;leaves+=result.leaves;cutoffs+=result.cutoffs;peak=MAX(peak,result.peak_memory);completed[i]=result.completed;statuses[i]=search_status_name(result.status);limited+=result.status==SEARCH_LIMIT;
                if(!ok)break;
                chosen=search_choose(&result,randoms[i]);
            }
            if(chosen<0 || (unsigned)chosen>=legal.count || !game_play(&game,legal.moves[chosen])){g_printerr("Internal illegal move.\n");g_rand_free(randoms[0]);g_rand_free(randoms[1]);g_free(names[0]);g_free(names[1]);return 1;}
            char notation[16];game_move_label(legal.moves[chosen],notation);g_string_append_printf(sequence,"%s%s",sequence->len?" ":"",notation);
        }
        GameResult result=game_result(&game);
        printf("%s,%d,%s,%s,%d,%d,%.9g,%.9g,%d,%d,%" G_GINT64_FORMAT ",%s,%u,%u,%u,%s,%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT ",%" G_GUINT64_FORMAT ",%" G_GINT64_FORMAT ",%s,%s,%u,%u,%u,%zu\n",GAME_ID,seed+run,names[0],names[1],depth[0],depth[1],seconds[0],seconds[1],memory[0],memory[1],node_limit,result==GAME_BLACK?"black":result==GAME_WHITE?"white":result==GAME_DRAW?"draw":"incomplete",game.actions,game_count(game.black),game_count(game.white),sequence->str,nodes,leaves,cutoffs,g_get_monotonic_time()-start,statuses[0],statuses[1],completed[0],completed[1],limited,peak);
        g_rand_free(randoms[0]);g_rand_free(randoms[1]);
    }
    g_free(names[0]);g_free(names[1]);return 0;
}
