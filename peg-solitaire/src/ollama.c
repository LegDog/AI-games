// SPDX-License-Identifier: GPL-3.0-or-later
#include "ollama.h"
#include "i18n.h"
#include "search.h"
#include <string.h>
#include <unistd.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

/* A private context makes the total deadline cover connection, model loading
 * and generation, rather than just inactivity on a socket. */
typedef struct {
    GMainLoop *loop;
    GCancellable *cancel;
    GBytes *bytes;
    GError *error;
    gboolean timed_out;
} Request;

static void cancel_request(GCancellable *parent, gpointer data)
{
    (void) parent;
    g_cancellable_cancel(data);
}

static gboolean deadline(gpointer data)
{
    Request *request = data;
    request->timed_out = TRUE;
    g_cancellable_cancel(request->cancel);
    return G_SOURCE_REMOVE;
}

static void received(GObject *source, GAsyncResult *result, gpointer data)
{
    Request *request = data;
    request->bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &request->error);
    g_main_loop_quit(request->loop);
}

static char *serialize(JsonObject *object)
{
    g_autoptr(JsonNode) node = json_node_new(JSON_NODE_OBJECT);
    json_node_set_object(node, object);
    return json_to_string(node, FALSE);
}

typedef struct {
    const char *id, *operation, *model, *path, *method;
    char request_id[37];
    guint status;
    gint64 started;
    gboolean assisted, thinking, error_logged;
} OllamaTrace;

/* Every worker has its own operation context, including all reasoning stages.
 * Save/restore at public API boundaries; no pointers outlive their operation. */
static GPrivate operation_trace = G_PRIVATE_INIT(NULL);

static gboolean append_log(const char *path, const char *line)
{
    /* The report writer requires regular files; explicitly support discarding
     * logs without opening or locking the shared /dev/null device. */
    if (g_str_equal(path, "/dev/null")) return TRUE;
    GError *error = NULL;
    if (game_append_report(path, line, &error)) return TRUE;
    g_printerr(_("Could not write Ollama log: %s\n"), error->message);
    g_clear_error(&error);
    return FALSE;
}

/* JSON Lines retain the exact payload text, including thinking and malformed
 * error bodies. The existing append helper locks across processes and rolls
 * back partial writes, so both games may safely share the same log. */
