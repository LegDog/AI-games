// SPDX-License-Identifier: GPL-3.0-or-later
#include <adwaita.h>
#include "../../tools/ui-panels.h"
#include "../../tools/ollama-view.h"
#include "../../tools/ollama-controls.h"
#include <math.h>
#include <glib/gstdio.h>
#include "animation.h"
#include "search.h"
#include "ollama.h"
#include "i18n.h"
typedef struct {
    Game game;PlayerKind kind;SearchOptions options;OllamaOptions ollama_options;
    guint generation;guint32 seed;gboolean list;int chosen;
    char *model,*url,*details;char **models;
    SearchResult search;OllamaResult ollama;
    GCancellable *cancel;GMutex mutex;unsigned progress;guint64 nodes;
} Job;
typedef struct {
    GtkApplication *application;GtkWidget *window,*board,*status,*error_label,*language;
    GtkWidget *controllers[2],*models[2],*depth[2],*depth_label[2],*iterations[2],*iteration_label[2],*thinking[2],*model_label[2];
    GtkWidget *size_choice,*swap_rule,*swap_move;
    GtkWidget *seconds_label,*memory_label,*seconds,*memory,*delay,*begin,*restart,*configure,*pause,*undo,*refresh,*retry;
    PlayerKind kinds[2];SearchOptions options[2];OllamaOptions ollama_options[2];char *selected_models[2];
    Game game;Animation animation;Game snapshots[GAME_MAX_ACTIONS];gsize history_lengths[GAME_MAX_ACTIONS];unsigned actions;
    GRand *random;guint32 seed;GString *history;char *pending_details,*error,*url,*preferences;
    gboolean playing,paused,ready,failed,saved,save_failed,closing;
    int pending,cursor;guint generation,tick,progress_source;
    SearchResult last_search;
    GTask *task;GCancellable *cancel;
} App;
static void schedule(App *app);
static void update(App *app);
static void start_animation(App *app,int square);
static unsigned side(int player){return player==1?0:1;}
static const char *color(int player){return player==1?"Red":"Blue";}
static GtkWidget *label(const char *text)
{
    GtkWidget *widget=gtk_label_new(_(text));gtk_label_set_xalign(GTK_LABEL(widget),0);
    g_object_set_data_full(G_OBJECT(widget),"message",g_strdup(text),g_free);return widget;
}
static GtkWidget *button(const char *text,GCallback callback,App *app)
{
    GtkWidget *widget=gtk_button_new_with_label(_(text));g_object_set_data_full(G_OBJECT(widget),"message",g_strdup(text),g_free);
    g_signal_connect(widget,"clicked",callback,app);return widget;
}
static void retranslate(GtkWidget *widget)
{
    const char *message=g_object_get_data(G_OBJECT(widget),"message");
    if(message){if(GTK_IS_LABEL(widget))gtk_label_set_text(GTK_LABEL(widget),_(message));else if(GTK_IS_BUTTON(widget))gtk_button_set_label(GTK_BUTTON(widget),_(message));else if(GTK_IS_CHECK_BUTTON(widget))gtk_check_button_set_label(GTK_CHECK_BUTTON(widget),_(message));}
    for(GtkWidget *child=gtk_widget_get_first_child(widget);child;child=gtk_widget_get_next_sibling(child))retranslate(child);
}
static void set_error(App *app,const char *message)
{
    g_free(app->error);app->error=g_strdup(message);
    if(app->error_label){gtk_label_set_text(GTK_LABEL(app->error_label),message?_(message):"");gtk_widget_set_visible(app->error_label,message!=NULL);}
}
static void cancel_work(App *app)
{
    ollama_view_invalidate(GTK_WINDOW(app->window));
    app->generation++;if(app->cancel)g_cancellable_cancel(app->cancel);
    if(app->progress_source){g_source_remove(app->progress_source);app->progress_source=0;}
    if(app->tick){gtk_widget_remove_tick_callback(app->board,app->tick);app->tick=0;}
    app->animation=(Animation){0};app->ready=FALSE;app->pending=-1;
    g_clear_pointer(&app->pending_details,g_free);
}
static void clear_attempt(App *app)
{
    ollama_view_clear(GTK_WINDOW(app->window));
    cancel_work(app);unsigned size=3+2*gtk_drop_down_get_selected(GTK_DROP_DOWN(app->size_choice));game_init_variant(&app->game,size,gtk_check_button_get_active(GTK_CHECK_BUTTON(app->swap_rule)));app->actions=0;app->paused=app->failed=app->saved=app->save_failed=FALSE;
    app->last_search=(SearchResult){0};
    app->cursor=0;set_error(app,NULL);g_string_truncate(app->history,0);
    app->seed=g_random_int();g_rand_set_seed(app->random,app->seed);
}
static void save_attempt(App *app)
{
    if(app->saved || !game_result(&app->game))return;
    g_autoptr(GDateTime) now=g_date_time_new_now_local();g_autofree char *time=g_date_time_format_iso8601(now);
    g_autoptr(GString) report=g_string_new("# Hex game\n\n");int result=game_result(&app->game);
    g_string_append_printf(report,"%s; seed: %u; Red: %s; Blue: %s; size: %u; swap enabled: %s; swapped: %s; winner: %s; stones: %u-%u; placements: %u; actions: %u.\n\n",time,app->seed,player_name(app->kinds[0]),player_name(app->kinds[1]),app->game.size,app->game.swap_enabled?"yes":"no",app->game.swapped?"yes":"no",color(result),game_count(app->game.red),game_count(app->game.blue),app->game.moves,app->game.actions);
    for(unsigned i=0;i<2;i++)g_string_append_printf(report,"%s configuration: depth %u; MCTS iterations %u; time %.3f s; memory %zu bytes; thinking %s.\n",color(i?-1:1),app->options[i].depth,app->options[i].iterations,app->options[i].seconds,app->options[i].memory_bytes,app->ollama_options[i].thinking?"on":"off");
    g_string_append_printf(report,"\n%s\n---\n\n",app->history->str);GError *error=NULL;
    if(!game_append_report(GAME_HISTORY_FILE,report->str,&error)){app->save_failed=TRUE;set_error(app,error->message);g_clear_error(&error);}
    else{app->saved=TRUE;app->save_failed=FALSE;set_error(app,NULL);}
}
static gboolean apply(App *app,unsigned move)
{
    if(app->actions>=GAME_MAX_ACTIONS)return FALSE;
    Game before=app->game;
    if(!game_play(&app->game,move))return FALSE;
    app->snapshots[app->actions]=before;app->history_lengths[app->actions++]=app->history->len;
    g_string_append_printf(app->history,"## Action %u — %s: %s\n\n%sAnimation duration: %.3f ms.\n\n",app->actions,color(before.turn),game_labels[move],app->pending_details?app->pending_details:"",app->animation.duration/1000.);
    if(move==GAME_SWAP)g_string_append(app->history,"Pie rule: reflect opening along a1–g7 and recolor it Blue; Red plays next.\n\n");
    save_attempt(app);return TRUE;
}
static void commit(App *app)
{
    int move=app->pending;app->animation.active=FALSE;app->pending=-1;app->ready=FALSE;
    if(move<0 || !apply(app,move)){app->failed=TRUE;set_error(app,_("The prepared move is no longer legal."));}
    g_clear_pointer(&app->pending_details,g_free);update(app);schedule(app);
}
static gboolean tick(GtkWidget *widget,GdkFrameClock *clock,gpointer data)
{
    (void)widget;App *app=data;AnimationFrame frame=animation_frame(&app->animation,gdk_frame_clock_get_frame_time(clock));
    gtk_widget_queue_draw(app->board);
    if(!app->paused && frame.finished){app->tick=0;commit(app);return G_SOURCE_REMOVE;}return G_SOURCE_CONTINUE;
}
static void start_animation(App *app,int square)
{
    if(!app->playing || app->closing || app->animation.active || square<0 || square>=64 || !(game_legal_moves(&app->game)&SQUARE_BIT(square)))return;
    app->pending=square;app->ready=FALSE;
    animation_start(&app->animation,g_get_monotonic_time(),gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->delay)));
    if(!app->animation.duration){commit(app);return;}
    app->tick=gtk_widget_add_tick_callback(app->board,tick,app,NULL);update(app);
}
static gboolean cancelled(gpointer data){Job *job=data;return job->cancel && g_cancellable_is_cancelled(job->cancel);}
static void progress(unsigned completed,guint64 nodes,gpointer data)
{
    Job *job=data;g_mutex_lock(&job->mutex);job->progress=completed;job->nodes=nodes;g_mutex_unlock(&job->mutex);
}
static void job_free(gpointer data)
{
    Job *job=data;g_mutex_clear(&job->mutex);g_free(job->model);g_free(job->url);g_free(job->details);g_strfreev(job->models);g_free(job);
}
static void worker(GTask *task,gpointer source,gpointer task_data,GCancellable *cancel)
{
    (void)source;Job *job=task_data;GError *error=NULL;
    if(job->list){job->models=ollama_list_models(job->url,OLLAMA_LIST_TIMEOUT_MS,cancel,&error);if(error)g_task_return_error(task,error);else g_task_return_boolean(task,TRUE);return;}
    g_autoptr(GRand) random=g_rand_new_with_seed(job->seed);g_autoptr(GString) details=g_string_new(NULL);
    int values[64];for(unsigned p=0;p<64;p++)values[p]=G_MININT;
    if(job->kind==PLAYER_RANDOM)job->chosen=game_random_move(game_legal_moves(&job->game),random);
    else if(job->kind==PLAYER_POSITIONAL)job->chosen=game_positional_move(&job->game,random,values);
    else if(job->kind==PLAYER_OLLAMA) {
        if(!ollama_choose_with_options(&job->game,job->model,job->url,OLLAMA_MOVE_TIMEOUT_MS,cancel,&job->ollama_options,&job->ollama,&error)){g_task_return_error(task,error);return;}
        job->chosen=job->ollama.move;ollama_record(details,job->model,&job->ollama);
    } else {
        SearchControl control={cancelled,progress,job};
        gboolean ok=job->kind==PLAYER_MINIMAX?minimax_evaluate(&job->game,&job->options,&control,&job->search):mcts_evaluate(&job->game,&job->options,&control,random,&job->search);
        if(!ok){g_task_return_new_error(task,G_IO_ERROR,job->search.status==SEARCH_CANCELLED?G_IO_ERROR_CANCELLED:G_IO_ERROR_FAILED,"%s",_("Search stopped before completing a decision. Adjust limits and Retry."));return;}
        job->chosen=search_choose(&job->search,random);memcpy(values,job->search.values,sizeof values);
        g_string_append_printf(details,"Algorithm: %s; completed: %u/%u; status: %s; nodes: %" G_GUINT64_FORMAT "; tree nodes: %u; elapsed: %.3f ms; memory: %zu bytes.\n\n",player_name(job->kind),job->search.completed,job->search.requested,search_status_name(job->search.status),job->search.nodes,job->search.tree_nodes,job->search.elapsed_us/1000.,job->search.peak_memory);
    }
    g_string_append(details,"| Move | Own distance | Opponent distance | Heuristic | Search value | Visits |\n|---|---:|---:|---:|---:|---:|\n");
    for(unsigned p=0;p<64;p++)if(game_legal_moves(&job->game)&SQUARE_BIT(p)){
        Game next=job->game;game_play(&next,p);PositionValue value;game_position(&next,job->game.turn,&value);
        g_string_append_printf(details,"| %s | %d | %d | %d | ",game_labels[p],value.own_distance,value.other_distance,value.total);
        if(values[p]!=G_MININT)g_string_append_printf(details,"%d",values[p]);else g_string_append(details,"—");
        g_string_append_printf(details," | %u |\n",job->search.visits[p]);
    }
    g_string_append_printf(details,"\nDecision seed: %u.\n\n",job->seed);job->details=g_string_free(g_steal_pointer(&details),FALSE);
    if(g_cancellable_is_cancelled(cancel))g_task_return_new_error(task,G_IO_ERROR,G_IO_ERROR_CANCELLED,"Cancelled");else g_task_return_boolean(task,TRUE);
}
static void done(GObject *source,GAsyncResult *result,gpointer data)
{
    (void)source;App *app=data;Job *job=g_task_get_task_data(G_TASK(result));GError *error=NULL;
    gboolean ok=g_task_propagate_boolean(G_TASK(result),&error);
    if(app->progress_source){g_source_remove(app->progress_source);app->progress_source=0;}
    g_clear_object(&app->task);g_clear_object(&app->cancel);
    if(!app->closing && job->generation==app->generation) {
        if(job->list && ok) {
            for(unsigned i=0;i<2;i++){
                GtkStringList *list=gtk_string_list_new((const char *const *)job->models);gtk_drop_down_set_model(GTK_DROP_DOWN(app->models[i]),G_LIST_MODEL(list));g_object_unref(list);
            }
            if(!job->models[0])set_error(app,_("No Ollama models found."));
        } else if(ok && app->playing && job->game.red==app->game.red && job->game.blue==app->game.blue && job->game.turn==app->game.turn && job->chosen>=0 && job->chosen<64 && (game_legal_moves(&app->game)&SQUARE_BIT(job->chosen))) {
            app->last_search=job->search;
            app->pending=job->chosen;app->ready=TRUE;app->pending_details=g_steal_pointer(&job->details);
        } else if(error && !g_error_matches(error,G_IO_ERROR,G_IO_ERROR_CANCELLED)) {
            if(!job->list)app->failed=TRUE;
            set_error(app,error->message);
        } else if(ok && !job->list && app->playing) {
            app->failed=TRUE;set_error(app,_("The prepared move is no longer legal."));
        }
    }
    g_clear_error(&error);if(!app->closing){update(app);schedule(app);}
}
static gboolean progress_tick(gpointer data){update(data);return G_SOURCE_CONTINUE;}
static void launch(App *app,gboolean list)
{
    if(app->task || app->closing)return;
    unsigned i=side(app->game.turn);
    Job *job=g_new0(Job,1);g_mutex_init(&job->mutex);job->game=app->game;job->generation=app->generation;job->list=list;job->chosen=-1;
    job->kind=app->kinds[i];job->options=app->options[i];job->ollama_options=app->ollama_options[i];job->seed=g_rand_int(app->random);
    job->model=g_strdup(app->selected_models[i]);job->url=g_strdup(app->url);app->cancel=g_cancellable_new();
    if(!list && job->kind==PLAYER_OLLAMA)ollama_view_bind(GTK_WINDOW(app->window),app->cancel,_(color(app->game.turn)),app->actions+1,job->model);
    job->cancel=app->cancel;
    app->task=g_task_new(NULL,app->cancel,done,app);g_task_set_task_data(app->task,job,job_free);g_task_set_return_on_cancel(app->task,FALSE);
    app->progress_source=g_timeout_add(100,progress_tick,app);g_task_run_in_thread(app->task,worker);update(app);
}
static void schedule(App *app)
{
    if(!app->playing || app->closing || app->paused || app->failed || app->animation.active)return;
    if(game_result(&app->game)){save_attempt(app);update(app);return;}
    if(app->kinds[side(app->game.turn)]==PLAYER_HUMAN)return;
    if(app->ready)start_animation(app,app->pending);else if(!app->task)launch(app,FALSE);
}
static SearchOptions read_options(App *app,unsigned i)
{
    return (SearchOptions){gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->depth[i])),gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->iterations[i])),gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->seconds)),(gsize)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->memory))*1024*1024,0};
}
static void begin_clicked(GtkButton *widget,gpointer data)
{
    (void)widget;App *app=data;app->playing=FALSE;clear_attempt(app);
    for(unsigned i=0;i<2;i++) {
        app->kinds[i]=gtk_drop_down_get_selected(GTK_DROP_DOWN(app->controllers[i]));app->options[i]=read_options(app,i);
        app->ollama_options[i].thinking=gtk_check_button_get_active(GTK_CHECK_BUTTON(app->thinking[i]));
    app->ollama_options[i].harness=ui_ollama_harness_active(app->thinking[i]);
        GtkStringObject *model=gtk_drop_down_get_selected_item(GTK_DROP_DOWN(app->models[i]));g_free(app->selected_models[i]);
        app->selected_models[i]=g_strdup(model?gtk_string_object_get_string(model):"");
        if(app->kinds[i]==PLAYER_OLLAMA && !*app->selected_models[i]){set_error(app,_("Select an Ollama model first."));update(app);return;}
    }
    app->playing=TRUE;ollama_view_begin(GTK_WINDOW(app->window));update(app);schedule(app);
}
static void restart_clicked(GtkButton *widget,gpointer data){(void)widget;App *app=data;clear_attempt(app);app->playing=TRUE;ollama_view_begin(GTK_WINDOW(app->window));update(app);schedule(app);}
static void configure_clicked(GtkButton *widget,gpointer data){
    ollama_view_clear(GTK_WINDOW(((App *)data)->window));(void)widget;App *app=data;app->playing=FALSE;clear_attempt(app);update(app);}
