// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <math.h>
#include <string.h>
typedef struct { guint64 key; int value; guint16 depth,epoch; } Entry;
typedef struct {
    Entry *table; gsize capacity,used;
    SearchLimits limits; const SearchControl *control; SearchResult *result;
    gint64 start,last_progress; unsigned epoch;
} Search;
SearchLimits search_defaults(void) { return (SearchLimits){60,256u*1024*1024,0}; }
const char *search_status_name(SearchStatus status)
{
    static const char *const names[]={"complete","limit-reached","cancelled","error"};return names[status];
}
static guint64 hash(guint64 key)
{
    key^=key>>30;key*=G_GUINT64_CONSTANT(0xbf58476d1ce4e5b9);key^=key>>27;
    key*=G_GUINT64_CONSTANT(0x94d049bb133111eb);return key^(key>>31);
}
static gboolean stopped(Search *s)
{
    if (s->control && s->control->cancelled && s->control->cancelled(s->control->data)) s->result->status=SEARCH_CANCELLED;
    if (s->result->status!=SEARCH_COMPLETE) return TRUE;
    gint64 now=g_get_monotonic_time();
    if ((s->limits.node_limit && s->result->nodes>=s->limits.node_limit) ||
        (s->limits.seconds>0 && now-s->start>=s->limits.seconds*1000000)) s->result->status=SEARCH_LIMIT;
    if (s->control && s->control->progress && now-s->last_progress>=100000) {
        s->control->progress(s->result->nodes,s->result->completed_depth,now-s->start,s->result->peak_memory,s->control->data);
        s->last_progress=now;
    }
    return s->result->status!=SEARCH_COMPLETE;
}
static Entry *lookup(Search *s,guint64 key,unsigned depth)
{
    gsize slot=hash(key^((guint64)depth<<40))&(s->capacity-1);
    while (s->table[slot].epoch==s->epoch && (s->table[slot].key!=key || s->table[slot].depth!=depth)) slot=(slot+1)&(s->capacity-1);
    return &s->table[slot];
}
static int visit(Search *s,guint64 occupied,unsigned pieces,unsigned depth)
{
    if (stopped(s)) return 0;
    s->result->nodes++;
    if (pieces==GAME_MAX_DEPTH) return pieces*256;
    Game game={.occupied=occupied,.moves=pieces};MoveList legal=game_legal_moves(&game);
    if (!legal.count || !depth) return pieces*256+(depth?0:legal.count);
    guint64 key=game_canonical(occupied);Entry *entry=lookup(s,key,depth);
    if (entry->epoch==s->epoch) return entry->value;
    guint64 seen=0;int best=-1;
    /* Equal destinations lead to exactly the same occupancy. */
    for (unsigned i=0;i<legal.count;i++) {
        unsigned destination=game_moves[legal.items[i]].destination;
        guint64 bit=POINT_BIT(destination);if (seen&bit) continue;seen|=bit;
        best=MAX(best,visit(s,occupied|bit,pieces+1,depth-1));
        if (s->result->status!=SEARCH_COMPLETE) return 0;
        if (best==GAME_MAX_DEPTH*256) break;
    }
    entry=lookup(s,key,depth); /* Descendants may have occupied the old empty slot. */
    if (entry->epoch!=s->epoch) {
        if (s->used>=s->capacity*7/10) { s->result->status=SEARCH_LIMIT;return 0; }
        s->used++;
    }
    *entry=(Entry){key,best,depth,s->epoch};return best;
}
gboolean search_evaluate(const Game *game,unsigned depth,const SearchLimits *limits,const SearchControl *control,SearchResult *result)
{
    memset(result,0,sizeof *result);for(unsigned m=0;m<GAME_CHOICES;m++) result->values[m]=-1;
    result->requested_depth=CLAMP(depth,1,GAME_MAX_DEPTH);result->legal=game_legal_moves(game);
    Search s={.limits=limits?*limits:search_defaults(),.control=control,.result=result,.start=g_get_monotonic_time()};
    gsize overhead=sizeof(Search)+sizeof(SearchResult)+GAME_MAX_DEPTH*(sizeof(Game)+sizeof(MoveList)+512)+sizeof(guint64)*10*5*256;
    if (!isfinite(s.limits.seconds) || s.limits.seconds<0) {result->status=SEARCH_ERROR;goto done;}
    if (stopped(&s)) goto done;
    if (!result->legal.count) goto done;
    if (s.limits.memory_bytes<=overhead+16*sizeof(Entry)) {result->status=SEARCH_LIMIT;goto done;}
    s.capacity=16;
    while (s.capacity<=G_MAXSIZE/2 && s.capacity*2<=(s.limits.memory_bytes-overhead)/sizeof(Entry)) s.capacity*=2;
    s.table=g_try_malloc0_n(s.capacity,sizeof(Entry));
    result->peak_memory=overhead+s.capacity*sizeof(Entry);
    if (!s.table) {result->status=SEARCH_ERROR;goto done;}
    for (unsigned iteration=1;iteration<=MIN(result->requested_depth,GAME_MAX_DEPTH-game->moves);iteration++) {
        s.epoch=iteration;s.used=0;
        int values[GAME_CHOICES],destination_values[GAME_POINTS],best=-1;MoveList candidates={0};
        for(unsigned p=0;p<GAME_POINTS;p++) destination_values[p]=-1;
        for(unsigned m=0;m<GAME_CHOICES;m++) values[m]=-1;
        for(unsigned i=0;i<result->legal.count;i++) {
            unsigned m=result->legal.items[i],destination=game_moves[m].destination;
            if (destination_values[destination]<0) destination_values[destination]=visit(&s,game->occupied|POINT_BIT(destination),game->moves+1,iteration-1);
            if (result->status!=SEARCH_COMPLETE) break;
            values[m]=destination_values[destination];
            if (values[m]>best) {best=values[m];candidates.count=0;}
            if (values[m]==best) candidates.items[candidates.count++]=m;
        }
        if (result->status!=SEARCH_COMPLETE) break;
        memcpy(result->values,values,sizeof values);result->best=candidates;result->completed_depth=iteration;
    }
done:
    result->elapsed_us=g_get_monotonic_time()-s.start;
    if (control && control->progress) control->progress(result->nodes,result->completed_depth,result->elapsed_us,result->peak_memory,control->data);
    g_free(s.table);
    return result->completed_depth>0 && result->status!=SEARCH_CANCELLED && result->status!=SEARCH_ERROR;
}
int search_choose(const SearchResult *result,GRand *random)
{
    return result->status==SEARCH_CANCELLED || result->status==SEARCH_ERROR ? -1:game_random_move(result->best,random);
}