static void log_exchange(const char *id, const char *event, const char *method,
                         const char *url, SoupMessage *message, GBytes *body,
                         gint64 started, const char *error)
{
    g_autoptr(JsonObject) entry = json_object_new();
    ollama_run_annotate(entry);
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    g_autofree char *timestamp = g_date_time_format(now, "%Y-%m-%dT%H:%M:%S.%f%:z");
    json_object_set_string_member(entry, "timestamp", timestamp);
    json_object_set_string_member(entry, "game", GAME_ID);
    json_object_set_int_member(entry, "pid", getpid());
    json_object_set_string_member(entry, "id", id);
    OllamaTrace *trace = g_private_get(&operation_trace);
    if (trace) {
        json_object_set_string_member(entry, "operation_id", trace->id);
        json_object_set_string_member(entry, "operation", trace->operation);
        json_object_set_int_member(entry, "operation_elapsed_us", g_get_monotonic_time() - trace->started);
        if (trace->model) json_object_set_string_member(entry, "model", trace->model);
        json_object_set_boolean_member(entry, "assisted", trace->assisted);
        json_object_set_boolean_member(entry, "thinking_requested", trace->thinking);
        if (*trace->request_id) json_object_set_string_member(entry, "request_id", trace->request_id);
        if (!message && trace->status) json_object_set_int_member(entry, "status", trace->status);
    }
    json_object_set_string_member(entry, "event", event);
    json_object_set_string_member(entry, "method", method);
    json_object_set_string_member(entry, "url", url);
    json_object_set_int_member(entry, "elapsed_us", g_get_monotonic_time() - started);
    if (message) {
        gboolean outgoing = g_str_equal(event, "request");
        if (!outgoing)
            json_object_set_int_member(entry, "status", soup_message_get_status(message));
        SoupMessageHeaders *headers = outgoing ? soup_message_get_request_headers(message) :
            soup_message_get_response_headers(message);
        SoupMessageHeadersIter iter;
        const char *name, *value;
        JsonArray *items = json_array_new();
        soup_message_headers_iter_init(&iter, headers);
        while (soup_message_headers_iter_next(&iter, &name, &value)) {
            JsonObject *header = json_object_new();
            json_object_set_string_member(header, "name", name);
            json_object_set_string_member(header, "value", value);
            json_array_add_object_element(items, header);
        }
        json_object_set_array_member(entry, "headers", items);
    }
    if (body) {
        gsize size;
        const char *data = g_bytes_get_data(body, &size);
        json_object_set_int_member(entry, "body_bytes", size);
        if (!size || g_utf8_validate(data, size, NULL)) {
            g_autofree char *text = size ? g_strndup(data, size) : g_strdup("");
            json_object_set_string_member(entry, "body", text);
        } else {
            g_autofree char *encoded = g_base64_encode((const guchar *) data, size);
            json_object_set_string_member(entry, "body_base64", encoded);
        }
    }
    if (error) json_object_set_string_member(entry, "error", error);
    g_autofree char *json = serialize(entry);
    g_autofree char *line = g_strconcat(json, "\n", NULL);
    const char *configured = g_getenv("OLLAMA_LOG_FILE");
    const char *path = configured && *configured ? configured : OLLAMA_DEFAULT_LOG_FILE;
    gboolean logged = append_log(path, line);
    const char *configured_error = g_getenv("OLLAMA_ERROR_LOG_FILE");
    const char *error_path = configured_error && *configured_error ? configured_error : OLLAMA_DEFAULT_ERROR_LOG_FILE;
    if (error) {
        append_log(error_path, line);
        if (trace) trace->error_logged = TRUE;
    } else if (!logged) {
        /* Report failure of the full trace in the separate error file without
         * recursing or changing the result of an otherwise valid model call. */
        json_object_set_string_member(entry, "event", "logging_error");
        json_object_set_string_member(entry, "error", _("Could not write the complete log."));
        json_object_set_string_member(entry, "failed_log", path);
        g_autofree char *failure = serialize(entry);
        g_autofree char *failure_line = g_strconcat(failure, "\n", NULL);
        append_log(error_path, failure_line);
    }
}

