// SPDX-License-Identifier: GPL-3.0-or-later
#include "ollama.h"
#include "search.h"
#include "ollama-server.h"
#include <glib/gstdio.h>
typedef struct {
    Fixture *fixture;
    Game game;
    OllamaOptions options;
    OllamaResult result;
    GCancellable *cancel;
    const char *model;
    guint timeout;
    gboolean finished, ok;
    GError *error;
} Call;
static gpointer call_worker(gpointer data)
{
    Call *call = data;
    call->ok = ollama_choose_with_options(&call->game, call->model ? call->model : "test:1b", call->fixture->url,
        call->timeout ? call->timeout : 2000, call->cancel, &call->options, &call->result, &call->error);
    g_atomic_int_set((gint *) &call->finished, TRUE);
    g_main_context_wakeup(NULL);
    return NULL;
}
static void run(Call *call)
{
    call->finished = FALSE;
    GThread *thread = g_thread_new("ollama-test", call_worker, call);
    while (!g_atomic_int_get((gint *) &call->finished)) g_main_context_iteration(NULL, TRUE);
    g_thread_join(thread);
}
static void protocol(Fixture *f, gconstpointer data)
{
    (void) data; if (!f->url) return;
    f->auto_move = TRUE; Call call = {.fixture=f}; game_init(&call.game,BOARD_TRIANGLE,GOAL_ANY);
    call.options.limits = (SearchLimits) {10,4u*1024*1024,0};
    run(&call); g_assert_true(call.ok); g_assert_no_error(call.error); g_assert_true(game_legal(&call.game,call.result.move));
    const char *prompt = json_object_get_string_member(f->request,"prompt");
    g_assert_nonnull(strstr(prompt,"No search assistance")); g_assert_null(strstr(prompt,"certified winning"));
    call.options.thinking = TRUE; f->info_body = "{\"thinking\":{\"values\":[false,true],\"default\":false}}";
    run(&call); g_assert_true(call.ok); g_assert_true(json_object_get_boolean_member(f->request,"think"));
    g_assert_cmpint(call.result.reasoning,==,OLLAMA_REASONING_NATIVE);
    call.model = "cogito:3b"; unsigned generations = f->generate_calls; run(&call);
    g_assert_true(call.ok); g_assert_cmpuint(f->generate_calls,==,generations+2);
    call.options.thinking = FALSE; call.options.assisted = TRUE; call.model = "mock:1b";
    while (game_result(&call.game) == GAME_ONGOING) {
        run(&call); g_assert_true(call.ok); g_assert_no_error(call.error);
        g_assert_cmpuint(call.result.allowed.count,>,0); g_assert_true(game_play(&call.game,call.result.move));
    }
    g_assert_cmpint(game_result(&call.game),==,GAME_SOLVED);
}
static void invalid(Fixture *f, gconstpointer data)
{
    (void) data; if (!f->url) return;
    const char *bad[] = {"broken","[]","{\"done\":false,\"response\":\"{}\"}",
        "{\"done\":true,\"response\":\"{\\\"source\\\":\\\"a1\\\",\\\"destination\\\":\\\"g7\\\"}\"}"};
    Call call = {.fixture=f}; game_init(&call.game,BOARD_ENGLISH,GOAL_ANY);
    for (unsigned i=0;i<G_N_ELEMENTS(bad);i++) { f->body=bad[i]; run(&call); g_assert_false(call.ok); g_assert_nonnull(call.error); g_clear_error(&call.error); }
    f->status=404; f->body="{\"error\":\"model missing\"}"; run(&call);
    g_assert_error(call.error,G_IO_ERROR,G_IO_ERROR_FAILED); g_clear_error(&call.error);
    f->status=200; f->body="{}"; f->delayed=TRUE; call.timeout=20; run(&call);
    g_assert_error(call.error,G_IO_ERROR,G_IO_ERROR_TIMED_OUT); g_clear_error(&call.error);
    f->delayed=FALSE; call.cancel=g_cancellable_new(); g_cancellable_cancel(call.cancel); run(&call);
    g_assert_error(call.error,G_IO_ERROR,G_IO_ERROR_CANCELLED); g_clear_error(&call.error); g_object_unref(call.cancel);
}
static void parse_and_assist(void)
{
    Game game; game_init(&game,BOARD_ENGLISH,GOAL_CENTER); OllamaOptions options={.limits={10,4u*1024*1024,0}};
    OllamaAnalysis analysis; GError *error=NULL;
    g_assert_true(ollama_analyze(&game,&options,NULL,&analysis,&error)); g_assert_no_error(error);
    MoveList legal=game_legal_moves(&game); g_assert_cmpuint(analysis.allowed.count,==,legal.count);
    g_autofree char *prompt=ollama_build_prompt(&game,&analysis,FALSE); g_assert_nonnull(strstr(prompt,"center hole d4"));
    const Board *b=game_board(game.board); Move m=b->jumps[legal.items[0]];
    g_autofree char *response=g_strdup_printf("{\"done\":true,\"response\":\"{\\\"source\\\":\\\"%s\\\",\\\"destination\\\":\\\"%s\\\"}\"}",b->labels[m.source],b->labels[m.destination]);
    int move=-1; g_assert_true(ollama_parse_move(&game,response,&move,&error)); g_assert_cmpint(move,==,(int)legal.items[0]);
    game_play(&game,move); g_assert_false(ollama_parse_move(&game,response,&move,&error)); g_clear_error(&error);
    game_init(&game,BOARD_TRIANGLE,GOAL_ANY); options.assisted=TRUE;
    gboolean partial=FALSE;
    for (unsigned limit=32;limit<=1000 && !partial;limit+=16) {
        options.limits.node_limit=limit;
        gboolean ok=ollama_analyze(&game,&options,NULL,&analysis,&error);
        partial=ok && analysis.partial && analysis.allowed.count>0;
        g_clear_error(&error);
    }
    g_assert_true(partial);
    options.limits.node_limit=1; g_assert_false(ollama_analyze(&game,&options,NULL,&analysis,&error)); g_clear_error(&error);
    g_autoptr(GCancellable) cancel=g_cancellable_new(); g_cancellable_cancel(cancel);
    g_assert_false(ollama_analyze(&game,&options,cancel,&analysis,&error)); g_assert_error(error,G_IO_ERROR,G_IO_ERROR_CANCELLED); g_clear_error(&error);
    g_auto(GStrv) models=ollama_parse_models("{\"models\":[{\"name\":\"z:1b\"},{\"name\":\"a:1b\"}]}",&error);
    g_assert_no_error(error); g_assert_cmpstr(models[0],==,"a:1b");
}
int main(int argc, char **argv)
{
    g_setenv("OLLAMA_LOG_FILE", "/dev/null", TRUE); g_setenv("OLLAMA_ERROR_LOG_FILE", "/dev/null", TRUE);
    g_test_init(&argc, &argv, NULL);
    g_test_add("/ollama/protocol", Fixture, NULL, setup, protocol, teardown);
    g_test_add("/ollama/invalid-timeout-cancel", Fixture, NULL, setup, invalid, teardown);
    g_test_add_func("/ollama/parsing-assistance", parse_and_assist);
    return g_test_run();
}
