#include "ollama.h"
#include "i18n.h"
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
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    g_autofree char *timestamp = g_date_time_format(now, "%Y-%m-%dT%H:%M:%S.%f%:z");
    json_object_set_string_member(entry, "timestamp", timestamp);
    json_object_set_string_member(entry, "game", GAME_CHOICES == 7 ? "connect-4" : "jogo-da-velha");
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
    const char *path = configured && *configured ? configured : "ollama.log";
    gboolean logged = append_log(path, line);
    const char *configured_error = g_getenv("OLLAMA_ERROR_LOG_FILE");
    const char *error_path = configured_error && *configured_error ? configured_error : "ollama-error.log";
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
    return json_object_ref(json_node_get_object(root));
}

static void log_operation_failure(const char *base, gint64 started, const Game *game,
                                  const OllamaResult *result, const GError *error)
{
    OllamaTrace *trace = g_private_get(&operation_trace);
    if (!trace || trace->error_logged) return;
    g_autoptr(JsonObject) context = json_object_new();
    json_object_set_string_member(context, "error_domain", g_quark_to_string(error->domain));
    json_object_set_int_member(context, "error_code", error->code);
    if (game) {
        json_object_set_int_member(context, "turn", game->turn);
        json_object_set_int_member(context, "moves", game->moves);
        JsonArray *board = json_array_new();
        for (unsigned row = 0; row < GAME_ROWS; row++) {
            JsonArray *cells = json_array_new();
            for (unsigned col = 0; col < GAME_COLS; col++)
                json_array_add_int_element(cells, game->cells[row][col]);
            json_array_add_array_element(board, cells);
        }
        json_object_set_array_member(context, "board", board);
    }
    if (result) {
        JsonArray *allowed = json_array_new();
        for (unsigned i = 0; i < GAME_CHOICES; i++)
            if (result->allowed & (1u << i)) json_array_add_int_element(allowed, ollama_move_number(i));
        json_object_set_array_member(context, "allowed_moves", allowed);
    }
    g_autofree char *text = serialize(context);
    g_autoptr(GBytes) bytes = g_bytes_new(text, strlen(text));
    g_autofree char *url = g_strconcat(base ? base : OLLAMA_DEFAULT_URL,
        trace->path ? trace->path : game ? "/api/generate" : "/api/tags", NULL);
    const char *event = g_error_matches(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT) ? "timeout" :
        g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ? "cancelled" : "error";
    log_exchange(trace->id, event, trace->method ? trace->method : game ? "POST" : "GET",
                 url, NULL, bytes, started, error->message);
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

unsigned ollama_move_number(unsigned move)
{
    return GAME_CHOICES == 7 ? move + 1 : 7 - (move / 3) * 3 + move % 3;
}

static gboolean legal_move(const Game *game, unsigned move)
{
    Game next = *game;
    return game_play(&next, move);
}

static gboolean move_from_object(const Game *game, JsonObject *response, int *chosen, GError **error)
{
    *chosen = -1;
    const char *answer = string_member(response, "response");
    JsonNode *done = json_object_get_member(response, "done");
    g_autoptr(JsonParser) parser = json_parser_new();
    if (!done || json_node_get_value_type(done) != G_TYPE_BOOLEAN || !json_node_get_boolean(done) ||
        !answer || !json_parser_load_from_data(parser, answer, -1, NULL)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("The model did not return a complete JSON move."));
        return FALSE;
    }
    JsonNode *root = json_parser_get_root(parser);
    JsonObject *choice = root && JSON_NODE_HOLDS_OBJECT(root) ? json_node_get_object(root) : NULL;
    JsonNode *move = choice ? json_object_get_member(choice, "move") : NULL;
    if (choice && json_object_get_size(choice) == 1 && move && json_node_get_value_type(move) == G_TYPE_INT64) {
        gint64 number = json_node_get_int(move);
        for (unsigned i = 0; i < GAME_CHOICES; i++) {
            if (number == ollama_move_number(i) && legal_move(game, i)) {
                *chosen = (int) i;
                return TRUE;
            }
        }
    }
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("The model returned an invalid or illegal move."));
    return FALSE;
}

gboolean ollama_parse_move(const Game *game, const char *json, int *move, GError **error)
{
    *move = -1;
    g_autoptr(JsonObject) object = parse_object(json, error);
    return object && move_from_object(game, object, move, error);
}

