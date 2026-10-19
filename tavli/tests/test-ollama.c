// SPDX-License-Identifier: GPL-3.0-or-later
#include "ollama.h"
#include "ollama-server.h"
typedef struct { Fixture *fixture;Game game;OllamaOptions options;OllamaResult result;GCancellable *cancel;const char *model;guint timeout;gint finished;gboolean ok;GError *error; } Call;
static gpointer call_worker(gpointer data)
{
    Call *c=data;c->ok=ollama_choose_with_options(&c->game,c->model?c->model:"mock:1b",c->fixture->url,c->timeout?c->timeout:2000,c->cancel,&c->options,&c->result,&c->error);g_atomic_int_set(&c->finished,TRUE);g_main_context_wakeup(NULL);return NULL;
}
static void run(Call *c){c->finished=FALSE;GThread *thread=g_thread_new("ollama-test",call_worker,c);while(!g_atomic_int_get(&c->finished))g_main_context_iteration(NULL,TRUE);g_thread_join(thread);}
static void protocol(Fixture *f,gconstpointer data)
{
    (void)data;if(!f->url)return;f->auto_move=TRUE;Call c={.fixture=f};game_init(&c.game,RUNNING,SESSION_SINGLE,5);game_opening(&c.game,6,1);game_roll(&c.game,2,2);
    run(&c);g_assert_true(c.ok);g_assert_no_error(c.error);g_assert_cmpuint(c.result.decision.plan.length,==,2);Game verify=c.game;g_assert_true(decision_apply(&verify,&c.result.decision));g_assert_true(game_valid(&verify));
    const char *prompt=json_object_get_string_member(f->request,"prompt");g_assert_nonnull(strstr(prompt,"No search assistance"));g_assert_nonnull(strstr(prompt,"Doubles give TWO uses"));g_assert_cmpuint(f->generate_calls,==,2);
    game_init(&c.game,BACKGAMMON,SESSION_FIXED,5);c.game.phase=PRE_ROLL;run(&c);g_assert_true(c.ok);g_assert_false(c.result.decision.checker);g_assert_cmpint(c.result.decision.action,==,ACTION_ROLL);
    game_control(&c.game,ACTION_DOUBLE);run(&c);g_assert_true(c.ok);g_assert_cmpint(c.result.decision.action,==,ACTION_TAKE);
    c.options.thinking=TRUE;f->info_body="{\"thinking\":{\"values\":[false,true],\"default\":false}}";run(&c);g_assert_true(c.ok);g_assert_cmpint(c.result.reasoning,==,OLLAMA_REASONING_NATIVE);g_assert_true(json_object_get_boolean_member(f->request,"think"));
    c.model="cogito:3b";unsigned before=f->generate_calls;run(&c);g_assert_true(c.ok);g_assert_cmpint(c.result.reasoning,==,OLLAMA_REASONING_COGITO);g_assert_cmpuint(f->generate_calls,==,before+2);
}
static void invalid(Fixture *f,gconstpointer data)
{
    (void)data;if(!f->url)return;Call c={.fixture=f};game_init(&c.game,BACKGAMMON,SESSION_FIXED,5);c.game.phase=PRE_ROLL;
    const char *bad[]={"broken","[]","{\"done\":false,\"response\":\"{}\"}","{\"done\":true,\"response\":\"{\\\"action_id\\\":99}\"}","{\"done\":true,\"response\":\"{\\\"action_id\\\":0,\\\"extra\\\":1}\"}","{\"done\":true,\"response\":\"{\\\"action_id\\\":0.0}\"}"};
    for(unsigned i=0;i<G_N_ELEMENTS(bad);i++){f->body=bad[i];run(&c);g_assert_false(c.ok);g_assert_nonnull(c.error);g_clear_error(&c.error);}
    f->status=404;f->body="{}";run(&c);g_assert_error(c.error,G_IO_ERROR,G_IO_ERROR_FAILED);g_clear_error(&c.error);
    f->status=200;f->delayed=TRUE;c.timeout=20;run(&c);g_assert_error(c.error,G_IO_ERROR,G_IO_ERROR_TIMED_OUT);g_clear_error(&c.error);f->delayed=FALSE;
    c.cancel=g_cancellable_new();g_cancellable_cancel(c.cancel);run(&c);g_assert_error(c.error,G_IO_ERROR,G_IO_ERROR_CANCELLED);g_clear_error(&c.error);g_object_unref(c.cancel);
}
static void parser(void)
{
    int chosen;GError *error=NULL;g_assert_true(ollama_parse_action("{\"done\":true,\"response\":\"{\\\"action_id\\\":1}\"}",2,&chosen,&error));g_assert_no_error(error);g_assert_cmpint(chosen,==,1);
    g_assert_false(ollama_parse_action("{\"done\":true,\"response\":\"{\\\"action_id\\\":-1}\"}",2,&chosen,&error));g_assert_nonnull(error);g_clear_error(&error);
}
int main(int argc,char **argv)
{
    g_setenv("OLLAMA_LOG_FILE","/dev/null",TRUE);g_setenv("OLLAMA_ERROR_LOG_FILE","/dev/null",TRUE);g_test_init(&argc,&argv,NULL);g_test_add("/ollama/turn-cube-thinking",Fixture,NULL,setup,protocol,teardown);g_test_add("/ollama/invalid-timeout-cancel",Fixture,NULL,setup,invalid,teardown);g_test_add_func("/ollama/parser",parser);return g_test_run();
}
