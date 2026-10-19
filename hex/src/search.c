// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <math.h>
#include <string.h>
typedef struct {
    SearchOptions options;const SearchControl *control;SearchResult *result;
    gint64 start,last_progress;
} Search;
SearchOptions search_defaults(void){return (SearchOptions){3,1000,5,64u*1024*1024,0};}
const char *search_status_name(SearchStatus status)
{
    static const char *const names[]={"complete","limit-reached","cancelled","error"};return names[status];
}
static gboolean stopped(Search *s)
{
    if(s->control && s->control->cancelled && s->control->cancelled(s->control->data))s->result->status=SEARCH_CANCELLED;
    if(s->result->status!=SEARCH_COMPLETE)return TRUE;
    gint64 now=g_get_monotonic_time();
    if((s->options.node_limit && s->result->nodes>=s->options.node_limit) ||
       (s->options.seconds>0 && now-s->start>=s->options.seconds*1000000))s->result->status=SEARCH_LIMIT;
    if(s->control && s->control->progress && now-s->last_progress>=100000) {
        s->control->progress(s->result->completed,s->result->nodes,s->control->data);s->last_progress=now;
    }return s->result->status!=SEARCH_COMPLETE;
}
static gboolean initialize(Search *s,const Game *game,const SearchOptions *options,const SearchControl *control,SearchResult *result)
{
    memset(result,0,sizeof *result);for(unsigned p=0;p<64;p++)result->values[p]=G_MININT;
    *s=(Search){.options=options?*options:search_defaults(),.control=control,.result=result,.start=g_get_monotonic_time()};
    if(!isfinite(s->options.seconds) || s->options.seconds<0 || s->options.depth<1 || s->options.depth>GAME_MAX_DEPTH || !s->options.iterations || s->options.iterations>1000000)result->status=SEARCH_ERROR;
    result->legal=game_legal_moves(game);return !stopped(s);
}
static gboolean finish(Search *s)
{
    s->result->elapsed_us=g_get_monotonic_time()-s->start;
    if(s->control && s->control->progress)s->control->progress(s->result->completed,s->result->nodes,s->control->data);
    return s->result->status!=SEARCH_CANCELLED && s->result->status!=SEARCH_ERROR && s->result->completed>0;
}
static int negamax(Search *s,const Game *game,unsigned depth,int alpha,int beta)
{
    if(stopped(s))return 0;
    s->result->nodes++;
    if(game_result(game) || !depth){PositionValue value;game_position(game,game->turn,&value);return value.total;}
    guint64 legal=game_legal_moves(game);
    int best=G_MININT;
    for(unsigned p=0;p<64;p++) {
        if(!(legal&SQUARE_BIT(p)))continue;
        Game next=*game;game_play(&next,p);int score=-negamax(s,&next,depth-1,-beta,-alpha);
        if(s->result->status!=SEARCH_COMPLETE)return 0;
        best=MAX(best,score);alpha=MAX(alpha,score);if(alpha>=beta)return best;
    }return best;
}
gboolean minimax_evaluate(const Game *game,const SearchOptions *options,const SearchControl *control,SearchResult *result)
{
    Search s;gboolean ok=initialize(&s,game,options,control,result);result->requested=s.options.depth;
    if(!ok)return finish(&s);
    result->peak_memory=sizeof(Search)+sizeof(SearchResult)+128*(sizeof(Game)+128)+4096;
    if(s.options.memory_bytes<result->peak_memory){result->peak_memory=0;result->status=SEARCH_LIMIT;return finish(&s);}
    if(!result->legal || game_result(game))return finish(&s);
    for(unsigned depth=1;depth<=s.options.depth;depth++) {
        int values[64];for(unsigned p=0;p<64;p++)values[p]=G_MININT;
        int best=G_MININT;guint64 choices=0;
        for(unsigned p=0;p<64;p++)if(result->legal&SQUARE_BIT(p)) {
            Game next=*game;game_play(&next,p);values[p]=-negamax(&s,&next,depth-1,-200000,200000);
            if(result->status!=SEARCH_COMPLETE)break;
            if(values[p]>best){best=values[p];choices=0;}if(values[p]==best)choices|=SQUARE_BIT(p);
        }
        if(result->status!=SEARCH_COMPLETE)break;
        memcpy(result->values,values,sizeof values);result->best=choices;result->completed=depth;
    }
    return finish(&s);
}
typedef struct {
    Game game;guint64 untried;
    unsigned parent,first_child,next_sibling,move,visits;
    double reward;
} Node;
static Node new_node(Game game,unsigned parent,unsigned move)
{
    guint64 legal=game_legal_moves(&game);
    return (Node){.game=game,.untried=legal,.parent=parent,.move=move};
}
gboolean mcts_evaluate(const Game *game,const SearchOptions *options,const SearchControl *control,GRand *random,SearchResult *result)
{
    Search s;gboolean ok=initialize(&s,game,options,control,result);result->requested=s.options.iterations;
    if(!ok || !result->legal || game_result(game))return finish(&s);
    gsize overhead=sizeof(Search)+sizeof(SearchResult)+8192;
    if(s.options.memory_bytes<=overhead+2*sizeof(Node)){result->status=SEARCH_LIMIT;return finish(&s);}
    unsigned capacity=MIN((gsize)s.options.iterations+1,(s.options.memory_bytes-overhead)/sizeof(Node));
    Node *tree=g_try_malloc0_n(capacity,sizeof(Node));
    if(!tree){result->status=SEARCH_ERROR;return finish(&s);}
    result->peak_memory=overhead+capacity*sizeof(Node);tree[0]=new_node(*game,0,64);unsigned count=1;
    for(unsigned iteration=0;iteration<s.options.iterations;iteration++) {
        if(stopped(&s))break;
        result->nodes++;
        unsigned index=0;
        /* Selection uses root-perspective rewards and reverses exploitation
         * when it is the opponent's turn. Both placements and the pie-rule action are explored. */
        while(!tree[index].untried && tree[index].first_child) {
            unsigned chosen=0;double best=-G_MAXDOUBLE;
            for(unsigned child=tree[index].first_child;child;child=tree[child].next_sibling) {
                double mean=tree[child].reward/tree[child].visits;
                double score=(tree[index].game.turn==game->turn?mean:-mean)+sqrt(2*log((double)tree[index].visits)/tree[child].visits);
                if(score>best){best=score;chosen=child;}
            }index=chosen;
        }
        if(tree[index].untried) {
            if(count==capacity){result->status=SEARCH_LIMIT;break;}
            unsigned move=(unsigned)game_random_move(tree[index].untried,random);
            Game next=tree[index].game;if(!game_play(&next,move)){result->status=SEARCH_ERROR;break;}
            tree[index].untried&=~SQUARE_BIT(move);
            unsigned child=count++;tree[child]=new_node(next,index,move);
            tree[child].next_sibling=tree[index].first_child;tree[index].first_child=child;index=child;
        }
        Game rollout=tree[index].game;unsigned actions=0;
        while(!game_result(&rollout)) {
            if(stopped(&s))break;
            if(actions++>=GAME_MAX_ACTIONS || !game_play(&rollout,game_random_move(game_legal_moves(&rollout),random))){result->status=SEARCH_ERROR;break;}
            result->nodes++;
        }
        if(result->status!=SEARCH_COMPLETE)break;
        int outcome=game_result(&rollout);double reward=outcome==game->turn?1:-1;
        for(;;){tree[index].visits++;tree[index].reward+=reward;if(!index)break;index=tree[index].parent;}
        result->completed++;
    }
    result->tree_nodes=count;
    unsigned most=0;int best=G_MININT;
    for(unsigned child=tree[0].first_child;child;child=tree[child].next_sibling)if(tree[child].visits) {
        unsigned move=tree[child].move;if(move>=64)continue;
        result->visits[move]=tree[child].visits;
        int mean=(int)lround(tree[child].reward/tree[child].visits*10000);result->values[move]=mean;
        if(tree[child].visits>most || (tree[child].visits==most && mean>best)){most=tree[child].visits;best=mean;result->best=0;}
        if(tree[child].visits==most && mean==best)result->best|=SQUARE_BIT(move);
    }
    g_free(tree);return finish(&s);
}
int search_choose(const SearchResult *result,GRand *random)
{
    return result->best && result->status!=SEARCH_CANCELLED && result->status!=SEARCH_ERROR?game_random_move(result->best,random):-1;
}