/* Exact safety for the small tic-tac-toe state space. This is a Boolean
 * minimax proof, independent of both players' positional scores and depths.
 * One cache is shared by all candidates for a fixed opposing attacker. */
static gboolean ttt_forced_win(const Game *game, int attacker, gint8 *cache)
{
    int result = game_result(game);
    if (result) return result == attacker;
    unsigned key = 0;
    for (unsigned row = 0; row < 3; row++)
        for (unsigned col = 0; col < 3; col++)
            key = key * 3 + (unsigned) (game->cells[row][col] + 1);
    key = key * 2 + (game->turn == 1);
    if (cache[key]) return cache[key] == 2;
    gboolean attackers_turn = game->turn == attacker;
    for (unsigned move = 0; move < 9; move++) {
        Game next = *game;
        if (!game_play(&next, move)) continue;
        gboolean wins = ttt_forced_win(&next, attacker, cache);
        if (wins == attackers_turn) {
            cache[key] = wins ? 2 : 1;
            return wins;
        }
    }
    cache[key] = attackers_turn ? 1 : 2;
    return !attackers_turn;
}

void ollama_analyze(const Game *game, gboolean assisted, OllamaAnalysis *analysis)
{
    memset(analysis, 0, sizeof *analysis);
    g_autofree gint8 *cache = assisted && GAME_CHOICES == 9 ? g_new0(gint8, 39366) : NULL;
    for (unsigned move = 0; move < GAME_CHOICES; move++) {
        analysis->landing_rows[move] = -1;
        Game next = *game;
        if (!game_play(&next, move)) continue;
        unsigned bit = 1u << move;
        analysis->legal |= bit;
        for (unsigned row = 0; row < GAME_ROWS; row++)
            for (unsigned col = 0; col < GAME_COLS; col++)
                if (!game->cells[row][col] && next.cells[row][col])
                    analysis->landing_rows[move] = (int) row;
        if (!assisted) continue;
        if (cache && !ttt_forced_win(&next, -game->turn, cache))
            analysis->non_losing |= bit;
        int result = game_result(&next);
        if (result == game->turn) analysis->winning |= bit;
        if (!result) {
            for (unsigned reply = 0; reply < GAME_CHOICES; reply++) {
                Game response = next;
                if (game_play(&response, reply) && game_result(&response) == -game->turn)
                    analysis->opponent_wins[move] |= 1u << reply;
            }
        }
        if (!analysis->opponent_wins[move]) analysis->safe |= bit;
    }
    analysis->allowed = assisted && analysis->winning ? analysis->winning :
        assisted && analysis->non_losing ? analysis->non_losing :
        assisted && analysis->safe ? analysis->safe : analysis->legal;
}

static void append_moves(GString *text, unsigned mask)
{
    for (unsigned i = 0; i < GAME_CHOICES; i++)
        if (mask & (1u << i)) g_string_append_printf(text, " %u", ollama_move_number(i));
}