static JsonObject *request_json(const char *base, const char *path, JsonObject *body,
                                guint timeout_ms, GCancellable *cancel, GError **error)
{
    gint64 started = g_get_monotonic_time();
    g_autofree char *id = g_uuid_string_random();
    const char *method = body ? "POST" : "GET";
    OllamaTrace *trace = g_private_get(&operation_trace);
    if (trace) {
        g_strlcpy(trace->request_id, id, sizeof trace->request_id);
        trace->method = method;
        trace->path = path;
        trace->status = 0;
    }
    g_autofree char *url = g_strconcat(base ? base : OLLAMA_DEFAULT_URL, path, NULL);
    ollama_run_request(path, body);
    g_autofree char *json = body ? serialize(body) : NULL;
    g_autoptr(GBytes) sent = json ? g_bytes_new(json, strlen(json)) : NULL;
    g_autoptr(SoupMessage) message = soup_message_new(method, url);
    if (message && sent)
        soup_message_set_request_body_from_bytes(message, "application/json", sent);
    log_exchange(id, "request", method, url, message, sent, started, NULL);
    if (!message) {
        log_exchange(id, "error", method, url, NULL, NULL, started, _("Invalid Ollama address."));
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Invalid Ollama address."));
        return NULL;
    }
    if (cancel && g_cancellable_set_error_if_cancelled(cancel, error)) {
        log_exchange(id, "cancelled", method, url, message, NULL, started, _("Cancelled before sending."));
        return NULL;
    }
    g_autoptr(GMainContext) context = g_main_context_new();
    g_main_context_push_thread_default(context);
    g_autoptr(SoupSession) session = soup_session_new();
    Request request = {0};
    request.loop = g_main_loop_new(context, FALSE);
    request.cancel = g_cancellable_new();
    gulong handler = cancel ? g_cancellable_connect(cancel, G_CALLBACK(cancel_request),
        g_object_ref(request.cancel), g_object_unref) : 0;
    GSource *timer = g_timeout_source_new(MAX(timeout_ms, 1));
    g_source_set_callback(timer, deadline, &request, NULL);
    g_source_attach(timer, context);
    ollama_event_request(cancel,id,path,body);
    soup_session_send_and_read_async(session, message, G_PRIORITY_DEFAULT, request.cancel, received, &request);
    g_main_loop_run(request.loop);
    g_source_destroy(timer);
    g_source_unref(timer);
    if (handler) g_cancellable_disconnect(cancel, handler);
    soup_session_abort(session);
    g_clear_object(&session);
    g_main_context_pop_thread_default(context);
    g_main_loop_unref(request.loop);
    g_object_unref(request.cancel);
    /* Record the body before HTTP-status or JSON validation, including model
     * thinking fields, inline <think> text and service error responses. */
    ollama_event_response(cancel,id,path,body && !json_object_has_member(body,"format"),request.bytes);
    if (request.bytes)
        log_exchange(id, "response", method, url, message, request.bytes, started, NULL);
    if (request.timed_out) {
        log_exchange(id, "timeout", method, url, message, NULL, started,
                     _("Ollama request timed out."));
        g_clear_error(&request.error);
        g_clear_pointer(&request.bytes, g_bytes_unref);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, _("Ollama request timed out."));
        return NULL;
    }
    if (request.error) {
        log_exchange(id, g_error_matches(request.error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ?
            "cancelled" : "error", method, url, message, NULL, started, request.error->message);
        g_clear_pointer(&request.bytes, g_bytes_unref);
        g_propagate_prefixed_error(error, request.error, "Ollama: ");
        return NULL;
    }
    g_autoptr(GBytes) bytes = request.bytes;
    guint status = soup_message_get_status(message);
    if (trace) trace->status = status;
    if (status < 200 || status >= 300) {
        g_autoptr(JsonParser) error_parser = json_parser_new();
        gsize error_size;
        const char *error_data = g_bytes_get_data(bytes, &error_size);
        const char *detail = _("Check the service and selected model.");
        if (json_parser_load_from_data(error_parser, error_data, error_size, NULL)) {
            JsonNode *root = json_parser_get_root(error_parser);
            JsonNode *description = root && JSON_NODE_HOLDS_OBJECT(root) ?
                json_object_get_member(json_node_get_object(root), "error") : NULL;
            if (description && json_node_get_value_type(description) == G_TYPE_STRING)
                detail = json_node_get_string(description);
        }
        g_autofree char *failure = g_strdup_printf(_("Ollama returned HTTP %u (%s): %s"),
            status, soup_message_get_reason_phrase(message), detail);
        log_exchange(id, "http_error", method, url, message, bytes, started, failure);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, failure);
        return NULL;
    }
    g_autoptr(JsonParser) parser = json_parser_new();
    gsize size;
    const char *data = g_bytes_get_data(bytes, &size);
    if (!json_parser_load_from_data(parser, data, size, error))
        return NULL;
    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("The Ollama response is not a JSON object."));
        return NULL;
    }
    ollama_run_response(path, json_node_get_object(root));
    return json_object_ref(json_node_get_object(root));
}

static void log_operation_failure(const char *base, gint64 started, const Game *game,
    const OllamaResult *result, const GError *error)
{
    (void) game; (void) result;
    OllamaTrace *trace = g_private_get(&operation_trace);
    if (trace && trace->error_logged) return;
    log_exchange(trace ? trace->id : "", "operation_error", "POST",
        base ? base : OLLAMA_DEFAULT_URL, NULL, NULL, started, error ? error->message : "Ollama failure");
}

static const char *string_member(JsonObject *object, const char *key)
{
    JsonNode *node = json_object_get_member(object, key);
    return node && json_node_get_value_type(node) == G_TYPE_STRING ? json_node_get_string(node) : NULL;
}

static gint compare_names(gconstpointer a, gconstpointer b)
{
    return g_strcmp0(*(char * const *) a, *(char * const *) b);
}

