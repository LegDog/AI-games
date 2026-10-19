// SPDX-License-Identifier: GPL-3.0-or-later
#include "graph.h"
#include <string.h>
gboolean graph_controller(PlayerKind kind)
{
    return kind==PLAYER_CONNECTIVITY || forest_controller(kind);
}
gboolean forest_controller(PlayerKind kind)
{
    return kind==PLAYER_FOREST || kind==PLAYER_GF2;
}
gboolean graph_analyze(const Game *game, GraphAnalysis *a)
{
    memset(a,0,sizeof *a); a->matrix_rank=-1;
    guint64 full=POINT_BIT(GAME_POINTS)-1;
    if ((game->occupied&~full) || game_count(game->occupied)!=game->moves || game->moves>GAME_MAX_DEPTH) return FALSE;
    a->empty=full^game->occupied;
    /* Only entry/destination matter: the middle is allowed either occupancy.
     * Every edge must have its registered reverse placement for the forest proof. */
    for (unsigned m=0;m<GAME_CHOICES;m++) {
        Move move=game_moves[m];
        if (move.entry>=GAME_POINTS || move.destination>=GAME_POINTS ||
            game_find_move(move.destination,move.entry)<0) return FALSE;
        if ((a->empty&POINT_BIT(move.entry)) && (a->empty&POINT_BIT(move.destination)))
            a->adjacent[move.entry]|=POINT_BIT(move.destination);
    }
    unsigned degree_sum=0;
    for (unsigned p=0;p<GAME_POINTS;p++) degree_sum+=game_count(a->adjacent[p]);
    a->edge_count=degree_sum/2;
    guint64 remaining=a->empty;
    while (remaining) {
        guint64 frontier=remaining&-remaining, component=0;
        while (frontier) {
            component|=frontier;
            guint64 next=0;
            for (unsigned p=0;p<GAME_POINTS;p++) if (frontier&POINT_BIT(p)) next|=a->adjacent[p];
            frontier=next&~component;
        }
        a->components[a->component_count++]=component;remaining&=~component;
    }
    a->max_final_pieces=game->moves+game_count(a->empty)-a->component_count;
    return TRUE;
}
static gboolean stopped(const SearchControl *control, GraphResult *result)
{
    if (control && control->cancelled && control->cancelled(control->data)) {
        result->status=GRAPH_CANCELLED;result->best.count=0;result->length=0;return TRUE;
    }
    return FALSE;
}
static gboolean initialize(const Game *game, const SearchControl *control, GraphResult *result)
{
    memset(result,0,sizeof *result);
    for (unsigned m=0;m<GAME_CHOICES;m++) result->values[m]=-1;
    if (stopped(control,result)) return FALSE;
    if (!graph_analyze(game,&result->analysis)) {result->status=GRAPH_ERROR;return FALSE;}
    return TRUE;
}
gboolean graph_connectivity(const Game *game, const SearchControl *control, GraphResult *result)
{
    gint64 start=g_get_monotonic_time();
    if (!initialize(game,control,result)) goto done;
    int best=-1, destination_values[GAME_POINTS];
    for (unsigned p=0;p<GAME_POINTS;p++) destination_values[p]=-1;
    for (unsigned m=0;m<GAME_CHOICES;m++) if (game_legal(game,m)) {
        if (stopped(control,result)) goto done;
        unsigned destination=game_moves[m].destination;
        if (destination_values[destination]<0) {
            Game child=*game;GraphAnalysis analysis;
            if (!game_play(&child,m) || !graph_analyze(&child,&analysis)) {result->status=GRAPH_ERROR;goto done;}
            destination_values[destination]=analysis.max_final_pieces;
        }
        int value=destination_values[destination];result->values[m]=value;
        if (value>best) {best=value;result->best.count=0;}
        if (value==best) result->best.items[result->best.count++]=m;
    }
    stopped(control,result);
done:
    result->elapsed_us=g_get_monotonic_time()-start;
    result->peak_memory=sizeof(GraphResult)+2*sizeof(GraphAnalysis)+sizeof(Game)+4096;
    if (result->status!=GRAPH_COMPLETE) {result->length=0;result->best.count=0;}
    return result->status==GRAPH_COMPLETE;
}
static void shuffle(unsigned *items, unsigned count, GRand *random)
{
    for (unsigned i=count;i>1;i--) {
        unsigned j=g_rand_int_range(random,0,i), temporary=items[i-1];items[i-1]=items[j];items[j]=temporary;
    }
}
gboolean graph_forest(const Game *game, gboolean gf2, GRand *random, const SearchControl *control, GraphResult *result)
{
    enum { MAX_EDGES=GAME_CHOICES/2 };
    gint64 start=g_get_monotonic_time();
    if (!initialize(game,control,result)) goto done;
    guint64 forest[GAME_POINTS];memcpy(forest,result->analysis.adjacent,sizeof forest);
    if (gf2) {
        unsigned endpoints[MAX_EDGES][2], columns[MAX_EDGES], count=0;
        for (unsigned a=0;a<GAME_POINTS;a++) for (unsigned b=a+1;b<GAME_POINTS;b++) if (forest[a]&POINT_BIT(b)) {
            if (count==MAX_EDGES) {result->status=GRAPH_ERROR;goto done;}
            endpoints[count][0]=a;endpoints[count][1]=b;columns[count]=count;count++;
        }
        shuffle(columns,count,random);
        /* Each row is at most 60 bits. Pivot columns refer to ORIGINAL edges,
         * never to the transformed matrix columns interpreted as placements. */
        guint64 rows[GAME_POINTS]={0};
        for (unsigned column=0;column<count;column++) {
            unsigned edge=columns[column];
            rows[endpoints[edge][0]]|=POINT_BIT(column);rows[endpoints[edge][1]]|=POINT_BIT(column);
        }
        memset(forest,0,sizeof forest);unsigned rank=0;
        for (unsigned column=0;column<count;column++) {
            if (stopped(control,result)) goto done;
            unsigned pivot=rank;
            while (pivot<GAME_POINTS && !(rows[pivot]&POINT_BIT(column))) pivot++;
            if (pivot==GAME_POINTS) continue;
            guint64 temporary=rows[rank];rows[rank]=rows[pivot];rows[pivot]=temporary;
            for (unsigned row=0;row<GAME_POINTS;row++) if (row!=rank && (rows[row]&POINT_BIT(column))) {
                rows[row]^=rows[rank];result->xor_operations++;
            }
            unsigned edge=columns[column],a=endpoints[edge][0],b=endpoints[edge][1];
            forest[a]|=POINT_BIT(b);forest[b]|=POINT_BIT(a);rank++;
        }
        result->analysis.matrix_rank=rank;
        if (rank!=game_count(result->analysis.empty)-result->analysis.component_count) {result->status=GRAPH_ERROR;goto done;}
    }
    unsigned vertices[GAME_POINTS], parent[GAME_POINTS], order[GAME_POINTS], length=0;
    for (unsigned p=0;p<GAME_POINTS;p++) {vertices[p]=p;parent[p]=GAME_POINTS;}
    shuffle(vertices,GAME_POINTS,random);guint64 seen=0;
    for (unsigned i=0;i<GAME_POINTS;i++) {
        unsigned root=vertices[i];if (!(result->analysis.empty&POINT_BIT(root)) || (seen&POINT_BIT(root))) continue;
        unsigned first=length;order[length++]=root;seen|=POINT_BIT(root);
        for (unsigned index=first;index<length;index++) {
            if (stopped(control,result)) goto done;
            unsigned v=order[index];
            for (unsigned j=0;j<GAME_POINTS;j++) {
                unsigned w=vertices[j];if (!(forest[v]&POINT_BIT(w)) || (seen&POINT_BIT(w))) continue;
                parent[w]=v;seen|=POINT_BIT(w);order[length++]=w;
            }
        }
    }
    Game replay=*game;
    for (unsigned i=length;i>0;i--) {
        if (stopped(control,result)) goto done;
        unsigned destination=order[i-1],source=parent[destination];if (source==GAME_POINTS) continue;
        int move=game_find_move(source,destination);
        if (move<0 || result->length>=GAME_MAX_DEPTH || !game_play(&replay,move)) {result->status=GRAPH_ERROR;goto done;}
        result->plan[result->length++]=move;
    }
    if (replay.moves!=result->analysis.max_final_pieces || !game_result(&replay)) result->status=GRAPH_ERROR;
    stopped(control,result);
done:
    result->elapsed_us=g_get_monotonic_time()-start;
    result->peak_memory=sizeof(GraphResult)+sizeof(Game)+sizeof(GraphAnalysis)+16*GAME_CHOICES+8192;
    if (result->status!=GRAPH_COMPLETE) {result->length=0;result->best.count=0;}
    return result->status==GRAPH_COMPLETE;
}