char *ollama_build_prompt(const Game *game, const OllamaAnalysis *analysis, gboolean assisted)
{
    GString *prompt = g_string_new(GAME_CHOICES == 7 ?
        "Play Connect Four on a 6-row, 7-column board. Gravity applies: a disc lands on the "
        "lowest empty cell of its column. Four connected pieces horizontally, vertically or "
        "diagonally win. A move is a column number 1 through 7.\n" :
        "Play tic-tac-toe on a 3x3 board. Three aligned pieces horizontally, vertically or "
        "diagonally win. Move numbers use the numeric keypad: top row 7 8 9, middle 4 5 6, "
        "bottom 1 2 3.\n");
    g_string_append_printf(prompt, "You play %d; opponent plays %d.\n"
        "Board markers: Y = YOUR pieces; A = ADVERSARY pieces; . = EMPTY.\n"
        "Rows are numbered from TOP to BOTTOM. Columns from LEFT to RIGHT.\nColumns:",
        game->turn, -game->turn);
    for (unsigned col = 0; col < GAME_COLS; col++) g_string_append_printf(prompt, " %u", col + 1);
    g_string_append_c(prompt, '\n');
    for (unsigned row = 0; row < GAME_ROWS; row++) {
        g_string_append_printf(prompt, "Row %u: ", row + 1);
        for (unsigned col = 0; col < GAME_COLS; col++) {
            int piece = game->cells[row][col];
            g_string_append_printf(prompt, "%c%s", piece == game->turn ? 'Y' : piece ? 'A' : '.',
                col + 1 == GAME_COLS ? "\n" : " ");
        }
    }
    g_string_append(prompt, "Legal moves and placement coordinates:\n");
    for (unsigned move = 0; move < GAME_CHOICES; move++) {
        if (!(analysis->legal & (1u << move))) continue;
        unsigned col = GAME_CHOICES == 7 ? move : move % GAME_COLS;
        g_string_append_printf(prompt, "Move %u -> row %d, column %u.\n",
            ollama_move_number(move), analysis->landing_rows[move] + 1, col + 1);
    }
    if (assisted) {
        g_string_append(prompt, "Tactical assistance is enabled. These are engine-verified immediate facts.\n");
        if (GAME_CHOICES == 9) {
            if (analysis->non_losing) {
                g_string_append(prompt, "Complete defense: moves that preserve a win or draw against perfect opposition:");
                append_moves(prompt, analysis->non_losing);
                g_string_append_c(prompt, '\n');
            } else {
                g_string_append(prompt, "Complete defense: this position is already a forced loss against perfect opposition.\n");
            }
        }
        if (analysis->winning) {
            g_string_append(prompt, "Moves that win immediately:");
            append_moves(prompt, analysis->winning);
            g_string_append_c(prompt, '\n');
        } else {
            for (unsigned move = 0; move < GAME_CHOICES; move++) {
                if (!analysis->opponent_wins[move]) continue;
                g_string_append_printf(prompt, "After move %u, adversary can win immediately with:", ollama_move_number(move));
                append_moves(prompt, analysis->opponent_wins[move]);
                g_string_append_c(prompt, '\n');
            }
            if (!analysis->safe)
                g_string_append(prompt, "Every legal move allows an immediate adversary win; choose among all legal moves.\n");
        }
    }
    g_string_append(prompt, "Allowed choices for this turn:");
    append_moves(prompt, analysis->allowed);
    g_string_append(prompt, ".\nChoose the strongest allowed move. First check whether you can win now. "
        "Then simulate the adversary's next move for every candidate and prevent an immediate loss "
        "when possible. Consider double threats and future winning lines after those checks. "
        "Respect the placement coordinates above. The final answer must be only a JSON object "
        "with one integer field named move.\n");
    return g_string_free(prompt, FALSE);
}