static char **models_from_object(JsonObject *object, GError **error)
{
    JsonNode *node = json_object_get_member(object, "models");
    if (!node || !JSON_NODE_HOLDS_ARRAY(node)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("Invalid Ollama model list."));
        return NULL;
    }
    g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func(g_free);
    JsonArray *models = json_node_get_array(node);
    for (unsigned i = 0; i < json_array_get_length(models); i++) {
        JsonNode *entry = json_array_get_element(models, i);
        const char *name = entry && JSON_NODE_HOLDS_OBJECT(entry) ?
            string_member(json_node_get_object(entry), "name") : NULL;
        if (!name || !*name) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("Invalid Ollama model name."));
            return NULL;
        }
        if (!g_ptr_array_find_with_equal_func(names, name, g_str_equal, NULL))
            g_ptr_array_add(names, g_strdup(name));
    }
    g_ptr_array_sort(names, compare_names);
    g_ptr_array_add(names, NULL);
    return (char **) g_ptr_array_free(g_steal_pointer(&names), FALSE);
}

static JsonObject *parse_object(const char *json, GError **error)
{
    g_autoptr(JsonParser) parser = json_parser_new();
    if (!json_parser_load_from_data(parser, json, -1, error)) return NULL;
    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("The Ollama response is not a JSON object."));
        return NULL;
    }
    return json_object_ref(json_node_get_object(root));
}

char **ollama_parse_models(const char *json, GError **error)
{
    g_autoptr(JsonObject) object = parse_object(json, error);
    return object ? models_from_object(object, error) : NULL;
}

char **ollama_list_models(const char *url, guint timeout_ms, GCancellable *cancel, GError **error)
{
    g_autofree char *id = g_uuid_string_random();
    OllamaTrace trace = {.id = id, .operation = "list_models", .started = g_get_monotonic_time()};
    OllamaTrace *previous = g_private_get(&operation_trace);
    g_private_set(&operation_trace, &trace);
    gint64 started = g_get_monotonic_time();
    GError *failure = NULL;
    g_autoptr(JsonObject) object = request_json(url, "/api/tags", NULL, timeout_ms, cancel, &failure);
    char **models = object ? models_from_object(object, &failure) : NULL;
    if (!models) log_operation_failure(url, started, NULL, NULL, failure);
    g_private_set(&operation_trace, previous);
    if (failure) g_propagate_error(error, failure);
    return models;
}

