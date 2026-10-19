// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
static int controller(const char *name)
{
    if(g_str_equal(name,"random"))return PLAYER_RANDOM;
    if(g_str_equal(name,"positional"))return PLAYER_POSITIONAL;
    if(g_str_equal(name,"minimax"))return PLAYER_MINIMAX;
    if(g_str_equal(name,"mcts"))return PLAYER_MCTS;
    return -1;
}
int main(int argc,char **argv)
{
    gint games=10,seed=0,depth[2]={4,4},iterations[2]={1000,1000},memory=64;double seconds=5;char *names[2]={NULL,NULL};
    GOptionEntry options[]={
        {"games",'n',0,G_OPTION_ARG_INT,&games,"Number of games","N"},
        {"seed",0,0,G_OPTION_ARG_INT,&seed,"First seed","N"},
        {"first",0,0,G_OPTION_ARG_STRING,&names[0],"Black: random, positional, minimax, mcts","NAME"},
        {"second",0,0,G_OPTION_ARG_STRING,&names[1],"White controller","NAME"},
        {"depth-first",0,0,G_OPTION_ARG_INT,&depth[0],"Black Minimax depth","N"},
        {"depth-second",0,0,G_OPTION_ARG_INT,&depth[1],"White Minimax depth","N"},
        {"iterations-first",0,0,G_OPTION_ARG_INT,&iterations[0],"Black MCTS iterations","N"},
        {"iterations-second",0,0,G_OPTION_ARG_INT,&iterations[1],"White MCTS iterations","N"},
        {"time-limit",0,0,G_OPTION_ARG_DOUBLE,&seconds,"Seconds per decision, zero unlimited","SECONDS"},
        {"memory-limit",0,0,G_OPTION_ARG_INT,&memory,"MiB per decision","MIB"},{NULL}};
    g_autoptr(GOptionContext) context=g_option_context_new("— reproducible Reversi tournaments; CSV on stdout");g_option_context_add_main_entries(context,options,NULL);GError *error=NULL;
    if(!g_option_context_parse(context,&argc,&argv,&error)){g_printerr("%s\n",error->message);g_clear_error(&error);g_free(names[0]);g_free(names[1]);return 2;}
    if(!names[0])names[0]=g_strdup("positional");
    if(!names[1])names[1]=g_strdup("minimax");
    gboolean valid=argc==1 && games>=1 && seed>=0 && (guint64)seed+games-1<=G_MAXINT && isfinite(seconds) && seconds>=0 && memory>=1 && memory<=4096;
    for(unsigned i=0;i<2;i++)valid&=controller(names[i])>=0 && depth[i]>=1 && depth[i]<=GAME_MAX_DEPTH && iterations[i]>=1 && iterations[i]<=1000000;
    if(!valid){g_printerr("Invalid arguments. Use --help.\n");g_free(names[0]);g_free(names[1]);return 2;}
    printf("game,seed,first,second,depth_first,depth_second,iterations_first,iterations_second,result,moves,passes,black,white,sequence,nodes,elapsed_us,status_first,status_second,completed_first,completed_second,limited_decisions,peak_memory_bytes\n");
    for(int run=0;run<games;run++) {
        GRand *randoms[]={g_rand_new_with_seed(seed+run),g_rand_new_with_seed((seed+run)^0x9e3779b9u)};Game game;game_init(&game);
        g_autoptr(GString) sequence=g_string_new(NULL);guint64 nodes=0;gsize peak=0;unsigned completed[2]={0},limited=0,actions=0;
        const char *statuses[]={"not-used","not-used"};gint64 start=g_get_monotonic_time();
        while(!game_result(&game) && actions<GAME_MAX_ACTIONS) {
            unsigned i=game.turn==1?0:1;guint64 legal=game_legal_moves(&game);int move=GAME_PASS;int values[64];
            if(legal) {
                int kind=controller(names[i]);
                if(kind==PLAYER_RANDOM)move=game_random_move(legal,randoms[i]);
                else if(kind==PLAYER_POSITIONAL)move=game_positional_move(&game,randoms[i],values);
                else {
                    SearchOptions config={depth[i],iterations[i],seconds,(gsize)memory*1024*1024,0};SearchResult result;
                    gboolean ok=kind==PLAYER_MINIMAX?minimax_evaluate(&game,&config,NULL,&result):mcts_evaluate(&game,&config,NULL,randoms[i],&result);
                    nodes+=result.nodes;peak=MAX(peak,result.peak_memory);completed[i]=result.completed;statuses[i]=search_status_name(result.status);limited+=result.status==SEARCH_LIMIT;
                    if(!ok)break;
                    move=search_choose(&result,randoms[i]);
                }
            }
            if(move<0 || !game_play(&game,move)){g_printerr("Internal illegal move.\n");g_rand_free(randoms[0]);g_rand_free(randoms[1]);g_free(names[0]);g_free(names[1]);return 1;}
            actions++;g_string_append_printf(sequence,"%s%s",sequence->len?" ":"",move==GAME_PASS?"pass":game_labels[move]);
        }
        int result=game_result(&game);
        printf("%s,%d,%s,%s,%d,%d,%d,%d,%s,%u,%u,%u,%u,%s,%" G_GUINT64_FORMAT ",%" G_GINT64_FORMAT ",%s,%s,%u,%u,%u,%zu\n",GAME_ID,seed+run,names[0],names[1],depth[0],depth[1],iterations[0],iterations[1],result==1?"black":result==-1?"white":result==2?"draw":"ongoing",game.moves,game.passes,game_count(game.black),game_count(game.white),sequence->str,nodes,g_get_monotonic_time()-start,statuses[0],statuses[1],completed[0],completed[1],limited,peak);
        g_rand_free(randoms[0]);g_rand_free(randoms[1]);
    }
    g_free(names[0]);g_free(names[1]);return 0;
}
