// SPDX-License-Identifier: GPL-3.0-or-later
#include <adwaita.h>
#include <math.h>
#include <glib/gstdio.h>
#include "animation.h"
#include "ollama.h"
#include "i18n.h"

typedef struct {
    Game game;PlayerKind kind;SearchOptions options;OllamaOptions thinking;OllamaResult ollama;
    SearchResult search;Decision decision;guint generation;guint32 seed;gboolean list;
    char *url,*model,*details;char **models;GCancellable *cancel;GMutex lock;unsigned progress;guint64 nodes;
} Job;
typedef struct {
    GtkApplication *application;GtkWidget *window,*board,*status,*error_label,*language;
    GtkWidget *variant,*session,*target,*controllers[2],*models[2],*thinking[2],*depth[2],*iterations[2],*depth_label[2],*iteration_label[2],*model_label[2];
    GtkWidget *seconds,*memory,*seconds_label,*memory_label,*delay,*begin,*roll,*offer,*take,*drop,*confirm,*undo,*pause,*retry,*restart,*next,*configure,*refresh,*moves;
    Game game,draft,round_start;gboolean playing,paused,failed,save_failed,saved,closing,drafting,bot_sequence,ready;
    PlayerKind kinds[2];SearchOptions options[2];OllamaOptions reasoning[2];char *selected_models[2];
    GRand *dice,*random[2],*round_dice;guint32 seed;GString *history;char *details,*error,*url,*preferences;
    GArray *choices;TurnPlan draft_plan,staged;unsigned staged_index;Step pending;Decision prepared;
    Animation animation;guint tick,generation,progress_source;int selected,cursor;
    GTask *task;GCancellable *cancel;SearchResult last_search;
} App;
static void update(App *app);
static void schedule(App *app);
static void refresh_choices(App *app);
static void animate_step(App *app,Step step);
static void confirm_turn(App *app);
static const Game *visible_game(App *app){return app->drafting?&app->draft:&app->game;}
static GtkWidget *label(const char *message)
{
    GtkWidget *w=gtk_label_new(_(message));gtk_label_set_xalign(GTK_LABEL(w),0);gtk_label_set_wrap(GTK_LABEL(w),TRUE);
    g_object_set_data_full(G_OBJECT(w),"message",g_strdup(message),g_free);return w;
}
static GtkWidget *button(const char *message,GCallback callback,App *app)
{
    GtkWidget *w=gtk_button_new_with_label(_(message));g_object_set_data_full(G_OBJECT(w),"message",g_strdup(message),g_free);g_signal_connect(w,"clicked",callback,app);return w;
}
static void retranslate(GtkWidget *w)
{
    const char *message=g_object_get_data(G_OBJECT(w),"message");
    if(message){if(GTK_IS_LABEL(w))gtk_label_set_text(GTK_LABEL(w),_(message));else if(GTK_IS_BUTTON(w))gtk_button_set_label(GTK_BUTTON(w),_(message));else if(GTK_IS_CHECK_BUTTON(w))gtk_check_button_set_label(GTK_CHECK_BUTTON(w),_(message));}
    for(GtkWidget *c=gtk_widget_get_first_child(w);c;c=gtk_widget_get_next_sibling(c))retranslate(c);
}
static void set_error(App *app,const char *message)
{
    g_free(app->error);app->error=g_strdup(message);if(app->error_label){gtk_label_set_text(GTK_LABEL(app->error_label),message?_(message):"");gtk_widget_set_visible(app->error_label,message!=NULL);}
}
static void cancel_work(App *app)
{
    app->generation++;if(app->cancel)g_cancellable_cancel(app->cancel);
    if(app->progress_source){g_source_remove(app->progress_source);app->progress_source=0;}
    if(app->tick){gtk_widget_remove_tick_callback(app->board,app->tick);app->tick=0;}
    app->animation=(Animation){0};app->ready=app->bot_sequence=FALSE;g_clear_pointer(&app->details,g_free);
}
static void dice_event(App *app,gboolean opening)
{
    unsigned a=g_rand_int_range(app->dice,1,7),b=g_rand_int_range(app->dice,1,7);
    g_string_append_printf(app->history,"%s: %u-%u\n\n",opening?"Opening roll":"Dice",a,b);
    if(opening)game_opening(&app->game,a,b);else game_roll(&app->game,a,b);
    if(app->game.phase==PLAY){app->draft=app->game;app->drafting=TRUE;app->draft_plan=(TurnPlan){0};}
}
static void save_game(App *app)
{
    if(app->saved || !game_finished(&app->game))return;
    GError *error=NULL;g_autoptr(GDateTime) date=g_date_time_new_now_local();g_autofree char *time=g_date_time_format_iso8601(date);
    GString *report=g_string_new("# Tavli game\n\n");
    g_string_append_printf(report,"%s; seed %u; variant %s; session %u; game %u; target %u; score %u-%u; winner %d; reason %s; points %u; cube %u; Crawford %s.\n\n",time,app->seed,variant_name(app->game.variant),app->game.match.session,app->game.match.number+1,app->game.match.target,app->game.match.score[0],app->game.match.score[1],app->game.winner+1,reason_name(app->game.reason),app->game.award,app->game.cube,app->game.crawford?"yes":"no");
    for(int p=0;p<2;p++)g_string_append_printf(report,"Player %d: %s; model %s; depth %u; simulations %u; time %.3f s; memory %zu; thinking %s.\n",p+1,player_name(app->kinds[p]),app->selected_models[p]?app->selected_models[p]:"",app->options[p].depth,app->options[p].iterations,app->options[p].seconds,app->options[p].memory_bytes,app->reasoning[p].thinking?"on":"off");
    g_string_append_printf(report,"\n%s\n---\n\n",app->history->str);
    if(game_append_report(GAME_HISTORY_FILE,report->str,&error)){app->saved=TRUE;app->save_failed=FALSE;set_error(app,NULL);}
    else{app->save_failed=TRUE;set_error(app,error->message);g_clear_error(&error);}g_string_free(report,TRUE);
}
static void round_snapshot(App *app)
{
    app->round_start=app->game;if(app->round_dice)g_rand_free(app->round_dice);app->round_dice=g_rand_copy(app->dice);
    app->drafting=FALSE;app->draft_plan=(TurnPlan){0};app->selected=-1;app->saved=app->save_failed=app->failed=FALSE;g_string_truncate(app->history,0);search_result_clear(&app->last_search);
}
static SearchOptions read_options(App *app,int p)
{
    return (SearchOptions){gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->depth[p])),gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->iterations[p])),gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->seconds)),(gsize)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->memory))*1024*1024,0};
}
static void begin_clicked(GtkButton *button_widget,gpointer data)
{
    (void)button_widget;App *app=data;cancel_work(app);app->playing=FALSE;set_error(app,NULL);
    Variant v=gtk_drop_down_get_selected(GTK_DROP_DOWN(app->variant));Session mode=gtk_drop_down_get_selected(GTK_DROP_DOWN(app->session));
    game_init(&app->game,v,mode,gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->target)));
    for(int p=0;p<2;p++) {
        app->kinds[p]=gtk_drop_down_get_selected(GTK_DROP_DOWN(app->controllers[p]));app->options[p]=read_options(app,p);app->reasoning[p].thinking=gtk_check_button_get_active(GTK_CHECK_BUTTON(app->thinking[p]));
        GtkStringObject *model=gtk_drop_down_get_selected_item(GTK_DROP_DOWN(app->models[p]));g_free(app->selected_models[p]);app->selected_models[p]=g_strdup(model?gtk_string_object_get_string(model):"");
        if(app->kinds[p]==PLAYER_OLLAMA && !*app->selected_models[p]){set_error(app,_("Select an Ollama model first."));update(app);return;}
    }
    app->seed=g_random_int();g_rand_set_seed(app->dice,app->seed);g_rand_set_seed(app->random[0],app->seed^0x9e3779b9u);g_rand_set_seed(app->random[1],app->seed^0x85ebca6bu);
    round_snapshot(app);app->playing=TRUE;app->paused=FALSE;refresh_choices(app);update(app);schedule(app);
}
static void new_configuration(GtkButton *button_widget,gpointer data)
{
    (void)button_widget;App *app=data;cancel_work(app);app->playing=FALSE;app->drafting=FALSE;app->paused=app->failed=app->saved=app->save_failed=FALSE;app->selected=-1;search_result_clear(&app->last_search);set_error(app,NULL);
    game_init(&app->game,gtk_drop_down_get_selected(GTK_DROP_DOWN(app->variant)),gtk_drop_down_get_selected(GTK_DROP_DOWN(app->session)),gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->target)));refresh_choices(app);update(app);
}
static void restart_clicked(GtkButton *button_widget,gpointer data)
{
    (void)button_widget;App *app=data;if(!app->playing || game_finished(&app->game))return;
    cancel_work(app);app->game=app->round_start;g_rand_free(app->dice);app->dice=g_rand_copy(app->round_dice);app->paused=app->failed=FALSE;app->drafting=FALSE;app->draft_plan=(TurnPlan){0};g_string_truncate(app->history,0);set_error(app,NULL);refresh_choices(app);update(app);schedule(app);
}
static void next_clicked(GtkButton *button_widget,gpointer data)
{
    (void)button_widget;App *app=data;if(!app->saved || app->game.match.session==SESSION_SINGLE || !game_next(&app->game))return;
    cancel_work(app);app->paused=FALSE;round_snapshot(app);refresh_choices(app);update(app);schedule(app);
}
static void pause_clicked(GtkButton *button_widget,gpointer data)
{
    (void)button_widget;App *app=data;app->paused=!app->paused;
    if(app->paused)animation_pause(&app->animation,g_get_monotonic_time());else animation_resume(&app->animation,g_get_monotonic_time());update(app);schedule(app);
}
static void undo_clicked(GtkButton *button_widget,gpointer data)
{
    (void)button_widget;App *app=data;if(!app->drafting || !app->draft_plan.length || app->animation.active || app->task || app->bot_sequence)return;
    app->draft_plan.length--;app->draft=app->game;for(unsigned i=0;i<app->draft_plan.length;i++)game_apply_step_generated(&app->draft,app->draft_plan.steps[i]);app->selected=-1;refresh_choices(app);update(app);
}
static void log_turn(App *app)
{
    g_string_append_printf(app->history,"Player %d turn:",app->game.active+1);
    if(!app->draft_plan.length)g_string_append(app->history," forced pass");
    for(unsigned i=0;i<app->draft_plan.length;i++){char text[64];step_label(app->draft_plan.steps[i],text,sizeof text);g_string_append_printf(app->history," %s",text);}
    g_string_append_printf(app->history,"\n\n%s",app->details?app->details:"");g_clear_pointer(&app->details,g_free);
}
static void confirm_turn(App *app)
{
    Game verify=app->game;if(!game_play_plan(&verify,&app->draft_plan)){app->failed=TRUE;set_error(app,_("The prepared move is no longer legal."));update(app);return;}
    log_turn(app);app->game=verify;app->drafting=app->bot_sequence=FALSE;app->selected=-1;app->draft_plan=(TurnPlan){0};
    save_game(app);refresh_choices(app);update(app);schedule(app);
}
static void confirm_clicked(GtkButton *button_widget,gpointer data)
{
    (void)button_widget;App *app=data;if(app->playing && !app->paused && !app->animation.active && app->drafting && app->kinds[app->game.actor]==PLAYER_HUMAN)confirm_turn(app);
}
static void execute_control(App *app,ActionKind kind)
{
    if(kind==ACTION_ROLL)dice_event(app,FALSE);
    else if(game_control(&app->game,kind))g_string_append_printf(app->history,"Player %d cube action: %s; stake %u.\n\n",kind==ACTION_DOUBLE?app->game.active+1:2-app->game.active,action_name(kind),app->game.cube);
    if(app->details){g_string_append(app->history,app->details);g_clear_pointer(&app->details,g_free);}
    save_game(app);refresh_choices(app);update(app);schedule(app);
}
static void control_clicked(GtkButton *widget,gpointer data)
{
    App *app=data;if(!app->playing || app->paused || app->failed || app->task || app->animation.active || app->kinds[app->game.actor]!=PLAYER_HUMAN)return;
    ActionKind kind=widget==GTK_BUTTON(app->roll)?ACTION_ROLL:widget==GTK_BUTTON(app->offer)?ACTION_DOUBLE:widget==GTK_BUTTON(app->take)?ACTION_TAKE:ACTION_DROP;
    execute_control(app,kind);
}
static void commit_step(App *app)
{
    app->animation.active=FALSE;
    if(!game_apply_step_generated(&app->draft,app->pending)){app->failed=TRUE;set_error(app,_("The prepared move is no longer legal."));update(app);return;}
    app->draft_plan.steps[app->draft_plan.length++]=app->pending;app->selected=-1;
    if(app->bot_sequence){app->staged_index++;if(app->staged_index<app->staged.length){animate_step(app,app->staged.steps[app->staged_index]);return;}confirm_turn(app);return;}
    refresh_choices(app);update(app);
}
static gboolean tick(GtkWidget *widget,GdkFrameClock *clock,gpointer data)
{
    (void)widget;App *app=data;gtk_widget_queue_draw(app->board);
    if(!app->paused && animation_frame(&app->animation,gdk_frame_clock_get_frame_time(clock)).finished){app->tick=0;commit_step(app);return G_SOURCE_REMOVE;}return G_SOURCE_CONTINUE;
}
static void animate_step(App *app,Step step)
{
    if(app->animation.active || app->draft_plan.length>=4)return;
    app->pending=step;animation_start(&app->animation,g_get_monotonic_time(),gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->delay)));
    if(!app->animation.duration){commit_step(app);return;}
    app->tick=gtk_widget_add_tick_callback(app->board,tick,app,NULL);update(app);
}
static void choose_human(App *app,unsigned id)
{
    if(!app->playing || app->paused || app->failed || app->animation.active || app->task || !app->drafting || app->kinds[app->game.actor]!=PLAYER_HUMAN || !app->choices || id>=app->choices->len)return;
    Step s=g_array_index(app->choices,Step,id);if(s.kind==ACTION_STEP)animate_step(app,s);
}
static void move_clicked(GtkButton *widget,gpointer data){choose_human(data,GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget),"choice")));}
static void refresh_choices(App *app)
{
    g_clear_pointer(&app->choices,g_array_unref);
    if(app->playing && app->drafting && !game_finished(&app->draft)){RuleControl control={.memory_bytes=(gsize)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->memory))*1024*1024};app->choices=game_choices(&app->draft,&control);if(!app->choices){app->failed=TRUE;set_error(app,_("Legal turns exceeded the memory limit."));}}
    if(!app->moves)return;
    GtkWidget *child;while((child=gtk_widget_get_first_child(app->moves)))gtk_box_remove(GTK_BOX(app->moves),child);
    if(app->choices)for(unsigned i=0;i<app->choices->len;i++) {
        Step s=g_array_index(app->choices,Step,i);if(s.kind!=ACTION_STEP)continue;
        char text[100],from[24],to[24];
        if(s.from==SOURCE_RESERVE)g_strlcpy(from,_("Reserve"),sizeof from);else if(s.from==SOURCE_BAR)g_strlcpy(from,_("Bar"),sizeof from);else g_snprintf(from,sizeof from,"%u",s.from+1);
        if(s.to==DEST_OFF)g_strlcpy(to,_("Off"),sizeof to);else g_snprintf(to,sizeof to,"%u",s.to+1);
        g_snprintf(text,sizeof text,"%s → %s (%s %u)",from,to,_("Die"),s.die);GtkWidget *w=gtk_button_new_with_label(text);g_object_set_data(G_OBJECT(w),"choice",GUINT_TO_POINTER(i));g_signal_connect(w,"clicked",G_CALLBACK(move_clicked),app);gtk_box_append(GTK_BOX(app->moves),w);
    }
}
static gboolean job_cancelled(gpointer data){return g_cancellable_is_cancelled(((Job *)data)->cancel);}
static void job_progress(unsigned completed,guint64 nodes,gpointer data){Job *job=data;g_mutex_lock(&job->lock);job->progress=completed;job->nodes=nodes;g_mutex_unlock(&job->lock);}
static void job_free(gpointer data)
{
    Job *job=data;search_result_clear(&job->search);g_mutex_clear(&job->lock);g_free(job->url);g_free(job->model);g_free(job->details);g_strfreev(job->models);g_free(job);
}
static void worker(GTask *task,gpointer source,gpointer task_data,GCancellable *cancel)
{
    (void)source;Job *job=task_data;GError *error=NULL;
    if(job->list){job->models=ollama_list_models(job->url,OLLAMA_LIST_TIMEOUT_MS,cancel,&error);if(error)g_task_return_error(task,error);else g_task_return_boolean(task,TRUE);return;}
    GString *details=g_string_new(NULL);
    if(job->kind==PLAYER_OLLAMA) {
        if(!ollama_choose_with_options(&job->game,job->model,job->url,OLLAMA_MOVE_TIMEOUT_MS,cancel,&job->thinking,&job->ollama,&error)){g_string_free(details,TRUE);g_task_return_error(task,error);return;}
        job->decision=job->ollama.decision;ollama_record(details,job->model,&job->ollama);
    }else {
        GRand *random=g_rand_new_with_seed(job->seed);SearchControl control={job_cancelled,job_progress,job};
        gboolean ok=search_decide(&job->game,job->kind,&job->options,&control,random,&job->search);g_rand_free(random);
        if(!ok){g_string_free(details,TRUE);g_task_return_new_error(task,G_IO_ERROR,job->search.status==SEARCH_CANCELLED?G_IO_ERROR_CANCELLED:G_IO_ERROR_FAILED,"%s",_("Search stopped before completing a decision. Adjust limits and Retry."));return;}
        job->decision=job->search.choice;
        g_string_append_printf(details,"Algorithm %s; seed %u; status %s; completed %u/%u; nodes %" G_GUINT64_FORMAT "; elapsed %.3f ms; memory %zu; truncated rollouts %u.\n\n",player_name(job->kind),job->seed,search_status_name(job->search.status),job->search.completed,job->search.requested,job->search.nodes,job->search.elapsed_us/1000.,job->search.peak_memory,job->search.rollout_cutoffs);
        if(job->search.candidates)for(unsigned i=0;i<job->search.candidates->len;i++) {
            CandidateValue *c=&g_array_index(job->search.candidates,CandidateValue,i);g_string_append_printf(details,"Candidate %u:",i);
            if(!c->decision.checker)g_string_append_printf(details," %s",action_name(c->decision.action));
            for(unsigned j=0;j<c->decision.plan.length;j++){char text[64];step_label(c->decision.plan.steps[j],text,sizeof text);g_string_append_printf(details," %s",text);}
            g_string_append_printf(details,"; value %.8f; visits %u.\n",c->value,c->visits);
        }
        g_string_append(details,"\n");
    }
    job->details=g_string_free(details,FALSE);if(g_cancellable_is_cancelled(cancel))g_task_return_new_error(task,G_IO_ERROR,G_IO_ERROR_CANCELLED,"Cancelled");else g_task_return_boolean(task,TRUE);
}
static void done(GObject *source,GAsyncResult *result,gpointer data)
{
    (void)source;App *app=data;Job *job=g_task_get_task_data(G_TASK(result));GError *error=NULL;gboolean ok=g_task_propagate_boolean(G_TASK(result),&error);
    if(app->progress_source){g_source_remove(app->progress_source);app->progress_source=0;}
    g_clear_object(&app->task);g_clear_object(&app->cancel);
    if(!app->closing && job->generation==app->generation) {
        if(job->list && ok) {
            for(int p=0;p<2;p++){GtkStringList *list=gtk_string_list_new((const char *const *)job->models);gtk_drop_down_set_model(GTK_DROP_DOWN(app->models[p]),G_LIST_MODEL(list));g_object_unref(list);}
            if(!job->models[0])set_error(app,_("No Ollama models found."));
        }else if(ok && app->playing && job->game.phase==app->game.phase && job->game.active==app->game.active && job->game.actor==app->game.actor && job->game.match.number==app->game.match.number && job->game.cube==app->game.cube && !memcmp(job->game.point,app->game.point,sizeof app->game.point) && !memcmp(job->game.remaining,app->game.remaining,sizeof app->game.remaining)) {
            app->prepared=job->decision;app->ready=TRUE;app->details=g_steal_pointer(&job->details);search_result_clear(&app->last_search);
            app->last_search=job->search;job->search=(SearchResult){0};
        }else if(error && !g_error_matches(error,G_IO_ERROR,G_IO_ERROR_CANCELLED)){if(!job->list)app->failed=TRUE;set_error(app,error->message);}
        else if(ok && !job->list){app->failed=TRUE;set_error(app,_("The prepared move is no longer legal."));}
    }
    g_clear_error(&error);if(!app->closing){update(app);schedule(app);}
}
static gboolean progress_tick(gpointer data){update(data);return G_SOURCE_CONTINUE;}
static void launch(App *app,gboolean list)
{
    if(app->task || app->closing)return;
    Job *job=g_new0(Job,1);g_mutex_init(&job->lock);job->game=app->game;job->generation=app->generation;job->list=list;int p=app->game.actor;
    job->kind=app->kinds[p];job->options=app->options[p];job->thinking=app->reasoning[p];job->seed=g_rand_int(app->random[p]);job->url=g_strdup(app->url);job->model=g_strdup(app->selected_models[p]);
    app->cancel=g_cancellable_new();job->cancel=app->cancel;app->task=g_task_new(NULL,app->cancel,done,app);g_task_set_task_data(app->task,job,job_free);g_task_set_return_on_cancel(app->task,FALSE);
    app->progress_source=g_timeout_add(100,progress_tick,app);g_task_run_in_thread(app->task,worker);update(app);
}
static void schedule(App *app)
{
    if(!app->playing || app->closing || app->paused || app->failed || app->save_failed || app->animation.active || app->bot_sequence)return;
    while(app->game.phase==OPENING)dice_event(app,TRUE);
    if(game_finished(&app->game)){save_game(app);update(app);return;}
    int p=app->game.actor;
    if(app->game.phase==PLAY && !app->drafting){app->draft=app->game;app->drafting=TRUE;app->draft_plan=(TurnPlan){0};refresh_choices(app);}
    if(app->kinds[p]==PLAYER_HUMAN){update(app);return;}
    if(app->game.phase==PRE_ROLL && !game_can_double(&app->game)){dice_event(app,FALSE);refresh_choices(app);}
    if(app->ready) {
        app->ready=FALSE;Decision d=app->prepared;
        if(!d.checker){execute_control(app,d.action);return;}
        app->draft=app->game;app->drafting=TRUE;app->draft_plan=(TurnPlan){0};app->staged=d.plan;app->staged_index=0;app->bot_sequence=TRUE;
        if(!d.plan.length){confirm_turn(app);return;}animate_step(app,d.plan.steps[0]);
    }else if(!app->task)launch(app,FALSE);
}
static void retry_clicked(GtkButton *button_widget,gpointer data)
{
    (void)button_widget;App *app=data;if(app->save_failed){save_game(app);update(app);return;}
    cancel_work(app);app->failed=FALSE;app->paused=FALSE;set_error(app,NULL);app->options[app->game.actor]=read_options(app,app->game.actor);refresh_choices(app);update(app);schedule(app);
}
static void refresh_clicked(GtkButton *button_widget,gpointer data){(void)button_widget;App *app=data;if(!app->playing && !app->task){set_error(app,NULL);launch(app,TRUE);}}
static gboolean can_confirm(App *app)
{
    return app->drafting && (game_finished(&app->draft) || (app->choices && app->choices->len==1 && g_array_index(app->choices,Step,0).kind==ACTION_END));
}
static void update(App *app)
{
    if(!app->window)return;
    gboolean config=!app->playing,model_needed=FALSE,planning=FALSE;const Game *g=visible_game(app);int actor=app->game.actor;
    for(int p=0;p<2;p++) {
        PlayerKind kind=config?gtk_drop_down_get_selected(GTK_DROP_DOWN(app->controllers[p])):app->kinds[p];gboolean model=kind==PLAYER_OLLAMA,e=kind==PLAYER_EXPECTI,m=kind==PLAYER_MCTS;model_needed|=model;planning|=e || m;
        gtk_widget_set_sensitive(app->controllers[p],config);gtk_widget_set_sensitive(app->models[p],config);gtk_widget_set_sensitive(app->thinking[p],config);
        gtk_widget_set_visible(app->models[p],model);gtk_widget_set_visible(app->model_label[p],model);gtk_widget_set_visible(app->thinking[p],model);
        gtk_widget_set_visible(app->depth[p],e);gtk_widget_set_visible(app->depth_label[p],e);gtk_widget_set_visible(app->iterations[p],m);gtk_widget_set_visible(app->iteration_label[p],m);
        gtk_widget_set_sensitive(app->depth[p],config || app->failed);gtk_widget_set_sensitive(app->iterations[p],config || app->failed);
    }
    gtk_widget_set_sensitive(app->variant,config && gtk_drop_down_get_selected(GTK_DROP_DOWN(app->session))!=SESSION_TRADITIONAL);gtk_widget_set_sensitive(app->session,config);gtk_widget_set_sensitive(app->target,config && gtk_drop_down_get_selected(GTK_DROP_DOWN(app->session))!=SESSION_SINGLE);
    gtk_widget_set_visible(app->refresh,model_needed);gtk_widget_set_sensitive(app->refresh,config && !app->task);
    gtk_widget_set_visible(app->begin,config);gtk_widget_set_visible(app->configure,!config);gtk_widget_set_visible(app->restart,!config && !game_finished(&app->game));
    gtk_widget_set_visible(app->next,!config && app->game.phase==GAME_OVER && app->game.match.session!=SESSION_SINGLE);gtk_widget_set_sensitive(app->next,app->saved);
    gtk_widget_set_visible(app->pause,!config && !game_finished(&app->game));gtk_button_set_label(GTK_BUTTON(app->pause),_(app->paused?"Resume":"Pause"));
    gboolean human=!config && app->kinds[actor]==PLAYER_HUMAN && !game_finished(&app->game),available=human && !app->paused && !app->failed && !app->task && !app->animation.active;
    gtk_widget_set_visible(app->roll,human && app->game.phase==PRE_ROLL);gtk_widget_set_visible(app->offer,human && game_can_double(&app->game));
    gtk_widget_set_visible(app->take,human && app->game.phase==CUBE_RESPONSE);gtk_widget_set_visible(app->drop,human && app->game.phase==CUBE_RESPONSE);
    gtk_widget_set_sensitive(app->roll,available);gtk_widget_set_sensitive(app->offer,available);gtk_widget_set_sensitive(app->take,available);gtk_widget_set_sensitive(app->drop,available);
    gtk_widget_set_visible(app->confirm,human && app->drafting);gtk_widget_set_sensitive(app->confirm,available && can_confirm(app));
    gtk_widget_set_visible(app->undo,human && app->drafting);gtk_widget_set_sensitive(app->undo,available && app->draft_plan.length>0);
    gtk_widget_set_visible(app->moves,human && app->drafting);gtk_widget_set_sensitive(app->moves,available);
    gtk_widget_set_visible(app->retry,app->failed || app->save_failed);gtk_widget_set_visible(app->seconds,planning || app->failed);gtk_widget_set_visible(app->seconds_label,planning || app->failed);gtk_widget_set_visible(app->memory,planning || app->failed);gtk_widget_set_visible(app->memory_label,planning || app->failed);gtk_widget_set_sensitive(app->seconds,config || app->failed);gtk_widget_set_sensitive(app->memory,config || app->failed);
    GString *text=g_string_new(NULL);g_string_append_printf(text,"%s · %s %u-%u",_(variant_name(g->variant)),_("Score"),g->match.score[0],g->match.score[1]);
    if(g->match.session==SESSION_SINGLE)g_string_append_printf(text," · %s",_("Single game"));else g_string_append_printf(text," / %u",g->match.target);
    if(g->variant==BACKGAMMON && g->match.session!=SESSION_SINGLE){
        g_string_append_printf(text," · %s %u · ",_("Cube"),g->cube);
        if(g->cube_owner<0)g_string_append(text,_("Centered"));else g_string_append_printf(text,_("Player %d"),g->cube_owner+1);
    }
    g_string_append_c(text,'\n');
    if(config)g_string_append(text,_("Choose both players and begin."));
    else if(game_finished(&app->game)) {
        if(app->game.winner<0)g_string_append(text,_("Draw!"));else g_string_append_printf(text,_("Player %d wins · %u points"),app->game.winner+1,app->game.award);
        g_string_append_printf(text," · %s",_(phase_name(app->game.phase)));
    }else if(app->paused)g_string_append(text,_("Paused"));
    else if(app->failed)g_string_append(text,_("Decision stopped. Adjust limits and Retry, or restart."));
    else if(app->animation.active)g_string_append(text,_("Moving checker…"));
    else if(can_confirm(app))g_string_append(text,_(game_finished(&app->draft)?"Turn ready. Confirm or undo a step.":"No more legal moves. Confirm to end the turn."));
    else g_string_append_printf(text,_("Player %d · %s"),actor+1,_(phase_name(app->game.phase)));
    if(app->game.phase==PLAY) {
        g_string_append_printf(text,"\n%s: %u-%u · %s:",_("Dice"),g->roll[0],g->roll[1],_("Remaining"));
        for(unsigned d=1;d<=6;d++)for(unsigned n=0;n<g->remaining[d];n++)g_string_append_printf(text," %u",d);
    }
    if(g->crawford)g_string_append(text," · Crawford");
    if(app->task){Job *job=g_task_get_task_data(app->task);if(!job->list && job->generation==app->generation){g_mutex_lock(&job->lock);g_string_append_printf(text,_("\nCompleted %u · nodes %" G_GUINT64_FORMAT),job->progress,job->nodes);g_mutex_unlock(&job->lock);}}
    else if(app->last_search.requested){g_string_append_printf(text,_("\nLast analysis: %u/%u"),app->last_search.completed,app->last_search.requested);if(app->last_search.status==SEARCH_LIMIT)g_string_append(text,_(" · resource limit"));}
    gtk_label_set_text(GTK_LABEL(app->status),text->str);g_string_free(text,TRUE);gtk_widget_queue_draw(app->board);
}
/* All physical coordinates remain in Player 1's view; points 1 and 24 are rightmost. */
static void transform(App *app,double *scale,double *x,double *y)
{
    double w=gtk_widget_get_width(app->board),h=gtk_widget_get_height(app->board);*scale=MIN(w/1000.,h/640.);*x=(w-1000**scale)/2;*y=(h-640**scale)/2;
}
static void point_center(unsigned q,double *x,double *y)
{
    unsigned col=q<12?11-q:q-12;*x=130+col*65+(col>=6?40:0);*y=q<12?520:120;
}
static void checker(cairo_t *cr,double x,double y,int p,double alpha,double radius)
{
    cairo_new_sub_path(cr);cairo_arc(cr,x,y,radius,0,2*G_PI);if(!p)cairo_set_source_rgba(cr,.96,.93,.82,alpha);else cairo_set_source_rgba(cr,.76,.18,.21,alpha);
    cairo_fill_preserve(cr);cairo_set_line_width(cr,2);cairo_set_source_rgba(cr,.16,.12,.11,alpha);cairo_stroke(cr);
}
static void location(const Game *g,int player,unsigned where,double *x,double *y)
{
    if(where<24){point_center(where,x,y);unsigned count=MIN(5u,(unsigned)g->point[where][player]+(g->pinned[where]>=0 && g->pinned[where]!=player));*y+=(where<12?-29.0:29.0)*(count?count-1:0);}
    else if(where==SOURCE_BAR){*x=500;*y=player?220:420;}
    else if(where==SOURCE_RESERVE){*x=player && g->variant!=RUNNING?45:955;*y=player?(g->variant==RUNNING?365:155):465;}
    else{*x=player && (g->variant==FEVGA)?45:955;*y=player?80:560;}
}
static void text_at(cairo_t *cr,double x,double y,const char *text,double size)
{
    cairo_set_source_rgb(cr,.95,.93,.87);cairo_set_font_size(cr,size);cairo_move_to(cr,x,y);cairo_show_text(cr,text);
}
static void draw(GtkDrawingArea *area,cairo_t *cr,int width,int height,gpointer data)
{
    (void)area;(void)width;(void)height;App *app=data;const Game *g=visible_game(app);double scale,x,y;transform(app,&scale,&x,&y);
    cairo_set_source_rgb(cr,.07,.1,.12);cairo_paint(cr);cairo_translate(cr,x,y);cairo_scale(cr,scale,scale);
    cairo_rectangle(cr,85,85,830,470);cairo_set_source_rgb(cr,.30,.19,.12);cairo_fill(cr);
    for(unsigned q=0;q<24;q++) {
        double px,py;point_center(q,&px,&py);gboolean lower=q<12;
        cairo_move_to(cr,px-29,lower?545:95);cairo_line_to(cr,px+29,lower?545:95);cairo_line_to(cr,px,lower?355:285);cairo_close_path(cr);
        cairo_set_source_rgb(cr,q%2?.63:.20,q%2?.39:.27,q%2?.22:.28);cairo_fill(cr);
        char number[8];g_snprintf(number,sizeof number,"%u",q+1);text_at(cr,px-8,lower?580:76,number,17);
        gboolean legal=FALSE;
        if(app->choices)for(unsigned i=0;i<app->choices->len;i++){Step s=g_array_index(app->choices,Step,i);legal|=s.kind==ACTION_STEP && s.from==(unsigned)app->selected && s.to==q;}
        if(legal){cairo_new_sub_path(cr);cairo_arc(cr,px,lower?347:293,8,0,2*G_PI);cairo_set_source_rgb(cr,1,.84,.2);cairo_fill(cr);}
        if((int)q==app->selected || ((int)q==app->cursor && app->playing && app->kinds[app->game.actor]==PLAYER_HUMAN)){cairo_rectangle(cr,px-30,lower?350:90,60,200);cairo_set_source_rgba(cr,1,.84,.2,(int)q==app->selected?1:.35);cairo_set_line_width(cr,2);cairo_stroke(cr);}
        int bottom=g->pinned[q],top=g->point[q][0]?0:1;if(bottom>=0)top=1-bottom;
        unsigned total=g->point[q][0]+g->point[q][1];
        if(total) {
            if(bottom>=0){checker(cr,px,py,bottom,1,24);text_at(cr,px-5,py+(lower?12:-5),"P",14);}
            unsigned count=MIN(5u,total);for(unsigned n=bottom>=0?1:0;n<count;n++){
                gboolean moving=app->animation.active && app->pending.from==q && g->active==top && n+1==count;
                gboolean captured=app->animation.active && app->pending.to==q && total==1 && top!=g->active && (g->variant==PORTES || g->variant==BACKGAMMON);
                if(!moving && !captured)checker(cr,px,py+(lower?-29.0:29.0)*n,top,1,24);
            }
            if(total>5){char count_text[12];g_snprintf(count_text,sizeof count_text,"%u",total);text_at(cr,px-8,py+(lower?-29.0:29.0)*4+5,count_text,17);}
        }
    }
    cairo_rectangle(cr,480,90,40,460);cairo_set_source_rgb(cr,.13,.09,.07);cairo_fill(cr);
    for(int p=0;p<2;p++) {
        const unsigned locations[]={SOURCE_RESERVE,SOURCE_BAR,DEST_OFF};const unsigned counts[]={g->reserve[p],g->bar[p],g->off[p]};
        for(unsigned i=0;i<3;i++) {
            double px,py;location(g,p,locations[i],&px,&py);
            if(!(app->animation.active && app->pending.from==locations[i] && p==g->active))checker(cr,px,py,p,counts[i]?1:.3,23);
            char count_text[16];g_snprintf(count_text,sizeof count_text,"%u",counts[i]);
            cairo_set_source_rgb(cr,p?1:.12,p?1:.12,p?1:.12);cairo_set_font_size(cr,18);cairo_move_to(cr,px-7,py+6);cairo_show_text(cr,count_text);
            text_at(cr,px-22,py+41,_(i==0?"Reserve":i==1?"Bar":"Off"),12);
        }
    }
    for(unsigned i=0;i<2;i++)if(g->roll[i]) {
        double dx=440+i*80;cairo_rectangle(cr,dx,303,42,42);cairo_set_source_rgb(cr,.92,.87,.71);cairo_fill(cr);char die[4];g_snprintf(die,sizeof die,"%u",g->roll[i]);cairo_set_source_rgb(cr,.12,.1,.08);cairo_set_font_size(cr,28);cairo_move_to(cr,dx+12,334);cairo_show_text(cr,die);
    }
    /* Direction arrows are fixed to each player's physical route, not the turn. */
    for(int p=0;p<2;p++){
        double ax,ay,bx,by;point_center(game_point_at(g,p,0),&ax,&ay);point_center(game_point_at(g,p,3),&bx,&by);ay=by=game_point_at(g,p,0)<12?610:35;
        if(!p)cairo_set_source_rgb(cr,.96,.93,.82);else cairo_set_source_rgb(cr,.9,.3,.33);
        cairo_move_to(cr,ax,ay);cairo_line_to(cr,bx,by);cairo_set_line_width(cr,3);cairo_stroke(cr);double dir=bx>ax?1:-1;
        cairo_move_to(cr,bx,by);cairo_line_to(cr,bx-dir*12,by-6);cairo_line_to(cr,bx-dir*12,by+6);cairo_close_path(cr);cairo_fill(cr);
    }
    if(app->animation.active) {
        AnimationFrame frame=animation_frame(&app->animation,g_get_monotonic_time());double ax,ay,bx,by;
        location(g,g->active,app->pending.from,&ax,&ay);Game landing=*g;game_apply_step_generated(&landing,app->pending);location(&landing,g->active,app->pending.to,&bx,&by);
        unsigned d=app->pending.die;double progress=frame.position*d;unsigned segment=MIN(d-1,(unsigned)progress);double part=progress-segment;
        int index=app->pending.from<24?game_index(g,g->active,app->pending.from):-1;
        if(frame.position>0 && d) {
            if(segment){unsigned point=game_point_at(g,g->active,index+segment);location(g,g->active,point,&ax,&ay);}
            if(segment+1<d){unsigned point=game_point_at(g,g->active,index+segment+1);location(g,g->active,point,&bx,&by);}
        }
        checker(cr,ax+(bx-ax)*part,ay+(by-ay)*part,g->active,frame.opacity,25);
        if(app->pending.to<24 && (g->variant==PORTES || g->variant==BACKGAMMON) && g->point[app->pending.to][1-g->active]==1){
            double hx,hy,tx,ty;location(g,1-g->active,app->pending.to,&hx,&hy);location(g,1-g->active,SOURCE_BAR,&tx,&ty);
            checker(cr,hx+(tx-hx)*frame.position,hy+(ty-hy)*frame.position,1-g->active,1,24);
        }
    }
}
static void select_location(App *app,unsigned where)
{
    if(!app->playing || app->paused || app->animation.active || !app->drafting || app->kinds[app->game.actor]!=PLAYER_HUMAN)return;
    unsigned match=G_MAXUINT,count=0;
    if(app->choices && app->selected>=0)for(unsigned i=0;i<app->choices->len;i++){Step s=g_array_index(app->choices,Step,i);if(s.kind==ACTION_STEP && s.from==(unsigned)app->selected && s.to==where){match=i;count++;}}
    if(count==1){choose_human(app,match);return;}
    if(count>1){set_error(app,_("Choose the die using the legal-move buttons."));update(app);return;}
    if(app->choices)for(unsigned i=0;i<app->choices->len;i++)if(g_array_index(app->choices,Step,i).from==where){app->selected=where;set_error(app,NULL);update(app);return;}
    app->selected=-1;update(app);
}
static void click(GtkGestureClick *gesture,int count,double px,double py,gpointer data)
{
    (void)gesture;(void)count;App *app=data;double scale,x,y;transform(app,&scale,&x,&y);if(scale<=0)return;px=(px-x)/scale;py=(py-y)/scale;gtk_widget_grab_focus(app->board);
    const Game *g=visible_game(app);int p=app->game.actor;
    const unsigned trays[]={SOURCE_RESERVE,SOURCE_BAR,DEST_OFF};for(unsigned i=0;i<3;i++){double tx,ty;location(g,p,trays[i],&tx,&ty);if(hypot(px-tx,py-ty)<30){select_location(app,trays[i]);return;}}
    for(unsigned q=0;q<24;q++){double qx,qy;point_center(q,&qx,&qy);if(fabs(px-qx)<30 && (q<12?py>=350 && py<=550:py>=90 && py<=290)){app->cursor=q;select_location(app,q);return;}}
}
static gboolean key_pressed(GtkEventControllerKey *controller,guint key,guint code,GdkModifierType modifiers,gpointer data)
{
    (void)controller;(void)code;(void)modifiers;App *app=data;int q=app->cursor;
    if(key==GDK_KEY_Left)q+=q<12?1:-1;else if(key==GDK_KEY_Right)q+=q<12?-1:1;else if(key==GDK_KEY_Up || key==GDK_KEY_Down)q=23-q;
    else if(key==GDK_KEY_Return || key==GDK_KEY_space){select_location(app,q);return TRUE;}else if(key==GDK_KEY_Escape){app->selected=-1;update(app);return TRUE;}else return FALSE;
    app->cursor=CLAMP(q,0,23);update(app);return TRUE;
}
static void configuration_changed(GObject *object,GParamSpec *spec,gpointer data)
{
    (void)object;(void)spec;App *app=data;if(!app->playing){game_init(&app->game,gtk_drop_down_get_selected(GTK_DROP_DOWN(app->variant)),gtk_drop_down_get_selected(GTK_DROP_DOWN(app->session)),gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->target)));}update(app);
}
static void language_changed(GObject *object,GParamSpec *spec,gpointer data)
{
    (void)spec;App *app=data;i18n_set_language(gtk_drop_down_get_selected(GTK_DROP_DOWN(object)));
    for(int p=0;p<2;p++){guint selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(app->controllers[p]));const char *names[PLAYER_COUNT+1];for(unsigned k=0;k<PLAYER_COUNT;k++)names[k]=_(player_name(k));names[PLAYER_COUNT]=NULL;GtkStringList *list=gtk_string_list_new(names);gtk_drop_down_set_model(GTK_DROP_DOWN(app->controllers[p]),G_LIST_MODEL(list));g_object_unref(list);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[p]),selected);}
    const char *sessions[]={_("Single game"),_("Fixed-variant match"),_("Traditional Tavli cycle"),NULL};guint selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(app->session));GtkStringList *list=gtk_string_list_new(sessions);gtk_drop_down_set_model(GTK_DROP_DOWN(app->session),G_LIST_MODEL(list));g_object_unref(list);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->session),selected);
    retranslate(app->window);refresh_choices(app);update(app);GError *error=NULL;g_autofree char *parent=g_path_get_dirname(app->preferences);
    if(g_mkdir_with_parents(parent,0700)<0 || !i18n_save(app->preferences,&error)){set_error(app,error?error->message:_("Could not save language preference."));g_clear_error(&error);}
}
static gboolean close_window(GtkWindow *window,gpointer data){(void)window;App *app=data;app->closing=TRUE;cancel_work(app);app->window=NULL;return FALSE;}
static GtkWidget *spin(GtkWidget *parent,double min,double max,double step,double value)
{
    GtkWidget *w=gtk_spin_button_new_with_range(min,max,step);gtk_spin_button_set_value(GTK_SPIN_BUTTON(w),value);gtk_box_append(GTK_BOX(parent),w);return w;
}
static void activate(GtkApplication *application,gpointer data)
{
    App *app=data;if(app->window){gtk_window_present(GTK_WINDOW(app->window));return;}
    app->window=adw_application_window_new(application);gtk_window_set_title(GTK_WINDOW(app->window),_(GAME_NAME));gtk_window_set_default_size(GTK_WINDOW(app->window),1200,850);
    GtkWidget *root=gtk_box_new(GTK_ORIENTATION_VERTICAL,0),*header=adw_header_bar_new();gtk_box_append(GTK_BOX(root),header);
    const char *languages[]={"System","English","Português","Español","Italiano",NULL};app->language=gtk_drop_down_new_from_strings(languages);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language),i18n_selection());adw_header_bar_pack_end(ADW_HEADER_BAR(header),app->language);
    GtkWidget *body=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);gtk_widget_set_margin_start(body,12);gtk_widget_set_margin_end(body,12);gtk_widget_set_margin_bottom(body,12);gtk_widget_set_vexpand(body,TRUE);gtk_box_append(GTK_BOX(root),body);
    GtkWidget *sidebar=gtk_box_new(GTK_ORIENTATION_VERTICAL,7),*scroll=gtk_scrolled_window_new();gtk_widget_set_size_request(scroll,260,-1);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),sidebar);gtk_box_append(GTK_BOX(body),scroll);
    gtk_box_append(GTK_BOX(sidebar),label("Variant"));const char *variants[]={"Portes","Plakoto","Fevga","Backgammon","Running",NULL};app->variant=gtk_drop_down_new_from_strings(variants);gtk_box_append(GTK_BOX(sidebar),app->variant);
    gtk_box_append(GTK_BOX(sidebar),label("Session"));const char *sessions[]={_("Single game"),_("Fixed-variant match"),_("Traditional Tavli cycle"),NULL};app->session=gtk_drop_down_new_from_strings(sessions);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->session),SESSION_FIXED);gtk_box_append(GTK_BOX(sidebar),app->session);
    gtk_box_append(GTK_BOX(sidebar),label("Match target"));app->target=spin(sidebar,1,25,1,5);
    for(int p=0;p<2;p++) {
        gtk_box_append(GTK_BOX(sidebar),label(p?"Player 2 — Red":"Player 1 — White"));const char *names[PLAYER_COUNT+1];for(unsigned k=0;k<PLAYER_COUNT;k++)names[k]=_(player_name(k));names[PLAYER_COUNT]=NULL;
        app->controllers[p]=gtk_drop_down_new_from_strings(names);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[p]),p?PLAYER_HEURISTIC:PLAYER_HUMAN);gtk_box_append(GTK_BOX(sidebar),app->controllers[p]);
        app->depth_label[p]=label("Search depth");gtk_box_append(GTK_BOX(sidebar),app->depth_label[p]);app->depth[p]=spin(sidebar,1,6,1,2);
        app->iteration_label[p]=label("MCTS iterations");gtk_box_append(GTK_BOX(sidebar),app->iteration_label[p]);app->iterations[p]=spin(sidebar,1,1000000,100,1000);
        app->model_label[p]=label("Ollama model");gtk_box_append(GTK_BOX(sidebar),app->model_label[p]);const char *empty[]={NULL};app->models[p]=gtk_drop_down_new_from_strings(empty);gtk_box_append(GTK_BOX(sidebar),app->models[p]);
        app->thinking[p]=gtk_check_button_new_with_label(_("Extended thinking"));g_object_set_data_full(G_OBJECT(app->thinking[p]),"message",g_strdup("Extended thinking"),g_free);gtk_box_append(GTK_BOX(sidebar),app->thinking[p]);
    }
    app->refresh=button("Refresh models",G_CALLBACK(refresh_clicked),app);gtk_box_append(GTK_BOX(sidebar),app->refresh);
    app->seconds_label=label("Planning seconds (0: unlimited)");gtk_box_append(GTK_BOX(sidebar),app->seconds_label);app->seconds=spin(sidebar,0,18000,1,5);
    app->memory_label=label("Solver memory limit (MiB)");gtk_box_append(GTK_BOX(sidebar),app->memory_label);app->memory=spin(sidebar,1,4096,16,64);
    gtk_box_append(GTK_BOX(sidebar),label("Animation delay (seconds)"));app->delay=spin(sidebar,0,3,.1,.5);gtk_spin_button_set_digits(GTK_SPIN_BUTTON(app->delay),1);
    app->begin=button("Begin",G_CALLBACK(begin_clicked),app);gtk_widget_add_css_class(app->begin,"suggested-action");gtk_box_append(GTK_BOX(sidebar),app->begin);
    app->pause=button("Pause",G_CALLBACK(pause_clicked),app);gtk_box_append(GTK_BOX(sidebar),app->pause);
    app->retry=button("Retry",G_CALLBACK(retry_clicked),app);gtk_box_append(GTK_BOX(sidebar),app->retry);
    app->restart=button("Restart current game",G_CALLBACK(restart_clicked),app);gtk_box_append(GTK_BOX(sidebar),app->restart);
    app->next=button("Next game",G_CALLBACK(next_clicked),app);gtk_box_append(GTK_BOX(sidebar),app->next);
    app->configure=button("New match / configuration",G_CALLBACK(new_configuration),app);gtk_box_append(GTK_BOX(sidebar),app->configure);
    gtk_box_append(GTK_BOX(sidebar),label("Legal checker moves"));app->moves=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);gtk_box_append(GTK_BOX(sidebar),app->moves);
    GtkWidget *right=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);gtk_widget_set_hexpand(right,TRUE);gtk_box_append(GTK_BOX(body),right);
    app->status=gtk_label_new(NULL);gtk_label_set_wrap(GTK_LABEL(app->status),TRUE);gtk_label_set_xalign(GTK_LABEL(app->status),0);gtk_box_append(GTK_BOX(right),app->status);
    app->error_label=gtk_label_new(NULL);gtk_label_set_wrap(GTK_LABEL(app->error_label),TRUE);gtk_widget_add_css_class(app->error_label,"error");gtk_box_append(GTK_BOX(right),app->error_label);gtk_widget_set_visible(app->error_label,FALSE);
    GtkWidget *actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);gtk_box_append(GTK_BOX(right),actions);
    app->roll=button("Roll dice",G_CALLBACK(control_clicked),app);app->offer=button("Offer double",G_CALLBACK(control_clicked),app);app->take=button("Take",G_CALLBACK(control_clicked),app);app->drop=button("Drop",G_CALLBACK(control_clicked),app);
    app->confirm=button("Confirm turn",G_CALLBACK(confirm_clicked),app);app->undo=button("Undo step",G_CALLBACK(undo_clicked),app);
    GtkWidget *buttons[]={app->roll,app->offer,app->take,app->drop,app->confirm,app->undo};for(unsigned i=0;i<G_N_ELEMENTS(buttons);i++)gtk_box_append(GTK_BOX(actions),buttons[i]);
    app->board=gtk_drawing_area_new();gtk_widget_set_size_request(app->board,640,410);gtk_widget_set_hexpand(app->board,TRUE);gtk_widget_set_vexpand(app->board,TRUE);gtk_widget_set_focusable(app->board,TRUE);gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->board),draw,app,NULL);gtk_box_append(GTK_BOX(right),app->board);
    gtk_box_append(GTK_BOX(right),label("Player 1's view stays fixed. Select a source and destination, or use a legal-move button. Confirm commits the turn; Undo step keeps the dice. P marks a pinned checker."));
    GtkGesture *gesture=gtk_gesture_click_new();g_signal_connect(gesture,"pressed",G_CALLBACK(click),app);gtk_widget_add_controller(app->board,GTK_EVENT_CONTROLLER(gesture));
    GtkEventController *keys=gtk_event_controller_key_new();g_signal_connect(keys,"key-pressed",G_CALLBACK(key_pressed),app);gtk_widget_add_controller(app->board,keys);
    g_signal_connect(app->variant,"notify::selected",G_CALLBACK(configuration_changed),app);g_signal_connect(app->session,"notify::selected",G_CALLBACK(configuration_changed),app);
    for(int p=0;p<2;p++)g_signal_connect(app->controllers[p],"notify::selected",G_CALLBACK(configuration_changed),app);
    g_signal_connect(app->language,"notify::selected",G_CALLBACK(language_changed),app);g_signal_connect(app->window,"close-request",G_CALLBACK(close_window),app);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(app->window),root);update(app);gtk_window_present(GTK_WINDOW(app->window));
}
static void app_init(App *app)
{
    app->selected=-1;app->cursor=23;app->dice=g_rand_new();for(int p=0;p<2;p++)app->random[p]=g_rand_new();app->history=g_string_new(NULL);game_init(&app->game,PORTES,SESSION_FIXED,5);
    app->url=g_strdup(g_getenv("OLLAMA_URL"));app->preferences=g_build_filename(g_get_user_config_dir(),"ai-games","tavli.ini",NULL);
}
static void app_clear(App *app)
{
    app->closing=TRUE;cancel_work(app);while(app->task)g_main_context_iteration(NULL,TRUE);g_rand_free(app->dice);if(app->round_dice)g_rand_free(app->round_dice);
    for(int p=0;p<2;p++){g_rand_free(app->random[p]);g_free(app->selected_models[p]);}g_string_free(app->history,TRUE);g_free(app->details);g_free(app->error);g_free(app->url);g_free(app->preferences);g_clear_pointer(&app->choices,g_array_unref);search_result_clear(&app->last_search);
}
int main(int argc,char **argv)
{
    App app={0};app_init(&app);GError *error=NULL;if(!i18n_load(app.preferences,&error)){g_printerr("%s\n",error->message);g_clear_error(&error);}
    app.application=GTK_APPLICATION(adw_application_new("io.github.aigames.Tavli",G_APPLICATION_DEFAULT_FLAGS));g_signal_connect(app.application,"activate",G_CALLBACK(activate),&app);
    int status=g_application_run(G_APPLICATION(app.application),argc,argv);app_clear(&app);g_object_unref(app.application);return status;
}
