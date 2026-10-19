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
    f->auto_move = TRUE;
    Call call = {.fixture = f}; game_init(&call.game);
    run(&call); g_assert_true(call.ok); g_assert_no_error(call.error);
    g_assert_true(game_legal(&call.game, call.result.move));
    const char *prompt = json_object_get_string_member(f->request, "prompt");
    g_assert_nonnull(strstr(prompt, "No search assistance"));
    g_assert_null(strstr(prompt, "greatest achievable"));
    g_assert_cmpuint(f->generate_calls, ==, 1); /* The model chooses the opening too. */
    g_assert_nonnull(strstr(prompt, "i14"));
    Move last = game_moves[55];
    g_autofree char *high_move = g_strdup_printf("{\"done\":true,\"response\":\"{\\\"entry\\\":\\\"%s\\\",\\\"destination\\\":\\\"%s\\\"}\"}",
        game_labels[last.entry], game_labels[last.destination]);
    int chosen = -1; GError *parse_error = NULL;
    g_assert_true(ollama_parse_move(&call.game, high_move, &chosen, &parse_error));
    g_assert_no_error(parse_error); g_assert_cmpint(chosen, ==, 55);
    call.options.thinking = TRUE;
    f->info_body = "{\"thinking\":{\"values\":[false,true],\"default\":false}}";
    run(&call); g_assert_true(call.ok); g_assert_cmpuint(f->show_calls, ==, 1);
    g_assert_true(json_object_get_boolean_member(f->request, "think"));
    g_assert_cmpint(call.result.reasoning, ==, OLLAMA_REASONING_NATIVE);
    call.model = "cogito:3b";
    unsigned generations = f->generate_calls;
    run(&call); g_assert_true(call.ok); g_assert_cmpuint(f->generate_calls, ==, generations + 2);
    g_assert_cmpint(call.result.reasoning, ==, OLLAMA_REASONING_COGITO);
    call.options = (OllamaOptions) {.assisted = TRUE}; call.model = "test:1b";
    while (!game_result(&call.game)) {
        run(&call); g_assert_true(call.ok); g_assert_no_error(call.error);
        guint64 allowed; g_assert_true(search_allowed(&call.game, NULL, &allowed));
        g_assert_cmpuint(call.result.allowed, ==, allowed);
        g_assert_true(game_play(&call.game, call.result.move));
    }
    g_assert_cmpint(game_result(&call.game), ==, 1);
}
static void invalid(Fixture *f, gconstpointer data)
{
    (void) data; if (!f->url) return;
    const char *bad[] = {
        "{\"done\":true,\"response\":\"{\\\"entry\\\":\\\"v1\\\",\\\"destination\\\":\\\"v3\\\"}\"}",
        "{\"done\":true,\"response\":\"{\\\"entry\\\":0,\\\"destination\\\":6}\"}",
        "{\"done\":false,\"response\":\"{}\"}", "broken", "[]",
        "{\"done\":true,\"response\":\"{\\\"entry\\\":\\\"v1\\\",\\\"destination\\\":\\\"i2\\\",\\\"extra\\\":1}\"}"
    };
    Call call = {.fixture = f}; game_init(&call.game);
    for (unsigned i = 0; i < G_N_ELEMENTS(bad); i++) {
        f->body = bad[i]; run(&call); g_assert_false(call.ok); g_assert_nonnull(call.error); g_clear_error(&call.error);
    }
    Move first = game_moves[0];
    g_autofree char *occupied_response = g_strdup_printf("{\"done\":true,\"response\":\"{\\\"entry\\\":\\\"%s\\\",\\\"destination\\\":\\\"%s\\\"}\"}",
        game_labels[first.entry], game_labels[first.destination]);
    f->body = occupied_response;
    g_assert_true(game_play(&call.game, 0)); /* Destination now occupied. */
    run(&call); g_assert_false(call.ok); g_clear_error(&call.error);
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
    Game game; game_init(&game);
    OllamaAnalysis pure, assisted;
    g_assert_true(ollama_analyze(&game, FALSE, NULL, &pure, NULL));
    g_assert_cmpuint(pure.allowed, ==, game_legal_moves(&game));
    g_assert_true((pure.allowed & MOVE_BIT(55)) != 0);
    g_autoptr(GRand) random = g_rand_new_with_seed(91);
    gboolean narrowed = FALSE;
    for (unsigned sample = 0; sample < 80; sample++) {
        game_init(&game);
        while (!game_result(&game) && game.moves < 12 + sample % 7)
            game_play(&game, game_random_move(game_legal_moves(&game), random));
        if (game_result(&game)) continue;
        g_assert_true(ollama_analyze(&game, FALSE, NULL, &pure, NULL));
        g_assert_true(ollama_analyze(&game, TRUE, NULL, &assisted, NULL));
        g_assert_cmpuint(assisted.allowed & ~pure.allowed, ==, 0);
        if (assisted.allowed != pure.allowed) narrowed = TRUE;
    }
    g_assert_true(narrowed);
    g_autoptr(GCancellable) cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    GError *cancel_error = NULL;
    g_assert_false(ollama_analyze(&game, TRUE, cancel, &assisted, &cancel_error));
    g_assert_error(cancel_error, G_IO_ERROR, G_IO_ERROR_CANCELLED); g_clear_error(&cancel_error);
    GError *error = NULL;
    g_auto(GStrv) models = ollama_parse_models("{\"models\":[{\"name\":\"z:1b\"},{\"name\":\"a:1b\"},{\"name\":\"z:1b\"}]}", &error);
    g_assert_no_error(error); g_assert_cmpuint(g_strv_length(models), ==, 2); g_assert_cmpstr(models[0], ==, "a:1b");
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