static gboolean move_from_object(const Game *game, JsonObject *response, int *chosen, GError **error)
{
    *chosen = -1;
    const char *answer = string_member(response, "response");
    JsonNode *done = json_object_get_member(response, "done");
    g_autoptr(JsonParser) parser = json_parser_new();
    if (done && json_node_get_value_type(done) == G_TYPE_BOOLEAN && json_node_get_boolean(done) &&
        answer && json_parser_load_from_data(parser, answer, -1, NULL)) {
        JsonNode *root = json_parser_get_root(parser);
        if (root && JSON_NODE_HOLDS_OBJECT(root)) {
            JsonObject *object = json_node_get_object(root); const Board *board = game_board(game->board);
            int source = game_point(board, string_member(object, "source"));
            int destination = game_point(board, string_member(object, "destination"));
            int move = game_find_move(board, source, destination);
            if (json_object_get_size(object) == 2 && move >= 0 && game_legal(game, move)) { *chosen = move; return TRUE; }
        }
    }
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("The model returned an invalid or illegal move.")); return FALSE;
}
gboolean ollama_parse_move(const Game *game, const char *json, int *move, GError **error)
{
    *move = -1; g_autoptr(JsonObject) object = parse_object(json,error);
    return object && move_from_object(game,object,move,error);
}
static gboolean assistance_cancelled(gpointer data) { return data && g_cancellable_is_cancelled(data); }
gboolean ollama_analyze(const Game *game, const OllamaOptions *options, GCancellable *cancel, OllamaAnalysis *analysis, GError **error)
{
    *analysis = (OllamaAnalysis) {0};
    if (cancel && g_cancellable_set_error_if_cancelled(cancel,error)) return FALSE;
    if (!options->assisted) { analysis->allowed = game_legal_moves(game); return TRUE; }
    SearchControl control = {.cancelled = assistance_cancelled, .data = cancel};
    analysis->search = search_certify(game,&options->limits,&control,0,&analysis->allowed);
    if (analysis->search.status == SEARCH_CANCELLED) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_CANCELLED,"Cancelled during search assistance"); return FALSE; }
    if (analysis->search.status == SEARCH_ERROR) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,_("Search allocation failed.")); return FALSE; }
    analysis->partial = analysis->search.status == SEARCH_LIMIT;
    if (analysis->allowed.count) return TRUE;
    g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,analysis->partial ? _("Search limit reached; no jump certified.") : _("No winning continuation exists."));
    return FALSE;
}
char *ollama_build_prompt(const Game *game, const OllamaAnalysis *analysis, gboolean assisted)
{
    const Board *board = game_board(game->board);
    GString *prompt = g_string_new("You are the single player in Peg Solitaire. Jump an occupied source peg over exactly one adjacent occupied middle peg into an EMPTY destination hole. Remove the jumped middle peg and vacate the source. Each jump removes exactly one peg. No skipped holes or jumps over empty holes.\n");
    g_string_append(prompt,game->board == BOARD_ENGLISH ? "English cross board: horizontal and vertical jumps only; no diagonals.\n" :
        "Triangular board: jumps along the three triangle axes. Rows increase downward; row N has N holes, labeled aN, bN, etc.\n");
    g_string_append_printf(prompt,"Goal: finish with one peg %s. %u pegs remain. Coordinates use column letters and rows increasing downward. Missing coordinates are not holes.\nBoard:\n",
        game->goal == GOAL_CENTER ? "in the center hole d4" : "in any hole",game_count(game->occupied));
    for (unsigned p = 0; p < board->points; p++) g_string_append_printf(prompt,"%s: %s\n",board->labels[p],game->occupied & POINT_BIT(p) ? "occupied" : "empty");
    if (assisted) g_string_append_printf(prompt,"Backtracking assistance: only certified winning jumps are offered. %s\n",analysis->partial ?
        "Resource limit reached; this is a certified subset, not a classification of unresolved jumps." : "All legal jumps were classified.");
    else g_string_append(prompt,"No search assistance is supplied. Find a winning continuation yourself.\n");
    g_string_append(prompt,"Allowed jumps (source -> middle -> destination):\n");
    for (unsigned i = 0; i < analysis->allowed.count; i++) {
        Move m = board->jumps[analysis->allowed.items[i]];
        g_string_append_printf(prompt,"%s -> %s -> %s\n",board->labels[m.source],board->labels[m.middle],board->labels[m.destination]);
    }
    g_string_append(prompt,"Return only {\"source\":\"hole label\",\"destination\":\"hole label\"} for one allowed jump.\n");
    return g_string_free(prompt,FALSE);
}
static JsonObject *move_schema(const Game *game, const MoveList *allowed)
{
    const Board *board = game_board(game->board);
    JsonObject *schema = json_object_new(); json_object_set_string_member(schema,"type","object");
    json_object_set_boolean_member(schema,"additionalProperties",FALSE);
    JsonArray *required = json_array_new(); json_array_add_string_element(required,"source"); json_array_add_string_element(required,"destination");
    json_object_set_array_member(schema,"required",required); JsonObject *properties = json_object_new();
    for (unsigned field = 0; field < 2; field++) {
        JsonObject *property = json_object_new(); json_object_set_string_member(property,"type","string");
        JsonArray *values = json_array_new(); guint64 points = 0;
        for (unsigned i = 0; i < allowed->count; i++) { Move m = board->jumps[allowed->items[i]]; points |= POINT_BIT(field ? m.destination : m.source); }
        for (unsigned p = 0; p < board->points; p++) if (points & POINT_BIT(p)) json_array_add_string_element(values,board->labels[p]);
        json_object_set_array_member(property,"enum",values); json_object_set_object_member(properties,field ? "destination" : "source",property);
    }
    json_object_set_object_member(schema,"properties",properties); return schema;
}
static gboolean cogito_v1(const char *model)
{
    const char *name = strrchr(model, '/');
    name = name ? name + 1 : model;
    return g_str_equal(name, "cogito") || g_str_has_prefix(name, "cogito:");
}

/* Send only advertised controls: true, high, or a supported default. Older
 * servers without this metadata retain the model's own default behavior. */
