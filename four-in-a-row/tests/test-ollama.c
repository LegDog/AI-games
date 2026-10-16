// SPDX-License-Identifier: GPL-3.0-or-later
#include "ollama.h"
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>

#include "ollama-server.h"

typedef struct {
    Fixture *fixture;
    gboolean list, done, ok, configured;
    OllamaOptions options;
    Game game;
    const char *model;
    guint timeout;
    GCancellable *cancel;
    char **names;
    OllamaResult result;
    GError *error;
} Call;

static void worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    (void) source;
    Call *call = data;
    if (call->list)
        call->names = ollama_list_models(call->fixture->url, call->timeout, cancel, &call->error);
    else if (call->configured)
        call->ok = ollama_choose_with_options(&call->game, call->model, call->fixture->url,
            call->timeout, cancel, &call->options, &call->result, &call->error);
    else
        call->ok = ollama_choose(&call->game, call->model, call->fixture->url,
                                call->timeout, cancel, &call->result, &call->error);
    g_task_return_boolean(task, TRUE);
}

static void done(GObject *source, GAsyncResult *result, gpointer data)
{
    (void) source;
    (void) result;
    ((Call *) data)->done = TRUE;
}

static void run(Call *call)
{
    call->done = FALSE;
    if (!call->timeout) call->timeout = 2000;
    g_autoptr(GTask) task = g_task_new(NULL, call->cancel, done, call);
    g_task_set_task_data(task, call, NULL);
    g_task_set_return_on_cancel(task, FALSE);
    g_task_run_in_thread(task, worker);
    while (!call->done) g_main_context_iteration(NULL, TRUE);
}

static void test_models(Fixture *f, gconstpointer data)
{
    (void) data;
    if (!f->url) return;
    f->body = "{\"models\":[{\"name\":\"z:latest\"},{\"name\":\"a:1b\"},{\"name\":\"z:latest\"}]}";
    Call call = {.fixture = f, .list = TRUE};
    run(&call);
    g_assert_no_error(call.error);
    g_assert_cmpstr(call.names[0], ==, "a:1b");
    g_assert_cmpstr(call.names[1], ==, "z:latest");
    g_assert_null(call.names[2]);
    g_strfreev(call.names);
    f->body = "{\"models\":[]}";
    run(&call);
    g_assert_no_error(call.error);
    g_assert_nonnull(call.names);
    g_assert_null(call.names[0]);
    g_strfreev(call.names);
    const char *bad[] = {"[]", "{\"models\":null}", "{\"models\":[{\"name\":2}]}", "broken"};
    for (unsigned i = 0; i < G_N_ELEMENTS(bad); i++) {
        f->body = bad[i];
        run(&call);
        g_assert_null(call.names);
        g_assert_nonnull(call.error);
        g_clear_error(&call.error);
    }
}

static void test_moves(Fixture *f, gconstpointer data)
{
    (void) data;
    if (!f->url) return;
    Call call = {.fixture = f, .model = "a:1b"};
    game_init(&call.game);
    /* Every valid number is translated to the engine's move index. */
    for (unsigned move = 0; move < GAME_CHOICES; move++) {
        g_autofree char *body = g_strdup_printf("{\"done\":true,\"response\":\"{\\\"move\\\":%u}\"}", ollama_move_number(move));
        f->body = body;
        run(&call);
        g_assert_true(call.ok);
        g_assert_no_error(call.error);
        g_assert_cmpint(call.result.move, ==, move);
        g_assert_cmpuint(call.game.moves, ==, 0);
        g_assert_cmpstr(json_object_get_string_member(f->request, "model"), ==, "a:1b");
        g_assert_false(json_object_get_boolean_member(f->request, "stream"));
        const char *prompt = json_object_get_string_member(f->request, "prompt");
        g_assert_nonnull(strstr(prompt, "You play 1; opponent plays -1"));
        JsonObject *schema = json_object_get_object_member(f->request, "format");
        JsonObject *props = json_object_get_object_member(schema, "properties");
        JsonObject *prop = json_object_get_object_member(props, "move");
        g_assert_cmpuint(json_array_get_length(json_object_get_array_member(prop, "enum")), ==, GAME_CHOICES);
    }
    /* Exclude an occupied cell / a full column, then reject it even if the
     * service ignores the structured-output constraints. */
    if (GAME_CHOICES == 7) {
        for (unsigned i = 0; i < GAME_ROWS; i++) g_assert_true(game_play(&call.game, 0));
    } else g_assert_true(game_play(&call.game, 0));
    g_autofree char *body = g_strdup_printf("{\"done\":true,\"response\":\"{\\\"move\\\":%u}\"}", ollama_move_number(0));
    f->body = body;
    run(&call);
    g_assert_false(call.ok);
    g_assert_error(call.error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&call.error);
    JsonObject *props = json_object_get_object_member(json_object_get_object_member(f->request, "format"), "properties");
    JsonArray *allowed = json_object_get_array_member(json_object_get_object_member(props, "move"), "enum");
    g_assert_cmpuint(json_array_get_length(allowed), ==, GAME_CHOICES - 1);
    for (unsigned i = 0; i < json_array_get_length(allowed); i++)
        g_assert_cmpint(json_array_get_int_element(allowed, i), !=, ollama_move_number(0));
}

