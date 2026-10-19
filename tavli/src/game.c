// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include "i18n.h"
#include <math.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

const char *variant_name(Variant v)
{
    static const char *const names[]={"Portes","Plakoto","Fevga","Backgammon","Running"};
    return (unsigned)v<VARIANT_COUNT?names[v]:"Unknown";
}
const char *phase_name(Phase p)
{
    static const char *const names[]={"Opening roll","Before roll","Cube response","Play dice","Game over","Match over"};
    return (unsigned)p<G_N_ELEMENTS(names)?names[p]:"Unknown";
}
const char *reason_name(ResultReason r)
{
    static const char *const names[]={"ongoing","single","gammon","backgammon","mother pinned","cube dropped","both mothers pinned"};
    return (unsigned)r<G_N_ELEMENTS(names)?names[r]:"unknown";
}
const char *action_name(ActionKind a)
{
    static const char *const names[]={"Move","Roll dice","Offer double","Take","Drop","Confirm turn"};
    return (unsigned)a<G_N_ELEMENTS(names)?names[a]:"Unknown";
}
int game_point_at(const Game *g,int player,unsigned index)
{
    if(index>=24 || player<0 || player>1)return -1;
    if(!player)return 23-(int)index;
    return g->variant==FEVGA?(35-(int)index)%24:(int)index;
}
int game_index(const Game *g,int player,unsigned point)
{
    if(point>=24)return -1;
    for(unsigned i=0;i<24;i++)if(game_point_at(g,player,i)==(int)point)return i;
    return -1;
}
gboolean game_finished(const Game *g){return g->phase==GAME_OVER || g->phase==MATCH_OVER;}
static void setup(Game *g)
{
    memset(g->point,0,sizeof g->point);memset(g->pinned,-1,sizeof g->pinned);
    memset(g->reserve,0,sizeof g->reserve);memset(g->bar,0,sizeof g->bar);memset(g->off,0,sizeof g->off);
    memset(g->escaped,0,sizeof g->escaped);memset(g->remaining,0,sizeof g->remaining);memset(g->roll,0,sizeof g->roll);
    g->phase=OPENING;g->active=g->actor=0;g->winner=-2;g->cube_owner=-1;g->cube=1;
    g->award=g->turns=g->steps=0;g->reason=REASON_NONE;g->release_required=FALSE;
    g->crawford=FALSE;
    if(g->variant==BACKGAMMON && g->match.session!=SESSION_SINGLE && !g->match.crawford_used &&
       (g->match.score[0]+1==g->match.target || g->match.score[1]+1==g->match.target)) {
        g->crawford=TRUE;g->match.crawford_used=TRUE;
    }
    for(int p=0;p<2;p++) {
        if(g->variant==RUNNING)g->reserve[p]=15;
        else if(g->variant==PLAKOTO || g->variant==FEVGA)g->point[game_point_at(g,p,0)][p]=15;
        else {
            const unsigned indexes[]={0,11,16,18},counts[]={2,5,3,5};
            for(unsigned k=0;k<4;k++)g->point[game_point_at(g,p,indexes[k])][p]=counts[k];
        }
    }
    if(g->match.number && g->variant!=BACKGAMMON && g->variant!=RUNNING && g->match.previous_winner>=0){
        g->active=g->actor=g->match.previous_winner;g->phase=PRE_ROLL;
    }
}
gboolean game_init(Game *g,Variant v,Session session,unsigned target)
{
    if((unsigned)v>=VARIANT_COUNT || (unsigned)session>SESSION_TRADITIONAL || target<1 || target>25)return FALSE;
    *g=(Game){.variant=session==SESSION_TRADITIONAL?PORTES:v,
        .match={.session=session,.selected=v,.target=target,.previous_winner=-1}};
    setup(g);return TRUE;
}
gboolean game_next(Game *g)
{
    if(g->phase!=GAME_OVER)return FALSE;
    if(g->winner>=0){g->match.previous_winner=g->winner;if(g->match.session==SESSION_TRADITIONAL)g->variant=(g->variant+1)%3;}
    else g->match.previous_winner=-1;
    g->match.number++;setup(g);return TRUE;
}
static void finish(Game *g,int winner,ResultReason reason)
{
    if(game_finished(g))return;
    unsigned base=0;
    if(winner>=0) {
        int loser=1-winner;base=1;
        if(reason==REASON_DROP)base=g->cube;
        else {
            if(reason==REASON_MOTHER || (g->variant!=RUNNING && !g->off[loser]))base=2;
            if(g->variant==BACKGAMMON && !g->off[loser]) {
                gboolean in_home=g->bar[loser]>0;
                for(unsigned i=18;i<24;i++)in_home|=g->point[game_point_at(g,winner,i)][loser]>0;
                if(in_home)base=3;
            }
            if(reason!=REASON_MOTHER)reason=base==3?REASON_BACKGAMMON:base==2?REASON_GAMMON:REASON_SINGLE;
            base*=g->cube;
        }
        g->match.score[winner]+=base;
    }
    g->winner=winner;g->award=base;g->reason=reason;g->phase=GAME_OVER;
    if(g->match.session!=SESSION_SINGLE && winner>=0 && g->match.score[winner]>=g->match.target)g->phase=MATCH_OVER;
}
static void check_result(Game *g)
{
    if(g->variant==PLAKOTO) {
        gboolean mother[2];
        for(int p=0;p<2;p++){int head=game_point_at(g,p,0);mother[p]=g->point[head][p]==1 && g->pinned[head]==p;}
        if(mother[0] && mother[1]){finish(g,-1,REASON_DRAW);return;}
        for(int p=0;p<2;p++)if(mother[p] && !g->point[game_point_at(g,1-p,0)][1-p]){finish(g,1-p,REASON_MOTHER);return;}
    }
    for(int p=0;p<2;p++)if(g->off[p]==15){finish(g,p,REASON_SINGLE);return;}
}
gboolean game_opening(Game *g,unsigned a,unsigned b)
{
    if(g->phase!=OPENING || a<1 || a>6 || b<1 || b>6)return FALSE;
    if(a==b)return TRUE;
    g->active=g->actor=a>b?0:1;g->phase=PRE_ROLL;
    return g->variant!=BACKGAMMON || game_roll(g,a,b);
}
gboolean game_trapping_prime(const Game *g,int owner)
{
    int other=1-owner,at=-1;unsigned active=0;
    for(unsigned q=0;q<24;q++)if(g->point[q][other]){if(at>=0)return FALSE;at=q;active+=g->point[q][other];}
    if(at<0 || !active || g->reserve[other] || g->bar[other])return FALSE;
    int index=game_index(g,other,at);if(index+6>=24)return FALSE;
    for(unsigned i=1;i<=6;i++)if(!g->point[game_point_at(g,other,index+i)][owner])return FALSE;
    return TRUE;
}
gboolean game_roll(Game *g,unsigned a,unsigned b)
{
    if(g->phase!=PRE_ROLL || a<1 || a>6 || b<1 || b>6)return FALSE;
    memset(g->remaining,0,sizeof g->remaining);g->roll[0]=a;g->roll[1]=b;
    unsigned copies=a==b && g->variant!=RUNNING?2:1;
    g->remaining[a]+=copies;g->remaining[b]+=copies;g->steps=0;g->phase=PLAY;g->actor=g->active;
    g->release_required=g->variant==FEVGA && game_trapping_prime(g,g->active);return TRUE;
}
gboolean game_can_double(const Game *g)
{
    return g->phase==PRE_ROLL && g->variant==BACKGAMMON && g->match.session!=SESSION_SINGLE && !g->crawford &&
        (g->cube_owner<0 || g->cube_owner==g->active) && g->match.score[g->active]+g->cube<g->match.target;
}
gboolean game_control(Game *g,ActionKind action)
{
    if(action==ACTION_DOUBLE && game_can_double(g)){g->phase=CUBE_RESPONSE;g->actor=1-g->active;return TRUE;}
    if(g->phase==CUBE_RESPONSE) {
        if(action==ACTION_TAKE){g->cube*=2;g->cube_owner=g->actor;g->actor=g->active;g->phase=PRE_ROLL;return TRUE;}
        if(action==ACTION_DROP){finish(g,g->active,REASON_DROP);return TRUE;}
    }
    return FALSE;
}
static gboolean home_ready(const Game *g,int p)
{
    if(g->bar[p] || g->reserve[p])return FALSE;
    for(unsigned i=0;i<18;i++)if(g->point[game_point_at(g,p,i)][p])return FALSE;
    return TRUE;
}
/* Atomic legality, before global turn quotas and end-of-turn prime release. */
static gboolean atomic(Game *g,Step s)
{
    int p=g->active,o=1-p;unsigned from=s.from,to=s.to,die=s.die;
    if(g->phase!=PLAY || s.kind!=ACTION_STEP || die<1 || die>6 || !g->remaining[die])return FALSE;
    if(g->bar[p] && from!=SOURCE_BAR)return FALSE;
    int index=-1,next;
    if(from==SOURCE_RESERVE){if(g->variant!=RUNNING || !g->reserve[p])return FALSE;next=die-1;}
    else if(from==SOURCE_BAR){if(!g->bar[p] || (g->variant!=PORTES && g->variant!=BACKGAMMON))return FALSE;next=die-1;}
    else {
        if(from>=24 || !g->point[from][p] || g->pinned[from]==p)return FALSE;
        index=game_index(g,p,from);next=index+(int)die;
        if(g->variant==FEVGA && !g->escaped[p] && index==0 && g->point[from][p]!=15)return FALSE;
    }
    if(next>=24) {
        if(to!=DEST_OFF || index<0)return FALSE;
        if(g->variant==RUNNING){if(next!=24)return FALSE;}
        else {
            if(!home_ready(g,p))return FALSE;
            if(next>24)for(int i=0;i<index;i++)if(g->point[game_point_at(g,p,i)][p])return FALSE;
        }
    }else {
        if(to!=(unsigned)game_point_at(g,p,next))return FALSE;
        if(g->point[to][o]) {
            if(g->variant==FEVGA || g->variant==RUNNING || g->point[to][o]>1 || g->pinned[to]==p)return FALSE;
        }
    }
    Game saved=*g;
    if(from==SOURCE_RESERVE)g->reserve[p]--;
    else if(from==SOURCE_BAR)g->bar[p]--;
    else {g->point[from][p]--;if(!g->point[from][p] && g->pinned[from]==o)g->pinned[from]=-1;}
    if(next>=24)g->off[p]++;
    else {
        if(g->point[to][o] && !g->point[to][p]) {
            if(g->variant==PLAKOTO)g->pinned[to]=o;
            else{g->point[to][o]=0;g->bar[o]++;}
        }
        g->point[to][p]++;
    }
    if(g->variant==FEVGA) {
        if(next>12)g->escaped[p]=TRUE;
        gboolean full=TRUE;for(unsigned i=0;i<6;i++)full&=g->point[game_point_at(g,p,i)][p]>0;
        if(full){*g=saved;return FALSE;}
    }
    g->remaining[die]--;g->steps++;check_result(g);return TRUE;
}
static unsigned raw_steps(const Game *g,Step choices[156])
{
    unsigned n=0;
    for(unsigned die=1;die<=6;die++)if(g->remaining[die])for(unsigned from=0;from<=SOURCE_BAR;from++) {
        int index=from<24?game_index(g,g->active,from):-1;
        int next=index+(int)die;
        Step s={ACTION_STEP,from,next>=24?DEST_OFF:(unsigned)game_point_at(g,g->active,next),die};
        Game copy=*g;if(atomic(&copy,s))choices[n++]=s;
    }return n;
}
typedef struct { TurnPlan plan; gboolean releases,terminal; } Candidate;
typedef struct { GArray *all;const RuleControl *control;gboolean stopped; } Enumeration;
static void enumerate(Enumeration *e,const Game *g,TurnPlan *plan)
{
    if(e->control && e->control->check && !e->control->check(e->control->data)){e->stopped=TRUE;return;}
    Step choices[156];unsigned count=game_finished(g)?0:raw_steps(g,choices);
    if(!count || plan->length==MAX_STEPS) {
        Candidate c={.plan=*plan,.releases=g->variant!=FEVGA || !game_trapping_prime(g,g->active),.terminal=game_finished(g)};
        gsize bytes=4096+4*(e->all->len+1)*(sizeof(Candidate)+sizeof(TurnPlan));
        if(e->control && e->control->peak_bytes)*e->control->peak_bytes=MAX(*e->control->peak_bytes,bytes);
        if(e->control && e->control->memory_bytes && bytes>e->control->memory_bytes){e->stopped=TRUE;return;}
        g_array_append_val(e->all,c);return;
    }
    for(unsigned i=0;i<count && !e->stopped;i++){Game next=*g;atomic(&next,choices[i]);plan->steps[plan->length++]=choices[i];enumerate(e,&next,plan);plan->length--;}
}
GArray *game_turns(const Game *g,const RuleControl *control)
{
    if(g->phase!=PLAY)return NULL;
    Enumeration e={.all=g_array_new(FALSE,FALSE,sizeof(Candidate)),.control=control};TurnPlan plan={0};
    enumerate(&e,g,&plan);
    if(e.stopped){g_array_unref(e.all);return NULL;}
    gboolean release=FALSE;unsigned maximum=0,high=0;
    for(unsigned i=0;i<e.all->len;i++)release|=g_array_index(e.all,Candidate,i).releases;
    for(unsigned i=0;i<e.all->len;i++){Candidate *c=&g_array_index(e.all,Candidate,i);if(release && !c->releases)continue;maximum=MAX(maximum,c->plan.length);}
    if(maximum==1)for(unsigned i=0;i<e.all->len;i++){Candidate *c=&g_array_index(e.all,Candidate,i);if((!release || c->releases) && c->plan.length==1)high=MAX(high,c->plan.steps[0].die);}
    GArray *result=g_array_new(FALSE,FALSE,sizeof(TurnPlan));
    for(unsigned i=0;i<e.all->len;i++) {
        Candidate *c=&g_array_index(e.all,Candidate,i);
        if(release && !c->releases)continue;
        if(c->plan.length!=maximum && !c->terminal)continue;
        if(maximum==1 && c->plan.steps[0].die!=high)continue;
        g_array_append_val(result,c->plan);
    }
    g_array_unref(e.all);return result;
}
gboolean step_equal(Step a,Step b){return a.kind==b.kind && a.from==b.from && a.to==b.to && a.die==b.die;}
GArray *game_choices(const Game *g,const RuleControl *control)
{
    GArray *choices=g_array_new(FALSE,FALSE,sizeof(Step));
    if(g->phase==PRE_ROLL){Step s={.kind=ACTION_ROLL};g_array_append_val(choices,s);if(game_can_double(g)){s.kind=ACTION_DOUBLE;g_array_append_val(choices,s);}}
    else if(g->phase==CUBE_RESPONSE){Step s={.kind=ACTION_TAKE};g_array_append_val(choices,s);s.kind=ACTION_DROP;g_array_append_val(choices,s);}
    else if(g->phase==PLAY) {
        GArray *turns=game_turns(g,control);if(!turns){g_array_unref(choices);return NULL;}
        for(unsigned i=0;i<turns->len;i++) {
            TurnPlan *t=&g_array_index(turns,TurnPlan,i);Step s=t->length?t->steps[0]:(Step){.kind=ACTION_END};gboolean found=FALSE;
            for(unsigned j=0;j<choices->len;j++)found|=step_equal(s,g_array_index(choices,Step,j));
            if(!found)g_array_append_val(choices,s);
        }g_array_unref(turns);
    }return choices;
}
gboolean game_apply_step_generated(Game *g,Step step){return atomic(g,step);}
gboolean game_step(Game *g,Step step)
{
    GArray *choices=game_choices(g,NULL);if(!choices)return FALSE;gboolean found=FALSE;
    for(unsigned i=0;i<choices->len;i++)found|=step_equal(step,g_array_index(choices,Step,i));
    g_array_unref(choices);return found && atomic(g,step);
}
gboolean game_end_turn(Game *g)
{
    if(g->phase!=PLAY)return FALSE;
    GArray *choices=game_choices(g,NULL);gboolean empty=choices && choices->len==1 && g_array_index(choices,Step,0).kind==ACTION_END;
    if(choices)g_array_unref(choices);
    if(!empty)return FALSE;
    memset(g->remaining,0,sizeof g->remaining);g->turns++;g->active=1-g->active;g->actor=g->active;g->phase=PRE_ROLL;g->release_required=FALSE;return TRUE;
}
gboolean game_apply_generated(Game *g,const TurnPlan *plan)
{
    if(g->phase!=PLAY || plan->length>4)return FALSE;
    Game next=*g;for(unsigned i=0;i<plan->length;i++)if(!atomic(&next,plan->steps[i]))return FALSE;
    if(!game_finished(&next)) {
        Step unused[156];if(raw_steps(&next,unused))return FALSE;
        memset(next.remaining,0,sizeof next.remaining);next.turns++;next.active=1-next.active;next.actor=next.active;next.phase=PRE_ROLL;next.release_required=FALSE;
    }
    *g=next;return TRUE;
}
gboolean game_play_plan(Game *g,const TurnPlan *plan)
{
    GArray *turns=game_turns(g,NULL);if(!turns)return FALSE;gboolean found=FALSE;
    for(unsigned i=0;i<turns->len;i++){TurnPlan *t=&g_array_index(turns,TurnPlan,i);if(t->length!=plan->length)continue;gboolean same=TRUE;for(unsigned j=0;j<t->length;j++)same&=step_equal(t->steps[j],plan->steps[j]);found|=same;}
    g_array_unref(turns);return found && game_apply_generated(g,plan);
}
void step_label(Step s,char *buffer,gsize size)
{
    if(s.kind!=ACTION_STEP){g_strlcpy(buffer,action_name(s.kind),size);return;}
    char from[16],to[16];if(s.from==SOURCE_RESERVE)g_strlcpy(from,"reserve",sizeof from);else if(s.from==SOURCE_BAR)g_strlcpy(from,"bar",sizeof from);else g_snprintf(from,sizeof from,"%u",s.from+1);
    if(s.to==DEST_OFF)g_strlcpy(to,"off",sizeof to);else g_snprintf(to,sizeof to,"%u",s.to+1);
    g_snprintf(buffer,size,"%s>%s/%u",from,to,s.die);
}
gboolean game_valid(const Game *g)
{
    if((unsigned)g->variant>=VARIANT_COUNT || g->active<0 || g->active>1 || g->actor<0 || g->actor>1)return FALSE;
    for(int p=0;p<2;p++){unsigned n=g->reserve[p]+g->bar[p]+g->off[p];for(unsigned q=0;q<24;q++)n+=g->point[q][p];if(n!=15)return FALSE;}
    for(unsigned q=0;q<24;q++) {
        if(g->point[q][0] && g->point[q][1]){int p=g->pinned[q];if(g->variant!=PLAKOTO || p<0 || p>1 || g->point[q][p]!=1)return FALSE;}
        else if(g->pinned[q]!=-1)return FALSE;
    }return TRUE;
}
int game_pips(const Game *g,int p)
{
    int total=25*(g->reserve[p]+g->bar[p]);for(unsigned q=0;q<24;q++)total+=g->point[q][p]*(24-game_index(g,p,q));return total;
}
static gboolean blocked(const Game *g,int p,int q)
{
    int o=1-p;if(g->pinned[q]==p)return TRUE;
    return g->point[q][o]>=(g->variant==FEVGA?1:2) || (g->variant==PLAKOTO && g->pinned[q]==p);
}
static void features(const Game *g,int p,int *control,int *blocked_landings,int *vulnerable,int *pinned)
{
    *control=*blocked_landings=*vulnerable=*pinned=0;
    for(unsigned q=0;q<24;q++) {
        if(g->pinned[q]==p)(*pinned)++;
        if(g->point[q][p]) {
            gboolean strong=g->point[q][p]>=2 || g->variant==FEVGA || g->variant==RUNNING || g->pinned[q]==1-p;
            *control+=strong;
            if(!strong)(*vulnerable)++;
        }
        if(g->point[q][1-p])for(unsigned d=1;d<=6;d++){int index=game_index(g,1-p,q)+d;if(index<24)*blocked_landings+=blocked(g,1-p,game_point_at(g,1-p,index));}
    }
}
int game_heuristic(const Game *g,int p)
{
    int control[2],blocks[2],blots[2],pins[2];for(int i=0;i<2;i++)features(g,i,&control[i],&blocks[i],&blots[i],&pins[i]);int o=1-p;
    return game_pips(g,o)-game_pips(g,p)+24*((int)g->off[p]-g->off[o])+8*(control[p]-control[o])+6*(blocks[p]-blocks[o])+
        12*((int)g->bar[o]+pins[o]-g->bar[p]-pins[p])-6*(blots[p]-blots[o]);
}
double game_value(const Game *g,int p)
{
    int o=1-p;double estimate=game_finished(g)?0:tanh(game_heuristic(g,p)/200.)*g->cube;
    if(g->match.session==SESSION_SINGLE) {
        if(game_finished(g))return g->winner<0?0:(g->winner==p?1:-1)*(double)g->award/3;
        return estimate/3;
    }
    if(g->match.score[p]>=g->match.target)return 1;
    if(g->match.score[o]>=g->match.target)return -1;
    return CLAMP(((double)g->match.score[p]-g->match.score[o]+estimate)/g->match.target,-.999,.999);
}