static JsonNode *thinking_control(JsonObject *info)
{
    JsonNode *node = json_object_get_member(info, "thinking");
    if (!node || !JSON_NODE_HOLDS_OBJECT(node)) return NULL;
    JsonObject *thinking = json_node_get_object(node);
    node = json_object_get_member(thinking, "values");
    if (!node || !JSON_NODE_HOLDS_ARRAY(node)) return NULL;
    JsonArray *values = json_node_get_array(node);
    JsonNode *fallback = json_object_get_member(thinking, "default");
    JsonNode *supported_default = NULL;
    for (unsigned i = 0; i < json_array_get_length(values); i++) {
        JsonNode *value = json_array_get_element(values, i);
        GType type = json_node_get_value_type(value);
        if ((type == G_TYPE_BOOLEAN && json_node_get_boolean(value)) ||
            (type == G_TYPE_STRING && g_str_equal(json_node_get_string(value), "high")))
            return json_node_copy(value);
        if (fallback && type == G_TYPE_STRING && json_node_get_value_type(fallback) == G_TYPE_STRING &&
            g_str_equal(json_node_get_string(value), json_node_get_string(fallback)))
            supported_default = value;
    }
    return supported_default ? json_node_copy(supported_default) : NULL;
}

static guint remaining_time(gint64 start, guint total_ms, GError **error)
{
    gint64 left = (gint64) total_ms * 1000 - (g_get_monotonic_time() - start);
    if (left <= 0) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT, _("The Ollama turn exceeded its total time limit."));
        return 0;
    }
    return (guint) MAX(1, left / 1000);
}

static gboolean complete_response(JsonObject *object)
{
    JsonNode *done = json_object_get_member(object, "done");
    return done && json_node_get_value_type(done) == G_TYPE_BOOLEAN && json_node_get_boolean(done);
}

static gboolean choose_turn(const Game *game, const char *model,
                            const char *url, guint timeout_ms,
                            GCancellable *cancel, const OllamaOptions *options,
                            OllamaResult *result, gint64 started,
                            GError **error) {
  if (!model || !*model || game_result(game) != GAME_ONGOING) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        _("Model missing or game finished."));
    return FALSE;
  }
  OllamaAnalysis analysis;
  if (!ollama_analyze(game, options, cancel, &analysis, error))
    return FALSE;
  result->allowed = analysis.allowed;
  result->partial = analysis.partial;
  result->assistance = analysis.search;
  g_autofree char *prompt =
      ollama_build_prompt(game, &analysis, options->assisted);
  g_autoptr(JsonObject) schema = move_schema(game, &analysis.allowed);
  g_autofree char *schema_text = serialize(schema);
  g_autoptr(JsonObject) body = json_object_new();
  json_object_set_string_member(body, "model", model);
  json_object_set_boolean_member(body, "stream", FALSE);
  g_autoptr(JsonObject) response = NULL;
  if (options->harness) {
    int reasoning = 0;
    response = ollama_harness_run(url, model, prompt, schema, options->thinking,
                                  started, timeout_ms, cancel, request_json,
                                  &reasoning, error);
    result->reasoning = (OllamaReasoning)reasoning;
  } else {
    guint timeout;
    if (options->thinking && cogito_v1(model)) {
      /* Legacy Cogito emits inline thinking. Let that stage generate freely;
       * a second, ordinary call produces the schema-constrained decision. */
      result->reasoning = OLLAMA_REASONING_COGITO;
      json_object_set_string_member(body, "system",
                                    "Enable deep thinking subroutine.");
      g_autofree char *instruction =
          g_strconcat(prompt,
                      "\nThis is a separate analysis stage; do not apply "
                      "final-answer formatting yet. Analyze the position and "
                      "recommend an allowed move using explanatory text.",
                      NULL);
      json_object_set_string_member(body, "prompt", instruction);
      timeout = remaining_time(started, timeout_ms, error);
      if (!timeout)
        return FALSE;
      g_autoptr(JsonObject) reasoning =
          request_json(url, "/api/generate", body, timeout, cancel, error);
      if (!reasoning)
        return FALSE;
      const char *text = string_member(reasoning, "response");
      if (!complete_response(reasoning) || !text || !*text) {
        g_set_error_literal(
            error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
            _("The model did not complete the position analysis."));
        return FALSE;
      }
      g_autofree char *combined =
          g_strdup_printf("%s\nPrevious analysis from this model:\n%s\n"
                          "Recheck its recommendation against the board and "
                          "allowed choices. Return only the final JSON move.\n"
                          "Response JSON schema: %s",
                          prompt, text, schema_text);
      json_object_set_string_member(body, "prompt", combined);
      json_object_set_string_member(body, "system",
                                    "Select the final legal move and output "
                                    "only the requested JSON object.");
    } else {
      if (options->thinking) {
        g_autoptr(JsonObject) query = json_object_new();
        json_object_set_string_member(query, "model", model);
        timeout = remaining_time(started, timeout_ms, error);
        if (!timeout)
          return FALSE;
        g_autoptr(JsonObject) info =
            request_json(url, "/api/show", query, timeout, cancel, error);
        if (!info)
          return FALSE;
        JsonNode *control = thinking_control(info);
        if (control) {
          json_object_set_member(body, "think", control);
          result->reasoning = OLLAMA_REASONING_NATIVE;
        }
      }
      g_autofree char *instruction =
          g_strdup_printf("%sResponse JSON schema: %s", prompt, schema_text);
      json_object_set_string_member(body, "prompt", instruction);
    }
    json_object_set_object_member(body, "format", json_object_ref(schema));
    timeout = remaining_time(started, timeout_ms, error);
    if (!timeout)
      return FALSE;
    response = request_json(url, "/api/generate", body, timeout, cancel, error);
  }

  if (!response || !move_from_object(game, response, &result->move, error))
    return FALSE;
  gboolean permitted = FALSE;
  for (unsigned i = 0; i < analysis.allowed.count; i++)
    permitted |= analysis.allowed.items[i] == (unsigned)result->move;
  if (!permitted) {
    result->move = -1;
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        _("The model chose a move outside the choices "
                          "permitted by tactical assistance."));
    return FALSE;
  }
  return TRUE;
}