static void test_invalid(Fixture *f, gconstpointer data)
{
    (void) data;
    if (!f->url) return;
    const char *bad[] = {
        "{\"done\":true,\"response\":\"{\\\"move\\\":0}\"}",
        "{\"done\":true,\"response\":\"{\\\"move\\\":1.5}\"}",
        "{\"done\":true,\"response\":\"{\\\"move\\\":\\\"1\\\"}\"}",
        "{\"done\":true,\"response\":\"{\\\"move\\\":true}\"}",
        "{\"done\":true,\"response\":\"{\\\"move\\\":1,\\\"other\\\":2}\"}",
        "{\"done\":true,\"response\":\"[]\"}",
        "{\"done\":false,\"response\":\"{\\\"move\\\":1}\"}",
        "{\"done\":true,\"response\":\"I pick 1\"}",
        "{\"done\":true,\"response\":3}", "[]", "broken"
    };
    Call call = {.fixture = f, .model = "a:1b"};
    game_init(&call.game);
    for (unsigned i = 0; i < G_N_ELEMENTS(bad); i++) {
        f->body = bad[i];
        run(&call);
        g_assert_false(call.ok);
        g_assert_nonnull(call.error);
        g_clear_error(&call.error);
        g_assert_cmpint(call.result.move, ==, -1);
    }
    f->status = 404;
    f->body = "{\"error\":\"model missing\"}";
    run(&call);
    g_assert_error(call.error, G_IO_ERROR, G_IO_ERROR_FAILED);
    g_assert_nonnull(strstr(call.error->message, "model missing"));
    g_clear_error(&call.error);
    g_autofree char *trace = NULL;
    GError *error = NULL;
    g_assert_true(g_file_get_contents(g_getenv("OLLAMA_ERROR_LOG_FILE"), &trace, NULL, &error));
    g_assert_no_error(error);
    g_auto(GStrv) lines = g_strsplit(trace, "\n", -1);
    gboolean found = FALSE;
    for (unsigned i = 0; lines[i] && *lines[i]; i++) {
        g_autoptr(JsonParser) parser = json_parser_new();
        g_assert_true(json_parser_load_from_data(parser, lines[i], -1, &error));
        g_assert_no_error(error);
        JsonObject *entry = json_node_get_object(json_parser_get_root(parser));
        if (!g_str_equal(json_object_get_string_member(entry, "event"), "http_error")) continue;
        found = TRUE;
        g_assert_cmpint(json_object_get_int_member(entry, "status"), ==, 404);
        g_assert_cmpstr(json_object_get_string_member(entry, "body"), ==, f->body);
        g_assert_cmpstr(json_object_get_string_member(entry, "model"), ==, call.model);
    }
    g_assert_true(found);
}

static gboolean cancel_call(gpointer data)
{
    g_cancellable_cancel(data);
    return G_SOURCE_REMOVE;
}