gboolean game_append_report(const char *path, const char *report, GError **error)
{
    int fd = g_open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
    if (fd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    _("Could not open %s: %s"), path, g_strerror(errno));
        return FALSE;
    }
    /* Serialize separate app instances, and roll back an incomplete append so
     * the pending report can be retried without duplicating its prefix. */
    int saved_errno = 0;
    while (flock(fd, LOCK_EX) < 0) {
        if (errno != EINTR) {
            saved_errno = errno;
            break;
        }
    }
    struct stat info;
    gboolean can_rollback = FALSE;
    if (!saved_errno) {
        if (fstat(fd, &info) < 0)
            saved_errno = errno;
        else if (!S_ISREG(info.st_mode))
            saved_errno = EINVAL;
        else
            can_rollback = TRUE;
    }
    gsize remaining = strlen(report);
    const char *cursor = report;
    while (!saved_errno && remaining) {
        ssize_t written = write(fd, cursor, remaining);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            saved_errno = written == 0 ? EIO : errno;
        else {
            cursor += written;
            remaining -= written;
        }
    }
    if (!saved_errno && fsync(fd) < 0)
        saved_errno = errno;
    gboolean rollback_failed = FALSE;
    if (saved_errno && can_rollback)
        rollback_failed = ftruncate(fd, info.st_size) < 0;
    /* Closing releases the advisory lock. Data has already been synchronized. */
    close(fd);
    if (saved_errno)
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                    _("Could not write %s: %s%s"), path, g_strerror(saved_errno),
                    rollback_failed ? _(". Could not remove the partial write; check the file before retrying") : "");
    return saved_errno == 0;
}