gboolean ollama_choose_with_options(const Game *game, const char *model, const char *url,
    guint timeout_ms, GCancellable *cancel, const OllamaOptions *options, OllamaResult *result, GError **error)
{
    OllamaOptions pure_options;
    if (options && options->harness) { pure_options=*options; pure_options.assisted=FALSE; options=&pure_options; }

    OllamaOptions defaults = {.limits = search_defaults()};
    if (!options) options = &defaults;
    *result = (OllamaResult) {.move = -1, .assisted = options->assisted,
        .thinking_requested = options->thinking};
    g_auto(OllamaRun) run = {0};
    ollama_run_begin(&run, &result->run, options->harness);
    gint64 started = g_get_monotonic_time();
    g_autofree char *id = g_uuid_string_random();
    OllamaTrace trace = {.id = id, .operation = "choose_move", .model = model, .started = started,
        .assisted = options->assisted, .thinking = options->thinking};
    OllamaTrace *previous = g_private_get(&operation_trace);
    g_private_set(&operation_trace, &trace);
    GError *failure = NULL;
    gboolean ok = choose_turn(game, model, url, timeout_ms, cancel, options, result, started, &failure);
    result->elapsed_us = g_get_monotonic_time() - started;
    if (!ok) log_operation_failure(url, started, game, result, failure);
    g_private_set(&operation_trace, previous);
    if(!ok)ollama_event_error(cancel,failure);

    if (failure) g_propagate_error(error, failure);
    return ok;
}

void ollama_record(GString *history, const char *model, const OllamaResult *result)
{
    ollama_run_record(history, &result->run);
    g_autofree char *safe = g_strescape(model ? model : "",NULL);
    g_string_append_printf(history,"Ollama model: %s; mode: %s; thinking: %s; reasoning: %u; elapsed: %.3f ms; certified choices: %u; partial certification: %s.\n\n",
        safe,result->assisted ? "backtracking-assisted" : "pure",result->thinking_requested ? "on" : "off",result->reasoning,result->elapsed_us/1000.0,
        result->allowed.count,result->partial ? "yes" : "no");
    if (result->assisted) g_string_append_printf(history,"Assistance: %s; nodes: %" G_GUINT64_FORMAT "; time: %.3f ms; peak solver memory: %zu bytes.\n\n",
        search_status_name(result->assistance.status),result->assistance.nodes,result->assistance.elapsed_us/1000.0,result->assistance.peak_memory);
}