static void test_deadline(Fixture *f, gconstpointer data)
{
    (void) data;
    if (!f->url) return;
    f->body = "{}";
    f->delayed = TRUE;
    Call call = {.fixture = f, .list = TRUE, .timeout = 50};
    run(&call);
    g_assert_error(call.error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
    g_clear_error(&call.error);
    g_autoptr(GCancellable) cancel = g_cancellable_new();
    call.cancel = cancel;
    call.timeout = 2000;
    g_timeout_add(50, cancel_call, cancel);
    run(&call);
    g_assert_error(call.error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_clear_error(&call.error);
}

static void test_cancel_without_service(void)
{
    g_autoptr(GCancellable) cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    GError *error = NULL;
    g_assert_null(ollama_list_models(NULL, 100, cancel, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_clear_error(&error);
    Game game; game_init(&game);
    OllamaResult result;
    g_assert_false(ollama_choose(&game, "model:tag", NULL, 100, cancel, &result, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_clear_error(&error);
    g_assert_cmpint(result.move, ==, -1);
    g_assert_cmpuint(game.moves, ==, 0);
    g_assert_false(ollama_choose(&game, "", NULL, 100, cancel, &result, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
    g_clear_error(&error);
    g_autoptr(GString) history = g_string_new(NULL);
    result = (OllamaResult) {.move = 0, .elapsed_us = 250000};
    ollama_record(history, "model:tag", &result);
    g_assert_nonnull(strstr(history->str, "model:tag"));
    g_assert_nonnull(strstr(history->str, "0.250 s"));
    /* Even a cancellation before transmission logs the complete attempted
     * request and an explicit cancellation, with matching request IDs. */
    g_autofree char *trace = NULL;
    g_assert_true(g_file_get_contents(g_getenv("OLLAMA_LOG_FILE"), &trace, NULL, &error));
    g_assert_no_error(error);
    g_assert_nonnull(strstr(trace, "model:tag"));
    g_auto(GStrv) lines = g_strsplit(trace, "\n", -1);
    unsigned requests = 0, cancellations = 0;
    for (unsigned i = 0; lines[i] && *lines[i]; i++) {
        g_autoptr(JsonParser) parser = json_parser_new();
        g_assert_true(json_parser_load_from_data(parser, lines[i], -1, &error));
        g_assert_no_error(error);
        JsonObject *entry = json_node_get_object(json_parser_get_root(parser));
        const char *event = json_object_get_string_member(entry, "event");
        requests += g_str_equal(event, "request");
        cancellations += g_str_equal(event, "cancelled");
    }
    g_assert_cmpuint(requests, >=, 2);
    g_assert_cmpuint(cancellations, >=, 2);
    g_autofree char *error_trace = NULL;
    g_assert_true(g_file_get_contents(g_getenv("OLLAMA_ERROR_LOG_FILE"), &error_trace, NULL, &error));
    g_assert_no_error(error);
    g_assert_nonnull(strstr(error_trace, "model:tag"));
    g_auto(GStrv) failures = g_strsplit(error_trace, "\n", -1);
    for (unsigned i = 0; failures[i] && *failures[i]; i++) {
        g_autoptr(JsonParser) parser = json_parser_new();
        g_assert_true(json_parser_load_from_data(parser, failures[i], -1, &error));
        g_assert_no_error(error);
        JsonObject *entry = json_node_get_object(json_parser_get_root(parser));
        const char *event = json_object_get_string_member(entry, "event");
        g_assert_cmpstr(event, !=, "request");
        g_assert_cmpstr(event, !=, "response");
        g_assert_nonnull(json_object_get_string_member(entry, "operation_id"));
        g_assert_nonnull(json_object_get_string_member(entry, "error"));
    }
}

static void test_protocol_without_network(void)
{
    GError *error = NULL;
    char **names = ollama_parse_models("{\"models\":[{\"name\":\"z:1\"},{\"name\":\"a:2\"},{\"name\":\"z:1\"}]}", &error);
    g_assert_no_error(error);
    g_assert_cmpstr(names[0], ==, "a:2");
    g_assert_cmpstr(names[1], ==, "z:1");
    g_assert_null(names[2]);
    g_strfreev(names);
    names = ollama_parse_models("{\"models\":[]}", &error);
    g_assert_no_error(error);
    g_assert_null(names[0]);
    g_strfreev(names);
    const char *bad_lists[] = {"[]", "{}", "{\"models\":null}", "{\"models\":[{\"name\":4}]}", "{\"models\":[{\"name\":\"\"}]}", "broken"};
    for (unsigned i = 0; i < G_N_ELEMENTS(bad_lists); i++) {
        g_assert_null(ollama_parse_models(bad_lists[i], &error));
        g_assert_nonnull(error);
        g_clear_error(&error);
    }
    Game game; game_init(&game);
    const unsigned keypad[] = {7,8,9,4,5,6,1,2,3};
    for (unsigned i = 0; i < GAME_CHOICES; i++) {
        unsigned number = GAME_CHOICES == 7 ? i + 1 : keypad[i];
        g_autofree char *body = g_strdup_printf("{\"done\":true,\"response\":\"{\\\"move\\\":%u}\"}", number);
        int chosen = -1;
        g_assert_true(ollama_parse_move(&game, body, &chosen, &error));
        g_assert_no_error(error);
        g_assert_cmpint(chosen, ==, i);
    }
    const char *bad_answers[] = {"{}", "[]", "{\"move\":0}", "{\"move\":-1}", "{\"move\":999}", "{\"move\":1.5}", "{\"move\":true}", "{\"move\":null}", "{\"move\":\"1\"}", "{\"move\":1,\"extra\":2}", "```json\n{\"move\":1}\n```"};
    for (unsigned i = 0; i < G_N_ELEMENTS(bad_answers); i++) {
        g_autoptr(JsonObject) response = json_object_new();
        json_object_set_boolean_member(response, "done", TRUE);
        json_object_set_string_member(response, "response", bad_answers[i]);
        g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
        json_node_set_object(node, response);
        g_autofree char *json = json_to_string(node, FALSE);
        int chosen = 5;
        g_assert_false(ollama_parse_move(&game, json, &chosen, &error));
        g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
        g_assert_cmpint(chosen, ==, -1);
        g_clear_error(&error);
    }
    /* Schema compliance cannot replace engine validation. */
    if (GAME_CHOICES == 7) {
        for (unsigned i = 0; i < GAME_ROWS; i++) g_assert_true(game_play(&game, 0));
    } else g_assert_true(game_play(&game, 0));
    g_autofree char *illegal = g_strdup_printf("{\"done\":true,\"response\":\"{\\\"move\\\":%u}\"}", GAME_CHOICES == 7 ? 1 : 7);
    int chosen;
    g_assert_false(ollama_parse_move(&game, illegal, &chosen, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&error);
    const char *bad_responses[] = {"[]", "broken", "{\"done\":false,\"response\":\"{\\\"move\\\":2}\"}", "{\"done\":true,\"response\":3}", "{\"done\":{},\"response\":\"{}\"}"};
    for (unsigned i = 0; i < G_N_ELEMENTS(bad_responses); i++) {
        g_assert_false(ollama_parse_move(&game, bad_responses[i], &chosen, &error));
        g_assert_nonnull(error);
        g_clear_error(&error);
    }
}

static unsigned mirror_move(unsigned move)
{
    return GAME_CHOICES == 7 ? 6 - move : (move / 3) * 3 + 2 - move % 3;
}

static void test_tactical_filter(void)
{
    Game empty; game_init(&empty);
    OllamaAnalysis analysis;
    for (unsigned assisted = 0; assisted < 2; assisted++) {
        ollama_analyze(&empty, assisted, &analysis);
        g_assert_cmpuint(analysis.allowed, ==, (1u << GAME_CHOICES) - 1);
    }
    /* Engine indexes, including recorded Cogito losses from 18:43:39/54. */
    const unsigned c4_block1[] = {6,3,4,3,0,3};
    const unsigned c4_block2[] = {6,3,5,2,0,4};
    const unsigned c4_win[] = {0,6,1,6,2,5};
    const unsigned c4_lost[] = {0,3,1,4,0,5};
    const unsigned ttt_block1[] = {0,3,8,4};
    const unsigned ttt_block2[] = {8,0,4,1};
    const unsigned ttt_win[] = {0,3,1,4};
    const unsigned ttt_lost[] = {0,4,8,2,6};
    const unsigned *cases[] = {
        GAME_CHOICES == 7 ? c4_block1 : ttt_block1,
        GAME_CHOICES == 7 ? c4_block2 : ttt_block2,
        GAME_CHOICES == 7 ? c4_win : ttt_win,
        GAME_CHOICES == 7 ? c4_lost : ttt_lost
    };
    const unsigned counts[] = {GAME_CHOICES == 7 ? 6 : 4, GAME_CHOICES == 7 ? 6 : 4,
        GAME_CHOICES == 7 ? 6 : 4, GAME_CHOICES == 7 ? 6 : 5};
    const unsigned required[] = {GAME_CHOICES == 7 ? 3 : 5, GAME_CHOICES == 7 ? 1 : 2,
        GAME_CHOICES == 7 ? 3 : 2};
    for (unsigned k = 0; k < 4; k++) {
        for (unsigned mirror = 0; mirror < 2; mirror++) {
            for (int color = -1; color <= 1; color += 2) {
                Game game; game_init(&game); game.turn = color;
                for (unsigned i = 0; i < counts[k]; i++)
                    g_assert_true(game_play(&game, mirror ? mirror_move(cases[k][i]) : cases[k][i]));
                Game original = game;
                ollama_analyze(&game, TRUE, &analysis);
                g_assert_cmpmem(&game, sizeof game, &original, sizeof original);
                g_assert_cmpuint(analysis.legal, !=, 0);
                if (k < 3) {
                    unsigned chosen = mirror ? mirror_move(required[k]) : required[k];
                    g_assert_cmpuint(analysis.allowed, ==, 1u << chosen);
                    if (k == 2) g_assert_cmpuint(analysis.winning, ==, 1u << chosen);
                } else {
                    g_assert_cmpuint(analysis.safe, ==, 0);
                    g_assert_cmpuint(analysis.allowed, ==, analysis.legal);
                }
                g_autofree char *assisted_prompt = ollama_build_prompt(&game, &analysis, TRUE);
                g_assert_nonnull(strstr(assisted_prompt, "engine-verified immediate facts"));
                OllamaAnalysis pure;
                ollama_analyze(&game, FALSE, &pure);
                g_assert_cmpuint(pure.allowed, ==, analysis.legal);
                g_autofree char *prompt = ollama_build_prompt(&game, &pure, FALSE);
                g_assert_null(strstr(prompt, "engine-verified"));
                g_assert_nonnull(strstr(prompt, "Y = YOUR pieces; A = ADVERSARY pieces; . = EMPTY"));
                g_assert_nonnull(strstr(prompt, "Row 1:"));
                g_assert_nonnull(strstr(prompt, "Legal moves and placement coordinates:"));
            }
        }
    }
    if (GAME_CHOICES == 7) {
        Game game; game_init(&game);
        g_assert_true(game_play(&game, 3));
        g_assert_true(game_play(&game, 3));
        ollama_analyze(&game, FALSE, &analysis);
        g_autofree char *prompt = ollama_build_prompt(&game, &analysis, FALSE);
        g_assert_nonnull(strstr(prompt, "Move 4 -> row 4, column 4."));
        g_assert_nonnull(strstr(prompt, "Row 5: . . . A . . ."));
        g_assert_nonnull(strstr(prompt, "Row 6: . . . Y . . ."));
    }
}

static void test_configured_reasoning(Fixture *f, gconstpointer data)
{
    (void) data;
    if (!f->url) return;
    f->auto_move = TRUE;
    Call call = {.fixture = f, .model = "cogito:3b", .configured = TRUE,
        .options = {.thinking = TRUE}};
    game_init(&call.game);
    run(&call);
    g_assert_true(call.ok);
    g_assert_no_error(call.error);
    g_assert_cmpuint(f->generate_calls, ==, 2);
    g_assert_cmpuint(f->show_calls, ==, 0);
    g_assert_cmpuint(call.result.reasoning, ==, OLLAMA_REASONING_COGITO);
    JsonObject *first = g_ptr_array_index(f->requests, 0);
    g_assert_cmpstr(json_object_get_string_member(first, "system"), ==, "Enable deep thinking subroutine.");
    g_assert_false(json_object_has_member(first, "format"));
    g_assert_true(json_object_has_member(f->request, "format"));
    g_assert_nonnull(strstr(json_object_get_string_member(f->request, "prompt"), "Previous analysis from this model"));
    g_assert_null(strstr(json_object_get_string_member(f->request, "system"), "Enable deep thinking"));
    call.model = "native:1b";
    f->info_body = "{\"thinking\":{\"values\":[false,true],\"default\":false}}";
    run(&call);
    g_assert_true(call.ok);
    g_assert_no_error(call.error);
    g_assert_cmpuint(call.result.reasoning, ==, OLLAMA_REASONING_NATIVE);
    g_assert_true(json_object_get_boolean_member(f->request, "think"));
    f->info_body = "{\"thinking\":{\"values\":[\"low\",\"medium\",\"high\"],\"default\":\"medium\"}}";
    run(&call);
    g_assert_true(call.ok);
    g_assert_no_error(call.error);
    g_assert_cmpstr(json_object_get_string_member(f->request, "think"), ==, "high");
    f->info_body = "{}";
    run(&call);
    g_assert_true(call.ok);
    g_assert_no_error(call.error);
    g_assert_false(json_object_has_member(f->request, "think"));
    g_assert_cmpuint(call.result.reasoning, ==, OLLAMA_REASONING_DEFAULT);
    unsigned queries = f->show_calls, generations = f->generate_calls;
    call.options.thinking = FALSE;
    call.model = "cogito:3b";
    run(&call);
    g_assert_true(call.ok);
    g_assert_no_error(call.error);
    g_assert_cmpuint(f->show_calls, ==, queries);
    g_assert_cmpuint(f->generate_calls, ==, generations + 1);
    g_assert_false(json_object_has_member(f->request, "system"));
    /* An unfinished analysis never triggers a decision call or a fallback. */
    f->auto_move = FALSE;
    f->body = "{\"done\":false,\"response\":\"unfinished analysis\"}";
    call.options.thinking = TRUE;
    generations = f->generate_calls;
    run(&call);
    g_assert_false(call.ok);
    g_assert_error(call.error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&call.error);
    g_assert_cmpuint(f->generate_calls, ==, generations + 1);
    g_assert_cmpint(call.result.move, ==, -1);
}

static void test_assisted_response(Fixture *f, gconstpointer data)
{
    (void) data;
    if (!f->url) return;
    Call call = {.fixture = f, .model = "model:1b", .configured = TRUE,
        .options = {.assisted = TRUE}};
    game_init(&call.game);
    const unsigned c4[] = {6,3,4,3,0,3};
    const unsigned ttt[] = {0,3,8,4};
    const unsigned *moves = GAME_CHOICES == 7 ? c4 : ttt;
    for (unsigned i = 0; i < (GAME_CHOICES == 7 ? 6u : 4u); i++)
        g_assert_true(game_play(&call.game, moves[i]));
    /* The service deliberately violates the narrower schema with a legal
     * but losing move. Reject instead of replacing it with an engine move. */
    g_autofree char *bad = g_strdup_printf("{\"done\":true,\"response\":\"{\\\"move\\\":%u}\"}", ollama_move_number(GAME_CHOICES == 7 ? 0 : 1));
    f->body = bad;
    run(&call);
    g_assert_false(call.ok);
    g_assert_error(call.error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&call.error);
    g_assert_cmpint(call.result.move, ==, -1);
    f->auto_move = TRUE;
    run(&call);
    g_assert_true(call.ok);
    g_assert_no_error(call.error);
    g_assert_cmpint(call.result.move, ==, GAME_CHOICES == 7 ? 3 : 5);
    g_assert_true(call.result.assisted);
    g_autoptr(GString) history = g_string_new(NULL);
    ollama_record(history, call.model, &call.result);
    g_assert_nonnull(strstr(history->str, "modo: assistido"));
    g_assert_nonnull(strstr(history->str, "Alternativas permitidas"));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    GError *error = NULL;
    g_autofree char *directory = g_dir_make_tmp("ollama-client-log-XXXXXX", &error);
    g_assert_no_error(error);
    g_autofree char *log_path = g_build_filename(directory, OLLAMA_DEFAULT_LOG_FILE, NULL);
    g_setenv("OLLAMA_LOG_FILE", log_path, TRUE);
    g_autofree char *error_log_path = g_build_filename(directory, OLLAMA_DEFAULT_ERROR_LOG_FILE, NULL);
    g_setenv("OLLAMA_ERROR_LOG_FILE", error_log_path, TRUE);
    g_test_add_func("/ollama/tactical-filter-without-network", test_tactical_filter);
    g_test_add_func("/ollama/protocol-without-network", test_protocol_without_network);
    g_test_add_func("/ollama/cancel-without-service", test_cancel_without_service);
    g_test_add("/ollama/configured-reasoning", Fixture, NULL, setup, test_configured_reasoning, teardown);
    g_test_add("/ollama/assisted-response", Fixture, NULL, setup, test_assisted_response, teardown);
    g_test_add("/ollama/models", Fixture, NULL, setup, test_models, teardown);
    g_test_add("/ollama/moves", Fixture, NULL, setup, test_moves, teardown);
    g_test_add("/ollama/invalid", Fixture, NULL, setup, test_invalid, teardown);
    g_test_add("/ollama/deadline-cancel", Fixture, NULL, setup, test_deadline, teardown);
    int result = g_test_run();
    g_remove(log_path);
    g_remove(error_log_path);
    g_rmdir(directory);
    return result;
}
