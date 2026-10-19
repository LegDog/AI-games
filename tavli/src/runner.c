// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
static int variant_id(const char *name){for(unsigned i=0;i<VARIANT_COUNT;i++)if(!g_ascii_strcasecmp(name,variant_name(i)))return i;return -1;}
static int kind_id(const char *name)
{
    const char *names[]={"random","heuristic","expectiminimax","mcts"};for(unsigned i=0;i<4;i++)if(!g_strcmp0(name,names[i]))return PLAYER_RANDOM+i;return -1;
}
int main(int argc,char **argv)
{
    int games=10,seed=0,target=5,memory=64,depth[2]={2,2},iterations[2]={1000,1000},turn_limit=10000;
    double seconds=5;char *variant=NULL,*session=NULL,*players[2]={NULL,NULL};
    GOptionEntry entries[]={
        {"games",'n',0,G_OPTION_ARG_INT,&games,"Number of games or matches","N"},
        {"seed",0,0,G_OPTION_ARG_INT,&seed,"Initial seed","N"},
        {"variant",0,0,G_OPTION_ARG_STRING,&variant,"portes, plakoto, fevga, backgammon, running","NAME"},
        {"session",0,0,G_OPTION_ARG_STRING,&session,"single, fixed, traditional","NAME"},
        {"target",0,0,G_OPTION_ARG_INT,&target,"Match target, 1..25","N"},
        {"first",0,0,G_OPTION_ARG_STRING,&players[0],"random, heuristic, expectiminimax, mcts","NAME"},
        {"second",0,0,G_OPTION_ARG_STRING,&players[1],"Second controller","NAME"},
        {"depth-first",0,0,G_OPTION_ARG_INT,&depth[0],"First search depth, 1..6","N"},
        {"depth-second",0,0,G_OPTION_ARG_INT,&depth[1],"Second search depth","N"},
        {"iterations-first",0,0,G_OPTION_ARG_INT,&iterations[0],"First MCTS budget","N"},
        {"iterations-second",0,0,G_OPTION_ARG_INT,&iterations[1],"Second MCTS budget","N"},
        {"time-limit",0,0,G_OPTION_ARG_DOUBLE,&seconds,"Seconds per decision, zero unlimited","S"},
        {"memory-limit",0,0,G_OPTION_ARG_INT,&memory,"Search MiB","MIB"},
        {"turn-limit",0,0,G_OPTION_ARG_INT,&turn_limit,"Maximum turns per session","N"},{NULL}};
    GOptionContext *context=g_option_context_new("— Tavli CSV with replayable dice, checker and cube events");g_option_context_add_main_entries(context,entries,NULL);GError *error=NULL;
    gboolean parsed=g_option_context_parse(context,&argc,&argv,&error);g_option_context_free(context);
    if(!parsed){g_printerr("%s\n",error->message);g_clear_error(&error);goto invalid;}
    if(!variant)variant=g_strdup("portes");
    if(!session)session=g_strdup("single");
    if(!players[0])players[0]=g_strdup("heuristic");
    if(!players[1])players[1]=g_strdup("random");
    int v=variant_id(variant),mode=!strcmp(session,"single")?SESSION_SINGLE:!strcmp(session,"fixed")?SESSION_FIXED:!strcmp(session,"traditional")?SESSION_TRADITIONAL:-1;
    gboolean valid=argc==1 && games>0 && seed>=0 && (guint64)seed+games-1<=G_MAXINT && target>=1 && target<=25 && v>=0 && mode>=0 && turn_limit>0 && isfinite(seconds) && seconds>=0 && memory>=1 && memory<=4096;
    for(unsigned p=0;p<2;p++)valid&=kind_id(players[p])>=0 && depth[p]>=1 && depth[p]<=6 && iterations[p]>0 && iterations[p]<=1000000;
    if(!valid)goto invalid;
    puts("game,seed,variant,session,target,first,second,result,winner,score_first,score_second,games,turns,sequence,nodes,completed,limited_decisions,peak_memory_bytes,search_status,elapsed_us");
    for(int run=0;run<games;run++) {
        unsigned current_seed=seed+run;GRand *dice=g_rand_new_with_seed(current_seed),*random[2]={g_rand_new_with_seed(current_seed^0x9e3779b9u),g_rand_new_with_seed(current_seed^0x85ebca6bu)};
        Game g;game_init(&g,v,mode,target);GString *trace=g_string_new(NULL);guint64 nodes=0;gsize peak=0;unsigned completed=0,limited=0,total_turns=0,game_count=1;gint64 start=g_get_monotonic_time();gboolean failed=FALSE;const char *last_status="not-used";
        g_string_append_printf(trace,"B:%s",variant_name(g.variant));
        while(total_turns<(unsigned)turn_limit) {
            if(game_finished(&g)) {
                if(mode==SESSION_SINGLE || g.phase==MATCH_OVER)break;
                game_next(&g);game_count++;g_string_append_printf(trace,";B:%s",variant_name(g.variant));continue;
            }
            if(g.phase==OPENING || (g.phase==PRE_ROLL && !game_can_double(&g))) {
                unsigned a=g_rand_int_range(dice,1,7),b=g_rand_int_range(dice,1,7);gboolean opening=g.phase==OPENING;
                g_string_append_printf(trace,";%c:%u-%u",opening?'O':'R',a,b);if(opening)game_opening(&g,a,b);else game_roll(&g,a,b);continue;
            }
            int p=g.actor;SearchOptions options={depth[p],iterations[p],seconds,(gsize)memory*1024*1024,0};SearchResult result={0};
            if(!search_decide(&g,kind_id(players[p]),&options,NULL,random[p],&result)){last_status=search_status_name(result.status);limited+=result.status==SEARCH_LIMIT;nodes+=result.nodes;peak=MAX(peak,result.peak_memory);search_result_clear(&result);failed=TRUE;break;}
            last_status=search_status_name(result.status);nodes+=result.nodes;peak=MAX(peak,result.peak_memory);completed+=result.completed;limited+=result.status==SEARCH_LIMIT;
            Decision d=result.choice;search_result_clear(&result);
            if(d.checker) {
                g_string_append(trace,";M:");if(!d.plan.length)g_string_append(trace,"pass");
                for(unsigned i=0;i<d.plan.length;i++){char label[64];step_label(d.plan.steps[i],label,sizeof label);g_string_append_printf(trace,"%s%s",i?"+":"",label);}
                if(!game_play_plan(&g,&d.plan)){g_printerr("Illegal generated turn.\n");return 1;}total_turns++;
            }else if(d.action==ACTION_ROLL) {
                unsigned a=g_rand_int_range(dice,1,7),b=g_rand_int_range(dice,1,7);g_string_append_printf(trace,";R:%u-%u",a,b);game_roll(&g,a,b);
            }else{g_string_append(trace,d.action==ACTION_DOUBLE?";D":d.action==ACTION_TAKE?";T":";X");game_control(&g,d.action);}
            if(!game_valid(&g)){g_printerr("Invalid position.\n");return 1;}
        }
        gboolean done=!failed && game_finished(&g) && (mode==SESSION_SINGLE || g.phase==MATCH_OVER);
        printf("tavli,%u,%s,%s,%d,%s,%s,%s,%s,%u,%u,%u,%u,%s,%" G_GUINT64_FORMAT ",%u,%u,%zu,%s,%" G_GINT64_FORMAT "\n",current_seed,variant_name(v),session,target,players[0],players[1],done?"completed":"incomplete",done?(g.winner==0?"first":g.winner==1?"second":"draw"):"none",g.match.score[0],g.match.score[1],game_count,total_turns,trace->str,nodes,completed,limited,peak,last_status,g_get_monotonic_time()-start);
        g_string_free(trace,TRUE);g_rand_free(dice);g_rand_free(random[0]);g_rand_free(random[1]);
    }
    g_free(variant);g_free(session);g_free(players[0]);g_free(players[1]);return 0;
invalid:
    g_printerr("Invalid arguments. Use --help.\n");g_free(variant);g_free(session);g_free(players[0]);g_free(players[1]);return 2;
}
