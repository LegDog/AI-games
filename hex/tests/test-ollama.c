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
typedef struct { unsigned counts[4]; } MessageCounts;
static void observed_message(OllamaMessageKind kind,const char *id,const char *text,gpointer data)
{
    (void)id;g_assert_nonnull(text);MessageCounts *counts=data;counts->counts[kind]++;
}
static void protocol(Fixture *f, gconstpointer data)
{
    (void) data; if (!f->url) return;
    f->auto_move = TRUE;
    MessageCounts counts={0};
    Call call = {.fixture = f,.cancel=g_cancellable_new()}; game_init(&call.game);
    ollama_observe(call.cancel,observed_message,&counts,NULL);
    run(&call); g_assert_true(call.ok); g_assert_no_error(call.error);
    g_assert_true((game_legal_moves(&call.game)&SQUARE_BIT(call.result.move))!=0);
    g_assert_cmpuint(counts.counts[OLLAMA_SENT],==,1);g_assert_cmpuint(counts.counts[OLLAMA_RECEIVED],==,1);
    g_assert_false(json_object_get_boolean_member(f->request,"stream"));
    const char *prompt = json_object_get_string_member(f->request, "prompt");
    g_assert_nonnull(strstr(prompt, "No search assistance"));
    g_assert_null(strstr(prompt, "greatest achievable"));
    g_assert_cmpuint(f->generate_calls, ==, 1); /* The model chooses the opening too. */
    g_assert_nonnull(strstr(prompt,"Allowed squares"));
    g_assert_cmpuint(call.result.allowed,==,game_legal_moves(&call.game));
    call.options.thinking = TRUE;
    f->info_body = "{\"thinking\":{\"values\":[false,true],\"default\":false}}";
    run(&call); g_assert_true(call.ok); g_assert_cmpuint(f->show_calls, ==, 1);
    g_assert_true(json_object_get_boolean_member(f->request, "think"));
    g_assert_cmpint(call.result.reasoning, ==, OLLAMA_REASONING_NATIVE);
    call.model = "cogito:3b";
    unsigned generations = f->generate_calls;
    run(&call); g_assert_true(call.ok); g_assert_cmpuint(f->generate_calls, ==, generations + 2);
    g_assert_cmpint(call.result.reasoning, ==, OLLAMA_REASONING_COGITO);
    g_assert_cmpuint(counts.counts[OLLAMA_THINKING],>,0);
    game_init_variant(&call.game,3,FALSE);call.game.red=SQUARE_BIT(0)|SQUARE_BIT(7)|SQUARE_BIT(14);
    generations=f->generate_calls;run(&call);g_assert_false(call.ok);g_assert_error(call.error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT);
    g_assert_cmpuint(f->generate_calls,==,generations);g_clear_error(&call.error);
    g_assert_cmpuint(counts.counts[OLLAMA_FAILURE],==,1);g_object_unref(call.cancel);

}
static void invalid(Fixture *f, gconstpointer data)
{
    (void) data; if (!f->url) return;
    const char *bad[]={
        "{\"done\":true,\"response\":\"{\\\"square\\\":\\\"g7\\\"}\"}",
        "{\"done\":true,\"response\":\"{\\\"square\\\":19}\"}",
        "{\"done\":true,\"response\":\"{\\\"square\\\":\\\"d3\\\",\\\"extra\\\":1}\"}",
        "{\"done\":false,\"response\":\"{}\"}","broken","[]"};
    Call call={.fixture=f};game_init(&call.game);
    for(unsigned i=0;i<G_N_ELEMENTS(bad);i++){f->body=bad[i];run(&call);g_assert_false(call.ok);g_assert_nonnull(call.error);g_clear_error(&call.error);}
    f->status = 404; f->body = "{\"error\":\"model missing\"}";
    run(&call); g_assert_error(call.error, G_IO_ERROR, G_IO_ERROR_FAILED); g_clear_error(&call.error);
    f->status = 200; f->body = "{}"; f->delayed = TRUE; call.timeout = 20;
    run(&call); g_assert_error(call.error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT); g_clear_error(&call.error);
    f->delayed = FALSE;
    call.cancel = g_cancellable_new(); g_cancellable_cancel(call.cancel);
    run(&call); g_assert_error(call.error, G_IO_ERROR, G_IO_ERROR_CANCELLED); g_clear_error(&call.error); g_object_unref(call.cancel);
}
static void parse_and_assist(void)
{
    Game game;game_init(&game);OllamaAnalysis analysis;ollama_analyze(&game,&analysis);
    g_assert_cmpuint(analysis.allowed,==,game_legal_moves(&game));int move=-1;GError *error=NULL;
    g_assert_true(ollama_parse_move(&game,"{\"done\":true,\"response\":\"{\\\"square\\\":\\\"d3\\\"}\"}",&move,&error));
    g_assert_no_error(error);g_assert_cmpint(move,==,17);
    g_assert_true(game_play(&game,game_point("b3")));
    g_assert_true(ollama_parse_move(&game,"{\"done\":true,\"response\":\"{\\\"square\\\":\\\"swap\\\"}\"}",&move,&error));
    g_assert_no_error(error);g_assert_cmpint(move,==,GAME_SWAP);

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