static JsonObject *move_schema(unsigned allowed)
{
    JsonObject *property = json_object_new();
    json_object_set_string_member(property, "type", "integer");
    JsonArray *moves = json_array_new();
    for (unsigned i = 0; i < GAME_CHOICES; i++)
        if (allowed & (1u << i)) json_array_add_int_element(moves, ollama_move_number(i));
    json_object_set_array_member(property, "enum", moves);
    JsonObject *properties = json_object_new();
    json_object_set_object_member(properties, "move", property);
    JsonObject *schema = json_object_new();
    json_object_set_string_member(schema, "type", "object");
    json_object_set_object_member(schema, "properties", properties);
    JsonArray *required = json_array_new();
    json_array_add_string_element(required, "move");
    json_object_set_array_member(schema, "required", required);
    json_object_set_boolean_member(schema, "additionalProperties", FALSE);
    return schema;
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

static gboolean choose_turn(const Game *game, const char *model, const char *url,
    guint timeout_ms, GCancellable *cancel, const OllamaOptions *options, OllamaResult *result,
    gint64 started, GError **error)
{
    if (!model || !*model || game_result(game)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Model missing or game finished."));
        return FALSE;
    }
    OllamaAnalysis analysis;
    ollama_analyze(game, options->assisted, &analysis);
    result->allowed = analysis.allowed;
    g_autofree char *prompt = ollama_build_prompt(game, &analysis, options->assisted);
    g_autoptr(JsonObject) schema = move_schema(analysis.allowed);
    g_autofree char *schema_text = serialize(schema);
    g_autoptr(JsonObject) body = json_object_new();
    json_object_set_string_member(body, "model", model);
    json_object_set_boolean_member(body, "stream", FALSE);
    guint timeout;
    if (options->thinking && cogito_v1(model)) {
        /* Legacy Cogito emits inline thinking. Let that stage generate freely;
         * a second, ordinary call produces the schema-constrained decision. */
        result->reasoning = OLLAMA_REASONING_COGITO;
        json_object_set_string_member(body, "system", "Enable deep thinking subroutine.");
        g_autofree char *instruction = g_strconcat(prompt,
            "\nThis is a separate analysis stage; do not apply final-answer formatting yet. Analyze the position and recommend an allowed move using explanatory text.", NULL);
        json_object_set_string_member(body, "prompt", instruction);
        timeout = remaining_time(started, timeout_ms, error);
        if (!timeout) return FALSE;
        g_autoptr(JsonObject) reasoning = request_json(url, "/api/generate", body, timeout, cancel, error);
        if (!reasoning) return FALSE;
        const char *text = string_member(reasoning, "response");
        if (!complete_response(reasoning) || !text || !*text) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, _("The model did not complete the position analysis."));
            return FALSE;
        }
        g_autofree char *combined = g_strdup_printf("%s\nPrevious analysis from this model:\n%s\n"
            "Recheck its recommendation against the board and allowed choices. Return only the final JSON move.\n"
            "Response JSON schema: %s", prompt, text, schema_text);
        json_object_set_string_member(body, "prompt", combined);
        json_object_set_string_member(body, "system", "Select the final legal move and output only the requested JSON object.");
    } else {
        if (options->thinking) {
            g_autoptr(JsonObject) query = json_object_new();
            json_object_set_string_member(query, "model", model);
            timeout = remaining_time(started, timeout_ms, error);
            if (!timeout) return FALSE;
            g_autoptr(JsonObject) info = request_json(url, "/api/show", query, timeout, cancel, error);
            if (!info) return FALSE;
            JsonNode *control = thinking_control(info);
            if (control) {
                json_object_set_member(body, "think", control);
                result->reasoning = OLLAMA_REASONING_NATIVE;
            }
        }
        g_autofree char *instruction = g_strdup_printf("%sResponse JSON schema: %s", prompt, schema_text);
        json_object_set_string_member(body, "prompt", instruction);
    }
    json_object_set_object_member(body, "format", json_object_ref(schema));
    timeout = remaining_time(started, timeout_ms, error);
    if (!timeout) return FALSE;
    g_autoptr(JsonObject) response = request_json(url, "/api/generate", body, timeout, cancel, error);
    if (!response || !move_from_object(game, response, &result->move, error)) return FALSE;
    if (!(analysis.allowed & (1u << result->move))) {
        result->move = -1;
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
            _("The model chose a move outside the choices permitted by tactical assistance."));
        return FALSE;
    }
    return TRUE;
}

gboolean ollama_choose_with_options(const Game *game, const char *model, const char *url,
    guint timeout_ms, GCancellable *cancel, const OllamaOptions *options, OllamaResult *result, GError **error)
{
    OllamaOptions defaults = {0};
    if (!options) options = &defaults;
    *result = (OllamaResult) {.move = -1, .assisted = options->assisted,
        .thinking_requested = options->thinking,
        .complete_defense = options->assisted && GAME_CHOICES == 9};
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
    if (failure) g_propagate_error(error, failure);
    return ok;
}

gboolean ollama_choose(const Game *game, const char *model, const char *url,
                       guint timeout_ms, GCancellable *cancel, OllamaResult *result, GError **error)
{
    return ollama_choose_with_options(game, model, url, timeout_ms, cancel, NULL, result, error);
}

void ollama_record(GString *history, const char *model, const OllamaResult *result)
{
    g_autofree char *escaped = g_markup_escape_text(model, -1);
    const char *reasoning = result->reasoning == OLLAMA_REASONING_COGITO ? "Cogito em duas etapas" :
        result->reasoning == OLLAMA_REASONING_NATIVE ? "nativo" : "padrão do modelo";
    g_string_append_printf(history, "Ollama — modelo: %s; jogada: %u; duração: %.3f s; modo: %s; "
        "raciocínio: %s (ampliação solicitada: %s).\n\n", escaped, ollama_move_number(result->move),
        result->elapsed_us / 1000000.0, result->complete_defense ? "assistido (defesa completa)" : result->assisted ? "assistido" : "puro", reasoning,
        result->thinking_requested ? "sim" : "não");
    GString *choices = g_string_new("Alternativas permitidas ao Ollama:");
    append_moves(choices, result->allowed);
    g_string_append_printf(history, "%s.\n\n", choices->str);
    g_string_free(choices, TRUE);
}