static void pause_clicked(GtkButton *widget,gpointer data)
{
    (void)widget;App *app=data;app->paused=!app->paused;
    if(app->paused)animation_pause(&app->animation,g_get_monotonic_time());else{animation_resume(&app->animation,g_get_monotonic_time());schedule(app);}update(app);
}
static void undo_clicked(GtkButton *widget,gpointer data)
{
    (void)widget;App *app=data;
    if(!app->actions || app->saved || app->animation.active || (app->kinds[0]!=PLAYER_HUMAN && app->kinds[1]!=PLAYER_HUMAN))return;
    cancel_work(app);app->game=app->snapshots[--app->actions];g_string_truncate(app->history,app->history_lengths[app->actions]);
    app->paused=TRUE;app->failed=FALSE;app->last_search=(SearchResult){0};set_error(app,NULL);ollama_view_undo(GTK_WINDOW(app->window));update(app);
}
static void retry_clicked(GtkButton *widget,gpointer data)
{
    (void)widget;App *app=data;
    if(app->save_failed){save_attempt(app);update(app);return;}
    cancel_work(app);app->failed=FALSE;app->paused=FALSE;set_error(app,NULL);unsigned i=side(app->game.turn);app->options[i]=read_options(app,i);update(app);schedule(app);
}
static void refresh_clicked(GtkButton *widget,gpointer data){(void)widget;App *app=data;if(!app->playing && !app->task){set_error(app,NULL);launch(app,TRUE);}}
static void update(App *app)
{
    if(!app->window)return;
    gboolean config=!app->playing,need_model=FALSE,planning=FALSE;
    for(unsigned i=0;i<2;i++) {
        PlayerKind kind=config?gtk_drop_down_get_selected(GTK_DROP_DOWN(app->controllers[i])):app->kinds[i];
        gboolean model=kind==PLAYER_OLLAMA,minimax=kind==PLAYER_MINIMAX,mcts=kind==PLAYER_MCTS;
        need_model|=model;planning|=minimax || mcts;
        gtk_widget_set_sensitive(app->controllers[i],config);gtk_widget_set_visible(app->models[i],model);gtk_widget_set_visible(app->model_label[i],model);
        gtk_widget_set_visible(app->thinking[i],model);gtk_widget_set_sensitive(app->models[i],config);gtk_widget_set_sensitive(app->thinking[i],config);
        gtk_widget_set_visible(app->depth[i],minimax);gtk_widget_set_visible(app->depth_label[i],minimax);
        gtk_widget_set_visible(app->iterations[i],mcts);gtk_widget_set_visible(app->iteration_label[i],mcts);
        gtk_widget_set_sensitive(app->depth[i],config || app->failed);gtk_widget_set_sensitive(app->iterations[i],config || app->failed);
    }
    gtk_widget_set_sensitive(app->size_choice,config);gtk_widget_set_sensitive(app->swap_rule,config);
    gtk_widget_set_visible(app->swap_move,!config && app->kinds[side(app->game.turn)]==PLAYER_HUMAN && (game_legal_moves(&app->game)&SQUARE_BIT(GAME_SWAP)));
    gtk_widget_set_sensitive(app->swap_move,!app->paused && !app->failed && !app->animation.active);
    gtk_widget_set_visible(app->refresh,need_model);gtk_widget_set_sensitive(app->refresh,config && !app->task);
    gtk_widget_set_visible(app->begin,config);gtk_widget_set_visible(app->restart,!config);gtk_widget_set_visible(app->configure,!config);
    gtk_widget_set_visible(app->pause,!config);gtk_widget_set_sensitive(app->pause,!game_result(&app->game));
    gtk_button_set_label(GTK_BUTTON(app->pause),_(app->paused?"Resume":"Pause"));
    gtk_widget_set_visible(app->undo,!config && (app->kinds[0]==PLAYER_HUMAN || app->kinds[1]==PLAYER_HUMAN));
    gtk_widget_set_sensitive(app->undo,app->actions>0 && !app->saved && !app->animation.active);
    gtk_widget_set_visible(app->retry,app->failed || app->save_failed);
    gtk_widget_set_sensitive(app->seconds,config || app->failed);gtk_widget_set_sensitive(app->memory,config || app->failed);
    gtk_widget_set_visible(app->seconds_label,planning);gtk_widget_set_visible(app->seconds,planning);
    gtk_widget_set_visible(app->memory_label,planning);gtk_widget_set_visible(app->memory,planning);
    GString *status=g_string_new(NULL);
    g_string_append_printf(status,_("Red %u · Blue %u\n"),game_count(app->game.red),game_count(app->game.blue));
    int outcome=game_result(&app->game);
    if(config)g_string_append(status,_("Choose both players and begin."));
    else if(outcome) {
        g_string_append_printf(status,_("%s wins!"),_(color(outcome)));
        g_string_append(status,_("\nGame over. Restart or choose a new configuration."));
    } else if(app->failed)g_string_append(status,_("Decision stopped. Adjust limits and Retry, or restart."));
    else if(app->paused)g_string_append(status,_("Paused"));
    else if(app->animation.active)g_string_append(status,_("Placing stone…"));
    else g_string_append_printf(status,_("%s to move · %u legal squares"),_(color(app->game.turn)),game_count(game_legal_moves(&app->game)));
    if(app->task) {
        Job *job=g_task_get_task_data(app->task);
        if(!job->list && job->generation==app->generation) {
            g_mutex_lock(&job->mutex);g_string_append_printf(status,_("\nCompleted %u · nodes %" G_GUINT64_FORMAT),job->progress,job->nodes);g_mutex_unlock(&job->mutex);
        }
    }
    if(!config && !app->task && app->last_search.requested) {
        g_string_append_printf(status,_("\nLast analysis: %u/%u"),app->last_search.completed,app->last_search.requested);
        if(app->last_search.status==SEARCH_LIMIT)g_string_append(status,_(" · resource limit"));
    }
    gtk_label_set_text(GTK_LABEL(app->status),status->str);g_string_free(status,TRUE);gtk_widget_queue_draw(app->board);
}
static void board_transform(App *app,double *scale,double *x,double *y)
{
    double w=gtk_widget_get_width(app->board),h=gtk_widget_get_height(app->board);*scale=MIN(w,h)/592.;*x=(w-*scale*592)/2;*y=(h-*scale*592)/2;
}
/* Pointy hexes: neighboring rows are shifted half a column to the right. */
static double radius(const App *app){return 500./(sqrt(3.)*(1.5*app->game.size+.5));}
static void center(const App *app,unsigned p,double *x,double *y)
{
    double r=radius(app),span=1.5*r*(app->game.size-1)+2*r;
    *x=46+sqrt(3.)*r*(p%7+(p/7)*.5+.5);*y=(592-span)/2+r+1.5*r*(p/7);
}
static void hexagon(cairo_t *cr,double x,double y,double r)
{
    cairo_new_path(cr);for(unsigned i=0;i<6;i++){double angle=G_PI/180*(60.0*i-30);double px=x+r*cos(angle),py=y+r*sin(angle);if(i)cairo_line_to(cr,px,py);else cairo_move_to(cr,px,py);}cairo_close_path(cr);
}
static void stone(cairo_t *cr,double x,double y,double r,int owner,double opacity)
{
    cairo_new_sub_path(cr);cairo_arc(cr,x,y,r,0,2*G_PI);
    if(owner==1)cairo_set_source_rgba(cr,.9,.17,.22,opacity);else cairo_set_source_rgba(cr,.16,.48,.97,opacity);
    cairo_fill_preserve(cr);cairo_set_source_rgba(cr,1,1,1,opacity*.65);cairo_set_line_width(cr,1.5);cairo_stroke(cr);
}
static void draw(GtkDrawingArea *area,cairo_t *cr,int width,int height,gpointer data)
{
    (void)area;(void)width;(void)height;App *app=data;double scale,x,y;board_transform(app,&scale,&x,&y);
    cairo_set_source_rgb(cr,.06,.085,.12);cairo_paint(cr);cairo_translate(cr,x,y);cairo_scale(cr,scale,scale);
    double rad=radius(app);int result=game_result(&app->game);guint64 path=result?game_winning_path(&app->game,result):0;
    AnimationFrame frame=animation_frame(&app->animation,g_get_monotonic_time());
    for(unsigned row=0;row<app->game.size;row++)for(unsigned col=0;col<app->game.size;col++){
        unsigned p=row*7+col;double px,py;center(app,p,&px,&py);hexagon(cr,px,py,rad*.96);
        cairo_set_source_rgb(cr,.23,.28,.33);cairo_fill_preserve(cr);cairo_set_source_rgb(cr,.57,.65,.7);cairo_set_line_width(cr,1);cairo_stroke(cr);
        /* Colored boundary segments mark the four goals; corners touch both. */
        for(unsigned edge=0;edge<6;edge++){
            int owner=((row==0 && (edge==4 || edge==5)) || (row==app->game.size-1 && (edge==1 || edge==2)))?1:
                ((col==0 && edge==3) || (col==app->game.size-1 && edge==0))?-1:0;
            if(!owner)continue;
            double a=G_PI/180*(60.0*edge-30),b=a+G_PI/3;
            cairo_move_to(cr,px+rad*cos(a),py+rad*sin(a));cairo_line_to(cr,px+rad*cos(b),py+rad*sin(b));
            if(owner==1)cairo_set_source_rgb(cr,1,.25,.28);else cairo_set_source_rgb(cr,.25,.55,1);cairo_set_line_width(cr,5);cairo_stroke(cr);
        }
        int owner=app->game.red&SQUARE_BIT(p)?1:app->game.blue&SQUARE_BIT(p)?-1:0;
        if(owner && !(app->animation.active && app->pending==GAME_SWAP))stone(cr,px,py,rad*.65,owner,1);
        if(path&SQUARE_BIT(p)){cairo_arc(cr,px,py,rad*.77,0,2*G_PI);cairo_set_source_rgb(cr,1,.83,.25);cairo_set_line_width(cr,3);cairo_stroke(cr);}
        if((int)p==app->cursor && app->playing && app->kinds[side(app->game.turn)]==PLAYER_HUMAN){hexagon(cr,px,py,rad*.84);cairo_set_source_rgb(cr,1,.83,.25);cairo_set_line_width(cr,2);cairo_stroke(cr);}
        cairo_set_font_size(cr,MIN(14,rad*.4));cairo_set_source_rgba(cr,1,1,1,owner?.8:.6);cairo_move_to(cr,px-rad*.28,py+rad*.13);cairo_show_text(cr,game_labels[p]);
    }
    if(app->animation.active && app->pending>=0){
        if(app->pending==GAME_SWAP){unsigned from=0;while(!(app->game.red&SQUARE_BIT(from)))from++;unsigned to=(from%7)*7+from/7;double ax,ay,bx,by;center(app,from,&ax,&ay);center(app,to,&bx,&by);stone(cr,ax+(bx-ax)*frame.position,ay+(by-ay)*frame.position,rad*.65,frame.position>0?-1:1,frame.opacity);}
        else{double px,py;center(app,app->pending,&px,&py);stone(cr,px,py,rad*.65,app->game.turn,frame.opacity);}
    }
}
static void play_human(App *app,int square)
{
    if(!app->playing || app->paused || app->failed || app->animation.active || game_result(&app->game) || app->kinds[side(app->game.turn)]!=PLAYER_HUMAN || square<0 || square>=64 || !(game_legal_moves(&app->game)&SQUARE_BIT(square)))return;
    if(square!=GAME_SWAP)app->cursor=square;
    app->pending_details=g_strdup("Human decision.\n\n");start_animation(app,square);
}
static void click(GtkGestureClick *gesture,int count,double px,double py,gpointer data)
{
    (void)gesture;(void)count;App *app=data;double scale,x,y;board_transform(app,&scale,&x,&y);
    gtk_widget_grab_focus(app->board);if(scale<=0)return;
    double hx=(px-x)/scale,hy=(py-y)/scale,rad=radius(app);
    for(unsigned r=0;r<app->game.size;r++)for(unsigned c=0;c<app->game.size;c++){
        double cx,cy;center(app,r*7+c,&cx,&cy);double dx=fabs(hx-cx),dy=fabs(hy-cy);
        if(dx<=sqrt(3.)*rad/2 && dy<=rad && sqrt(3.)*dy+dx<=sqrt(3.)*rad){play_human(app,r*7+c);return;}
    }
}
static void swap_clicked(GtkButton *widget,gpointer data){(void)widget;play_human(data,GAME_SWAP);}
static gboolean key_pressed(GtkEventControllerKey *controller,guint key,guint code,GdkModifierType state,gpointer data)
{
    (void)controller;(void)code;(void)state;App *app=data;
    if(!app->playing || app->kinds[side(app->game.turn)]!=PLAYER_HUMAN)return FALSE;
    int r=app->cursor/7,c=app->cursor%7;
    if(key==GDK_KEY_Left)c=MAX(0,c-1);else if(key==GDK_KEY_Right)c=MIN((int)app->game.size-1,c+1);else if(key==GDK_KEY_Up)r=MAX(0,r-1);else if(key==GDK_KEY_Down)r=MIN((int)app->game.size-1,r+1);
    else if(key==GDK_KEY_Return || key==GDK_KEY_space){play_human(app,app->cursor);return TRUE;}else return FALSE;
    app->cursor=r*7+c;gtk_widget_queue_draw(app->board);return TRUE;
}
static void configuration_changed(GObject *object,GParamSpec *spec,gpointer data){(void)object;(void)spec;update(data);}
static void board_size_changed(GObject *object,GParamSpec *spec,gpointer data)
{
    (void)object;(void)spec;App *app=data;if(app->playing)return;
    game_init_variant(&app->game,3+2*gtk_drop_down_get_selected(GTK_DROP_DOWN(app->size_choice)),gtk_check_button_get_active(GTK_CHECK_BUTTON(app->swap_rule)));app->cursor=0;update(app);
}
static void language_changed(GObject *object,GParamSpec *spec,gpointer data)
{
    (void)spec;App *app=data;i18n_set_language(gtk_drop_down_get_selected(GTK_DROP_DOWN(object)));
    for(unsigned i=0;i<2;i++) {
        guint selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(app->controllers[i]));const char *names[PLAYER_COUNT+1];
        for(unsigned k=0;k<PLAYER_COUNT;k++)names[k]=_(player_name(k));
        names[PLAYER_COUNT]=NULL;
        GtkStringList *model=gtk_string_list_new(names);gtk_drop_down_set_model(GTK_DROP_DOWN(app->controllers[i]),G_LIST_MODEL(model));g_object_unref(model);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[i]),selected);
    }
    retranslate(app->window);gtk_window_set_title(GTK_WINDOW(app->window),_(GAME_NAME));update(app);GError *error=NULL;
    g_autofree char *directory=g_path_get_dirname(app->preferences);
    if(g_mkdir_with_parents(directory,0700)<0 || !i18n_save(app->preferences,&error)){set_error(app,error?error->message:_("Could not save language preference."));g_clear_error(&error);}

    ollama_view_retranslate(GTK_WINDOW(app->window));
}
static gboolean close_window(GtkWindow *window,gpointer data){(void)window;App *app=data;app->closing=TRUE;cancel_work(app);app->window=NULL;return FALSE;}
static GtkWidget *spin(GtkWidget *parent,double min,double max,double step,double value)
{
    GtkWidget *widget=gtk_spin_button_new_with_range(min,max,step);gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget),value);gtk_box_append(GTK_BOX(parent),widget);return widget;
}
static void activate(GtkApplication *application,gpointer data)
{
    App *app=data;if(app->window){gtk_window_present(GTK_WINDOW(app->window));return;}
    app->window=adw_application_window_new(application);gtk_window_set_title(GTK_WINDOW(app->window),_(GAME_NAME));gtk_window_set_default_size(GTK_WINDOW(app->window),1000,800);
    GtkWidget *root=gtk_box_new(GTK_ORIENTATION_VERTICAL,0),*header=adw_header_bar_new();gtk_box_append(GTK_BOX(root),header);
    const char *languages[]={"System","English","Português","Español","Italiano",NULL};app->language=gtk_drop_down_new_from_strings(languages);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language),i18n_selection());adw_header_bar_pack_end(ADW_HEADER_BAR(header),app->language);
    GtkWidget *body=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,16);gtk_widget_set_margin_start(body,16);gtk_widget_set_margin_end(body,16);gtk_widget_set_margin_bottom(body,16);gtk_widget_set_vexpand(body,TRUE);gtk_box_append(GTK_BOX(root),body);
    GtkWidget *sidebar=gtk_box_new(GTK_ORIENTATION_VERTICAL,8),*scroll=gtk_scrolled_window_new();gtk_widget_set_size_request(scroll,280,-1);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),sidebar);gtk_box_append(GTK_BOX(body),scroll);
    gtk_box_append(GTK_BOX(sidebar),label("Board size"));const char *sizes[]={"3 × 3","5 × 5","7 × 7",NULL};
    app->size_choice=gtk_drop_down_new_from_strings(sizes);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->size_choice),1);gtk_box_append(GTK_BOX(sidebar),app->size_choice);
    app->swap_rule=gtk_check_button_new_with_label(_("Allow opening swap (pie rule)"));g_object_set_data_full(G_OBJECT(app->swap_rule),"message",g_strdup("Allow opening swap (pie rule)"),g_free);gtk_check_button_set_active(GTK_CHECK_BUTTON(app->swap_rule),TRUE);gtk_box_append(GTK_BOX(sidebar),app->swap_rule);
    for(unsigned i=0;i<2;i++) {
        gtk_box_append(GTK_BOX(sidebar),label(color(i?-1:1)));const char *names[PLAYER_COUNT+1];for(unsigned k=0;k<PLAYER_COUNT;k++)names[k]=_(player_name(k));
        names[PLAYER_COUNT]=NULL;
        app->controllers[i]=gtk_drop_down_new_from_strings(names);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[i]),i?PLAYER_POSITIONAL:PLAYER_HUMAN);gtk_box_append(GTK_BOX(sidebar),app->controllers[i]);
        app->depth_label[i]=label("Search depth");gtk_box_append(GTK_BOX(sidebar),app->depth_label[i]);app->depth[i]=spin(sidebar,1,GAME_MAX_DEPTH,1,3);
        app->iteration_label[i]=label("MCTS iterations");gtk_box_append(GTK_BOX(sidebar),app->iteration_label[i]);app->iterations[i]=spin(sidebar,1,1000000,100,1000);
        app->model_label[i]=label("Ollama model");gtk_box_append(GTK_BOX(sidebar),app->model_label[i]);const char *empty[]={NULL};app->models[i]=gtk_drop_down_new_from_strings(empty);gtk_box_append(GTK_BOX(sidebar),app->models[i]);
        app->thinking[i]=gtk_check_button_new_with_label(_("Extended thinking"));g_object_set_data_full(G_OBJECT(app->thinking[i]),"message",g_strdup("Extended thinking"),g_free);gtk_box_append(GTK_BOX(sidebar),app->thinking[i]);
    ui_ollama_harness_add(app->thinking[i],NULL,_("Reasoning harness"));
        g_signal_connect(app->controllers[i],"notify::selected",G_CALLBACK(configuration_changed),app);
    }
    app->refresh=button("Refresh models",G_CALLBACK(refresh_clicked),app);gtk_box_append(GTK_BOX(sidebar),app->refresh);
    app->seconds_label=label("Planning seconds (0: unlimited)");gtk_box_append(GTK_BOX(sidebar),app->seconds_label);app->seconds=spin(sidebar,0,18000,1,5);
    app->memory_label=label("Solver memory limit (MiB)");gtk_box_append(GTK_BOX(sidebar),app->memory_label);app->memory=spin(sidebar,1,4096,16,64);
    gtk_box_append(GTK_BOX(sidebar),label("Animation delay (seconds)"));app->delay=spin(sidebar,0,3,.1,.5);gtk_spin_button_set_digits(GTK_SPIN_BUTTON(app->delay),1);
    app->begin=button("Begin",G_CALLBACK(begin_clicked),app);gtk_widget_add_css_class(app->begin,"suggested-action");gtk_box_append(GTK_BOX(sidebar),app->begin);
    app->pause=button("Pause",G_CALLBACK(pause_clicked),app);gtk_box_append(GTK_BOX(sidebar),app->pause);
    app->swap_move=button("Swap opening",G_CALLBACK(swap_clicked),app);gtk_box_append(GTK_BOX(sidebar),app->swap_move);
    app->undo=button("Undo",G_CALLBACK(undo_clicked),app);gtk_box_append(GTK_BOX(sidebar),app->undo);
    app->retry=button("Retry",G_CALLBACK(retry_clicked),app);gtk_box_append(GTK_BOX(sidebar),app->retry);
    app->restart=button("Restart",G_CALLBACK(restart_clicked),app);gtk_box_append(GTK_BOX(sidebar),app->restart);
    app->configure=button("New configuration",G_CALLBACK(configure_clicked),app);gtk_box_append(GTK_BOX(sidebar),app->configure);
    app->status=gtk_label_new(NULL);gtk_label_set_wrap(GTK_LABEL(app->status),TRUE);gtk_label_set_xalign(GTK_LABEL(app->status),0);
    app->error_label=gtk_label_new(NULL);gtk_label_set_wrap(GTK_LABEL(app->error_label),TRUE);gtk_widget_add_css_class(app->error_label,"error");gtk_widget_set_visible(app->error_label,FALSE);
    GtkWidget *right=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);gtk_widget_set_hexpand(right,TRUE);gtk_box_append(GTK_BOX(body),right);
    gtk_box_append(GTK_BOX(right),ui_message_panel(app->status,app->error_label));

    app->board=gtk_drawing_area_new();gtk_widget_set_size_request(app->board,540,540);gtk_widget_set_hexpand(app->board,TRUE);gtk_widget_set_vexpand(app->board,TRUE);gtk_widget_set_focusable(app->board,TRUE);gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(app->board),draw,app,NULL);gtk_box_append(GTK_BOX(right),app->board);
    GtkWidget *rules=label("Red connects top to bottom; Blue connects left to right. Place a stone on an empty hex. Red starts. Blue may swap the opening: reflect and recolor the first stone. No draws. Arrow keys and Enter also play.");gtk_label_set_wrap(GTK_LABEL(rules),TRUE);gtk_label_set_selectable(GTK_LABEL(rules),TRUE);gtk_box_append(GTK_BOX(right),ui_fixed_text_panel(rules,80));
    GtkGesture *gesture=gtk_gesture_click_new();g_signal_connect(gesture,"pressed",G_CALLBACK(click),app);gtk_widget_add_controller(app->board,GTK_EVENT_CONTROLLER(gesture));
    GtkEventController *keys=gtk_event_controller_key_new();g_signal_connect(keys,"key-pressed",G_CALLBACK(key_pressed),app);gtk_widget_add_controller(app->board,keys);
    g_signal_connect(app->size_choice,"notify::selected",G_CALLBACK(board_size_changed),app);
    g_signal_connect(app->language,"notify::selected",G_CALLBACK(language_changed),app);g_signal_connect(app->window,"close-request",G_CALLBACK(close_window),app);
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(app->window),root);update(app);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header),ollama_view_install(GTK_WINDOW(app->window),app->controllers[0],app->controllers[1],PLAYER_OLLAMA,i18n_translate));
    gtk_window_present(GTK_WINDOW(app->window));
}
int main(int argc,char **argv)
{
    App app={.pending=-1,.cursor=0};app.random=g_rand_new();app.history=g_string_new(NULL);game_init(&app.game);
    app.url=g_strdup(g_getenv("OLLAMA_URL"));app.preferences=g_build_filename(g_get_user_config_dir(),"ai-games","hex.ini",NULL);GError *error=NULL;
    if(!i18n_load(app.preferences,&error)){g_printerr("%s\n",error->message);g_clear_error(&error);}
    app.application=GTK_APPLICATION(adw_application_new("io.github.aigames.Hex",G_APPLICATION_DEFAULT_FLAGS));g_signal_connect(app.application,"activate",G_CALLBACK(activate),&app);
    int status=g_application_run(G_APPLICATION(app.application),argc,argv);app.closing=TRUE;cancel_work(&app);while(app.task)g_main_context_iteration(NULL,TRUE);
    g_object_unref(app.application);g_rand_free(app.random);g_string_free(app.history,TRUE);g_free(app.url);g_free(app.preferences);g_free(app.error);g_free(app.pending_details);for(unsigned i=0;i<2;i++)g_free(app.selected_models[i]);return status;
}
