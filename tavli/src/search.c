// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <math.h>
#include <string.h>

const char *player_name(PlayerKind kind)
{
    static const char *const names[]={"Human","Random","Race and blocking heuristic","Expectiminimax","Stochastic MCTS","Ollama"};
    return (unsigned)kind<PLAYER_COUNT?names[kind]:"Unknown";
}
const char *search_status_name(SearchStatus s)
{
    static const char *const names[]={"complete","limit-reached","cancelled","error"};return names[s];
}
SearchOptions search_defaults(void){return (SearchOptions){2,1000,5,64u*1024*1024,0};}
void search_result_clear(SearchResult *r){g_clear_pointer(&r->candidates,g_array_unref);memset(r,0,sizeof *r);}
gboolean decision_equal(const Decision *a,const Decision *b)
{
    if(a->checker!=b->checker)return FALSE;
    if(!a->checker)return a->action==b->action;
    if(a->plan.length!=b->plan.length)return FALSE;
    for(unsigned i=0;i<a->plan.length;i++)if(!step_equal(a->plan.steps[i],b->plan.steps[i]))return FALSE;
    return TRUE;
}
gboolean decision_apply(Game *g,const Decision *d)
{
    return d->checker?game_play_plan(g,&d->plan):game_control(g,d->action);
}
typedef struct {
    SearchOptions options;const SearchControl *control;SearchResult *result;
    gint64 start,last_progress;gsize used;int root;GRand *random;
} Search;
static gboolean check(gpointer data)
{
    Search *s=data;
    if(s->control && s->control->cancelled && s->control->cancelled(s->control->data))s->result->status=SEARCH_CANCELLED;
    if(s->result->status!=SEARCH_COMPLETE)return FALSE;
    gint64 now=g_get_monotonic_time();
    if((s->options.seconds>0 && now-s->start>=s->options.seconds*1000000) ||
       (s->options.node_limit && s->result->nodes>=s->options.node_limit) || s->used>=s->options.memory_bytes){s->result->status=SEARCH_LIMIT;return FALSE;}
    if(s->control && s->control->progress && now-s->last_progress>=100000){s->last_progress=now;s->control->progress(s->result->completed,s->result->nodes,s->control->data);}
    return TRUE;
}
static gsize array_bytes(const GArray *a){return a?128+2*a->len*g_array_get_element_size((GArray *)a):0;}
static gboolean reserve(Search *s,gsize bytes)
{
    if(bytes>s->options.memory_bytes || s->used>s->options.memory_bytes-bytes){s->result->status=SEARCH_LIMIT;return FALSE;}
    s->used+=bytes;s->result->peak_memory=MAX(s->result->peak_memory,s->used);return TRUE;
}
static GArray *turns(Search *s,const Game *g)
{
    if(!check(s))return NULL;
    gsize peak=0;RuleControl control={check,s,s->options.memory_bytes-s->used,&peak};
    GArray *list=game_turns(g,&control);
    s->result->peak_memory=MAX(s->result->peak_memory,MIN(s->options.memory_bytes,s->used+peak));
    if(!list){if(s->result->status==SEARCH_COMPLETE)s->result->status=SEARCH_LIMIT;return NULL;}
    if(!reserve(s,array_bytes(list))){g_array_unref(list);return NULL;}return list;
}
static void release_array(Search *s,GArray *a){if(a){s->used-=array_bytes(a);g_array_unref(a);}}
static GBytes *position_key(const Game *g)
{
    GByteArray *key=g_byte_array_new();g_byte_array_append(key,(const guint8 *)g->point,sizeof g->point);
    g_byte_array_append(key,(const guint8 *)g->pinned,sizeof g->pinned);
    g_byte_array_append(key,g->reserve,2);g_byte_array_append(key,g->bar,2);g_byte_array_append(key,g->off,2);
    guint8 fields[]={g->escaped[0],g->escaped[1],g->phase,g->active,g->actor,g->winner+2,g->cube,g->cube_owner+1,g->match.score[0],g->match.score[1]};
    g_byte_array_append(key,fields,sizeof fields);return g_byte_array_free_to_bytes(key);
}
/* Search canonicalizes equivalent complete turns; human prefix choices retain all orders. */
static GArray *decisions(Search *s,const Game *g)
{
    GArray *out=g_array_new(FALSE,TRUE,sizeof(CandidateValue));
    if(g->phase==PLAY) {
        GArray *list=turns(s,g);if(!list){g_array_unref(out);return NULL;}
        GHashTable *seen=g_hash_table_new_full(g_bytes_hash,g_bytes_equal,(GDestroyNotify)g_bytes_unref,NULL);
        gsize hash_bytes=0;
        for(unsigned i=0;i<list->len;i++) {
            if(!check(s))break;
            TurnPlan plan=g_array_index(list,TurnPlan,i);Game next=*g;game_apply_generated(&next,&plan);GBytes *key=position_key(&next);
            if(g_hash_table_contains(seen,key)){g_bytes_unref(key);continue;}
            if(!reserve(s,512)){g_bytes_unref(key);break;}hash_bytes+=512;
            g_hash_table_add(seen,key);CandidateValue value={.decision={.checker=TRUE,.plan=plan}};g_array_append_val(out,value);
        }
        g_hash_table_unref(seen);s->used-=hash_bytes;release_array(s,list);
    }else {
        GArray *choices=game_choices(g,NULL);
        for(unsigned i=0;i<choices->len;i++){CandidateValue value={.decision={.action=g_array_index(choices,Step,i).kind}};g_array_append_val(out,value);}g_array_unref(choices);
    }
    if(!check(s) || !out->len || !reserve(s,array_bytes(out))){g_array_unref(out);return NULL;}return out;
}
static gboolean apply_generated(Game *g,const Decision *d)
{
    return d->checker?game_apply_generated(g,&d->plan):game_control(g,d->action);
}
static double expect(Search *s,const Game *g,unsigned depth);
static double chance(Search *s,const Game *g,unsigned depth)
{
    double value=0;
    for(unsigned a=1;a<=6;a++)for(unsigned b=a;b<=6;b++) {
        if(!check(s))return 0;
        Game next=*g;game_roll(&next,a,b);value+=(a==b?1.:2.)/36*expect(s,&next,depth);
    }return value;
}
static double evaluate_action(Search *s,const Game *g,const Decision *d,unsigned depth)
{
    if(!d->checker && d->action==ACTION_ROLL)return chance(s,g,depth);
    Game next=*g;if(!apply_generated(&next,d)){s->result->status=SEARCH_ERROR;return 0;}
    return expect(s,&next,d->checker?depth-1:depth);
}
static double expect(Search *s,const Game *g,unsigned depth)
{
    if(!check(s))return 0;
    s->result->nodes++;
    if(!depth || game_finished(g))return game_value(g,s->root);
    GArray *list=decisions(s,g);if(!list)return 0;
    gboolean maximize=g->actor==s->root;double best=maximize?-2:2;
    for(unsigned i=0;i<list->len && check(s);i++) {
        double value=evaluate_action(s,g,&g_array_index(list,CandidateValue,i).decision,depth);
        best=maximize?MAX(best,value):MIN(best,value);
    }
    release_array(s,list);return best;
}
static double heuristic_action(const Game *g,const Decision *d,int root)
{
    if(d->checker){Game next=*g;apply_generated(&next,d);return game_value(&next,root);}
    if(d->action==ACTION_ROLL)return game_value(g,root);
    if(d->action==ACTION_DOUBLE) {
        Game take=*g,drop=*g;game_control(&take,ACTION_DOUBLE);game_control(&drop,ACTION_DOUBLE);
        game_control(&take,ACTION_TAKE);game_control(&drop,ACTION_DROP);
        return MIN(game_value(&take,root),game_value(&drop,root));
    }
    Game next=*g;game_control(&next,d->action);return game_value(&next,root);
}
static void choose_best(Search *s)
{
    double best=-G_MAXDOUBLE;unsigned tied=0;
    for(unsigned i=0;i<s->result->candidates->len;i++) {
        CandidateValue *c=&g_array_index(s->result->candidates,CandidateValue,i);
        if(c->value>best+1e-12){best=c->value;tied=0;}
        if(fabs(c->value-best)<1e-12 && g_rand_int_range(s->random,0,++tied)==0)s->result->choice=c->decision;
    }s->result->valid=TRUE;
}
static gboolean random_decision(Search *s,const Game *g,Decision *out)
{
    Game draft=*g;*out=(Decision){.checker=g->phase==PLAY};
    for(unsigned step=0;step<=4;step++) {
        if(!check(s))return FALSE;
        gsize peak=0;RuleControl control={check,s,s->options.memory_bytes-s->used,&peak};GArray *choices=game_choices(&draft,&control);
        s->result->peak_memory=MAX(s->result->peak_memory,MIN(s->options.memory_bytes,s->used+peak));
        if(!choices){if(s->result->status==SEARCH_COMPLETE)s->result->status=SEARCH_LIMIT;return FALSE;}
        if(!choices->len){g_array_unref(choices);return FALSE;}
        Step chosen=g_array_index(choices,Step,g_rand_int_range(s->random,0,choices->len));g_array_unref(choices);
        if(!out->checker){out->action=chosen.kind;return TRUE;}
        if(chosen.kind==ACTION_END)return TRUE;
        if(out->plan.length==4)return FALSE;
        out->plan.steps[out->plan.length++]=chosen;
        /* Already filtered through legal complete-turn prefixes. */
        Game next=draft;
        if(!game_apply_step_generated(&next,chosen)){s->result->status=SEARCH_ERROR;return FALSE;}draft=next;
        if(game_finished(&draft))return TRUE;
    }return FALSE;
}
typedef struct {
    Game game;gboolean chance;unsigned parent,first_child,next_sibling,root_action,visits;
    double reward;GArray *actions;unsigned expanded;
} Node;
static gboolean node_add(Search *s,GPtrArray *tree,Game game,gboolean chance_node,unsigned parent,unsigned root_action,unsigned *index)
{
    if(!reserve(s,sizeof(Node)+32))return FALSE;
    Node *node=g_new0(Node,1);node->game=game;node->chance=chance_node;node->parent=parent;node->root_action=root_action;
    *index=tree->len;g_ptr_array_add(tree,node);return TRUE;
}
static gboolean transition(Search *s,const Game *g,const Decision *d,Game *next,gboolean *chance_node)
{
    *next=*g;*chance_node=!d->checker && d->action==ACTION_ROLL;
    if(*chance_node)return TRUE;
    if(!apply_generated(next,d)){s->result->status=SEARCH_ERROR;return FALSE;}return TRUE;
}
static double rollout(Search *s,Game g)
{
    unsigned completed=0,actions=0;
    while(!game_finished(&g) && completed<128 && actions++<1024 && check(s)) {
        s->result->nodes++;
        if(g.phase==PRE_ROLL) {
            /* The rollout policy handles cube choices before drawing real-looking simulated dice. */
            if(game_can_double(&g) && g_rand_boolean(s->random)){game_control(&g,ACTION_DOUBLE);continue;}
            game_roll(&g,g_rand_int_range(s->random,1,7),g_rand_int_range(s->random,1,7));
        }else if(g.phase==CUBE_RESPONSE)game_control(&g,g_rand_boolean(s->random)?ACTION_TAKE:ACTION_DROP);
        else if(g.phase==PLAY) {
            GArray *list=turns(s,&g);if(!list)break;
            TurnPlan plan=g_array_index(list,TurnPlan,g_rand_int_range(s->random,0,list->len));release_array(s,list);game_apply_generated(&g,&plan);completed++;
        }else{s->result->status=SEARCH_ERROR;break;}
    }
    if(!game_finished(&g))s->result->rollout_cutoffs++;
    return game_value(&g,s->root);
}
static void mcts(Search *s,const Game *g)
{
    GPtrArray *tree=g_ptr_array_new();unsigned root;
    if(!node_add(s,tree,*g,FALSE,0,0,&root)){g_ptr_array_unref(tree);return;}
    Node *first=g_ptr_array_index(tree,0);first->actions=s->result->candidates;
    /* Seeded shuffled expansion prevents coordinate-order bias under small budgets. */
    for(unsigned i=first->actions->len;i>1;i--){unsigned j=g_rand_int_range(s->random,0,i);CandidateValue t=g_array_index(first->actions,CandidateValue,i-1);g_array_index(first->actions,CandidateValue,i-1)=g_array_index(first->actions,CandidateValue,j);g_array_index(first->actions,CandidateValue,j)=t;}
    for(unsigned iteration=0;iteration<s->options.iterations && check(s);iteration++) {
        unsigned at=0,depth=0;
        while(check(s) && depth++<512) {
            Node *node=g_ptr_array_index(tree,at);s->result->nodes++;
            if(game_finished(&node->game))break;
            if(node->chance) {
                unsigned a=g_rand_int_range(s->random,1,7),b=g_rand_int_range(s->random,1,7);if(a>b){unsigned t=a;a=b;b=t;}
                unsigned child=node->first_child;
                for(;child;child=((Node *)g_ptr_array_index(tree,child))->next_sibling){Node *n=g_ptr_array_index(tree,child);if(n->game.roll[0]==a && n->game.roll[1]==b)break;}
                if(!child){Game next=node->game;game_roll(&next,a,b);if(!node_add(s,tree,next,FALSE,at,node->root_action,&child))break;Node *n=g_ptr_array_index(tree,child);n->next_sibling=node->first_child;node->first_child=child;at=child;break;}
                at=child;continue;
            }
            if(!node->actions){node->actions=decisions(s,&node->game);if(!node->actions)break;}
            if(node->expanded<node->actions->len) {
                unsigned action=node->expanded++;CandidateValue *c=&g_array_index(node->actions,CandidateValue,action);Game next;gboolean chance_node;unsigned child;
                if(!transition(s,&node->game,&c->decision,&next,&chance_node) || !node_add(s,tree,next,chance_node,at,at?node->root_action:action,&child))break;
                Node *n=g_ptr_array_index(tree,child);n->next_sibling=node->first_child;node->first_child=child;at=child;break;
            }
            double best=-G_MAXDOUBLE;unsigned chosen=0;
            for(unsigned child=node->first_child;child;child=((Node *)g_ptr_array_index(tree,child))->next_sibling) {
                Node *n=g_ptr_array_index(tree,child);double mean=n->reward/MAX(1u,n->visits);
                double value=(node->game.actor==s->root?mean:-mean)+sqrt(2*log(MAX(1u,node->visits))/MAX(1u,n->visits));
                if(value>best){best=value;chosen=child;}
            }
            if(!chosen)break;
            at=chosen;
        }
        Node *leaf=g_ptr_array_index(tree,at);Game simulation=leaf->game;
        if(leaf->chance)game_roll(&simulation,g_rand_int_range(s->random,1,7),g_rand_int_range(s->random,1,7));
        double value=rollout(s,simulation);if(!check(s))break;
        for(unsigned n=at;;){Node *node=g_ptr_array_index(tree,n);node->visits++;node->reward+=value;if(!n)break;n=node->parent;}
        s->result->completed++;
    }
    unsigned most=0,tied=0;double best=-G_MAXDOUBLE;
    for(unsigned child=first->first_child;child;child=((Node *)g_ptr_array_index(tree,child))->next_sibling) {
        Node *node=g_ptr_array_index(tree,child);CandidateValue *c=&g_array_index(s->result->candidates,CandidateValue,node->root_action);
        c->visits=node->visits;c->value=node->visits?node->reward/node->visits:0;if(!node->visits)continue;
        if(c->visits>most || (c->visits==most && c->value>best+1e-12)){most=c->visits;best=c->value;tied=0;}
        if(c->visits==most && fabs(c->value-best)<1e-12 && g_rand_int_range(s->random,0,++tied)==0){s->result->choice=c->decision;s->result->valid=TRUE;}
    }
    for(unsigned i=0;i<tree->len;i++){Node *node=g_ptr_array_index(tree,i);if(i)release_array(s,node->actions);g_free(node);s->used-=sizeof(Node)+32;}
    g_ptr_array_unref(tree);
}
gboolean search_decide(const Game *g,PlayerKind kind,const SearchOptions *options,const SearchControl *control,GRand *random,SearchResult *result)
{
    *result=(SearchResult){0};Search s={.options=options?*options:search_defaults(),.control=control,.result=result,.start=g_get_monotonic_time(),.root=g->actor,.random=random,.used=65536};
    if(!isfinite(s.options.seconds) || s.options.seconds<0 || s.options.depth<1 || s.options.depth>6 || !s.options.iterations || s.options.iterations>1000000 || !random || kind<PLAYER_RANDOM || kind>PLAYER_MCTS || game_finished(g) || g->phase==OPENING)result->status=SEARCH_ERROR;
    result->requested=kind==PLAYER_EXPECTI?s.options.depth:kind==PLAYER_MCTS?s.options.iterations:1;
    if(!check(&s))goto done;
    result->peak_memory=s.used;
    if(kind==PLAYER_RANDOM) {
        result->valid=random_decision(&s,g,&result->choice);result->completed=result->valid?1:0;goto done;
    }
    result->candidates=decisions(&s,g);if(!result->candidates)goto done;
    if(kind==PLAYER_HEURISTIC) {
        for(unsigned i=0;i<result->candidates->len;i++)g_array_index(result->candidates,CandidateValue,i).value=heuristic_action(g,&g_array_index(result->candidates,CandidateValue,i).decision,s.root);
        if(check(&s)){choose_best(&s);result->completed=1;}
    }else if(kind==PLAYER_MCTS)mcts(&s,g);
    else {
        unsigned count=result->candidates->len;gsize bytes=count*sizeof(double);
        if(!reserve(&s,bytes))goto done;
        double *values=g_new(double,count);
        for(unsigned depth=1;depth<=s.options.depth && check(&s);depth++) {
            for(unsigned i=0;i<count && check(&s);i++)values[i]=evaluate_action(&s,g,&g_array_index(result->candidates,CandidateValue,i).decision,depth);
            if(!check(&s))break;
            for(unsigned i=0;i<count;i++)g_array_index(result->candidates,CandidateValue,i).value=values[i];
            result->completed=depth;
        }
        if(result->completed)choose_best(&s);
        g_free(values);s.used-=bytes;
    }
done:
    result->elapsed_us=g_get_monotonic_time()-s.start;
    if(result->status==SEARCH_CANCELLED || result->status==SEARCH_ERROR)result->valid=FALSE;
    if(control && control->progress)control->progress(result->completed,result->nodes,control->data);
    return result->valid;
}
