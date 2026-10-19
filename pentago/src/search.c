// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <math.h>
#include <string.h>
typedef struct {
    SearchOptions options;const SearchControl *control;SearchResult *result;
    gint64 start,last_progress;gboolean pruning;
} Search;
SearchOptions search_defaults(void){return (SearchOptions){3,5,64u*1024*1024,0};}
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
    if(s->control && s->control->progress && now-s->last_progress>=100000){s->control->progress(s->result->completed,s->result->nodes,s->control->data);s->last_progress=now;}
    return s->result->status!=SEARCH_COMPLETE;
}
static int negamax(Search *s,const Game *g,unsigned depth,int alpha,int beta)
{
    if(stopped(s))return 0;
    s->result->nodes++;
    if(game_result(g) || !depth){PositionValue v;game_position(g,g->turn,&v);s->result->leaves++;return v.total;}
    GameMoves legal;game_legal_moves(g,&legal);int best=-GAME_WIN_SCORE-1;
    for(unsigned i=0;i<legal.count;i++){
        Game next=*g;game_apply_legal(&next,legal.moves[i]);int value=-negamax(s,&next,depth-1,-beta,-alpha);
        if(s->result->status!=SEARCH_COMPLETE)return 0;
        best=MAX(best,value);
        if(s->pruning){alpha=MAX(alpha,value);if(alpha>=beta){s->result->cutoffs++;break;}}
    }return best;
}
gboolean minimax_evaluate(const Game *g,gboolean pruning,const SearchOptions *options,const SearchControl *control,SearchResult *r)
{
    memset(r,0,sizeof *r);for(unsigned i=0;i<GAME_CHOICES;i++)r->values[i]=G_MININT;
    Search s={.options=options?*options:search_defaults(),.control=control,.result=r,.start=g_get_monotonic_time(),.pruning=pruning};
    r->requested=s.options.depth;
    if(!isfinite(s.options.seconds) || s.options.seconds<0 || s.options.depth<1 || s.options.depth>GAME_MAX_DEPTH)r->status=SEARCH_ERROR;
    /* Conservative bound for explicit move arrays, recursion frames and root copies.
     * This is accounted working memory, not process RSS. No search heap/cache. */
    gsize required=sizeof(Search)+sizeof(SearchResult)+GAME_MAX_DEPTH*(sizeof(GameMoves)+sizeof(Game)+4096)+8192;
    if(r->status==SEARCH_COMPLETE && s.options.memory_bytes<required)r->status=SEARCH_LIMIT;
    if(r->status==SEARCH_COMPLETE)r->peak_memory=required;
    if(!stopped(&s))game_legal_moves(g,&r->legal);
    for(unsigned depth=1;r->legal.count && depth<=s.options.depth && !stopped(&s);depth++){
        int values[GAME_CHOICES],best=-GAME_WIN_SCORE-1;
        for(unsigned i=0;i<r->legal.count;i++){
            Game next=*g;game_apply_legal(&next,r->legal.moves[i]);
            /* Full window at every root action: values and all ties are exact. */
            values[i]=-negamax(&s,&next,depth-1,-GAME_WIN_SCORE-1,GAME_WIN_SCORE+1);
            if(r->status!=SEARCH_COMPLETE)break;
            best=MAX(best,values[i]);
        }
        if(r->status!=SEARCH_COMPLETE)break;
        for(unsigned i=0;i<r->legal.count;i++){r->values[i]=values[i];r->best[i]=values[i]==best;}
        r->completed=depth;
    }
    r->elapsed_us=g_get_monotonic_time()-s.start;
    if(control && control->progress)control->progress(r->completed,r->nodes,control->data);
    return r->status!=SEARCH_CANCELLED && r->status!=SEARCH_ERROR && r->completed>0;
}
int search_choose(const SearchResult *r,GRand *random)
{
    if(r->status==SEARCH_CANCELLED || r->status==SEARCH_ERROR || !r->completed)return -1;
    int chosen=-1;unsigned ties=0;
    for(unsigned i=0;i<r->legal.count;i++)if(r->best[i] && g_rand_int_range(random,0,++ties)==0)chosen=i;
    return chosen;
}
