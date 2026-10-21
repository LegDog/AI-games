// SPDX-License-Identifier: GPL-3.0-or-later
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


static gboolean action_from_object(JsonObject *response,unsigned count,int *chosen,GError **error)
{
    *chosen=-1;const char *answer=string_member(response,"response");JsonNode *done=json_object_get_member(response,"done");
    g_autoptr(JsonParser) parser=json_parser_new();
    if(done && json_node_get_value_type(done)==G_TYPE_BOOLEAN && json_node_get_boolean(done) && answer && json_parser_load_from_data(parser,answer,-1,NULL)) {
        JsonNode *root=json_parser_get_root(parser);
        if(root && JSON_NODE_HOLDS_OBJECT(root)) {
            JsonObject *object=json_node_get_object(root);JsonNode *id=json_object_get_member(object,"action_id");
            if(json_object_get_size(object)==1 && id && json_node_get_value_type(id)==G_TYPE_INT64) {
                gint64 value=json_node_get_int(id);if(value>=0 && value<count){*chosen=value;return TRUE;}
            }
        }
    }
    g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,_("The model returned an invalid or illegal move."));return FALSE;
}
gboolean ollama_parse_action(const char *json,unsigned count,int *chosen,GError **error)
{
    *chosen=-1;g_autoptr(JsonObject) response=parse_object(json,error);return response && action_from_object(response,count,chosen,error);
}
char *ollama_build_prompt(const Game *g,const GArray *choices)
{
    GString *p=g_string_new("Play the specified Tavli variant. There are 24 physical board points numbered 1..24 in Player 1's view, and 15 pieces per player. No search assistance, evaluations, recommended moves or future dice are supplied. Choose one listed legal action ID; checker choices are prefixes that can complete a legal full turn. Each die is a separate move with a legal intermediate landing. Use the maximum possible number of dice; if only one of either unequal die can be used, use the higher. A completed winning move ends the game immediately.\n");
    g_string_append_printf(p,"Variant: %s. You are Player %d. Active checker player: %d. Phase: %s.\n",variant_name(g->variant),g->actor+1,g->active+1,phase_name(g->phase));
    if(g->variant==RUNNING)g_string_append(p,"Running: start with 15 reserve pieces and an empty board. Player 1 runs counterclockwise, entering upper right and exiting lower right. Player 2 runs clockwise, entering lower right and exiting upper right in the fixed Player 1 view (upper left to lower left from Player 2's seat). Reserve entry with die d lands on the d-th point of your route; enter or advance freely, including advancing the same piece with both dice. Any opposing piece blocks LANDING, but may be passed over. Own pieces stack without limit. No captures, backward movement, or prime restrictions. Doubles give TWO uses, not four. You may exit any piece even while others are in reserve. The last route point needs 1 to exit; overshooting is forbidden. First to remove all 15 wins one point.\n");
    else {
        g_string_append(p,"Doubles give four uses. Bring every active piece into your final six route points before bearing off. Exact bearing off is allowed; overshoot may remove only a furthest remaining piece.\n");
        if(g->variant==FEVGA)g_string_append(p,"Fevga: both players move counterclockwise from opposite starting stacks. Any opposing checker blocks landing; own stacks allowed. No hitting. Move only your first escaped checker until it has passed the opponent's starting point. Never fill all six starting-table points. If the opponent gathers immediately behind your six-point prime, open a gap when the current roll permits.\n");
        else if(g->variant==PLAKOTO)g_string_append(p,"Plakoto: opposite directions, 15 pieces on each head. Landing on an opposing singleton PINS it beneath your pieces; it cannot move until your final pinning checker leaves. A pinned point and opposing stacks of two block landing. No bar captures. Pinning the opponent's last starting checker (mother) wins two points unless your own mother remains threatened at your head; both mothers pinned is a draw.\n");
        else g_string_append(p,"Portes/Backgammon: opposite directions, opposing stacks of two block landing; a singleton is hit onto the bar. Re-enter ALL bar checkers before moving other pieces. Single/gammon wins score 1/2; only Backgammon permits a triple win if the unborne loser has a bar checker or checker in the winner's home table.\n");
    }
    for(int player=0;player<2;player++) {
        g_string_append_printf(p,"Player %d route:",player+1);for(unsigned i=0;i<24;i++)g_string_append_printf(p," %d",game_point_at(g,player,i)+1);
        g_string_append_printf(p,". Reserve %u; bar %u; off %u.\n",g->reserve[player],g->bar[player],g->off[player]);
    }
    g_string_append_printf(p,"Score %u-%u, target %u, session %u. Cube %u; owner %d (0 centered); Crawford %s. Double only before rolling and with cube access, never on opening or Crawford, and never with a dead cube. Take gives cube ownership to responder; drop loses the PREVIOUS stake.\n",g->match.score[0],g->match.score[1],g->match.target,g->match.session,g->cube,g->cube_owner+1,g->crawford?"yes":"no");
    g_string_append(p,"Points: number:Player1_count/Player2_count:pinned_player(0 none)\n");
    for(unsigned q=0;q<24;q++)g_string_append_printf(p,"%u:%u/%u:%d\n",q+1,g->point[q][0],g->point[q][1],g->pinned[q]+1);
    g_string_append_printf(p,"Rolled %u,%u. Remaining dice:",g->roll[0],g->roll[1]);for(unsigned d=1;d<=6;d++)for(unsigned n=0;n<g->remaining[d];n++)g_string_append_printf(p," %u",d);
    g_string_append(p,"\nLegal actions:\n");
    for(unsigned i=0;i<choices->len;i++){char label[64];step_label(g_array_index(choices,Step,i),label,sizeof label);g_string_append_printf(p,"%u: %s\n",i,label);}
    g_string_append(p,"Return only {\"action_id\":0}, replacing 0 with the chosen listed ID.\n");return g_string_free(p,FALSE);
}
static JsonObject *move_schema(unsigned count)
{
    JsonObject *schema=json_object_new(),*properties=json_object_new(),*id=json_object_new();JsonArray *values=json_array_new(),*required=json_array_new();
    json_object_set_string_member(schema,"type","object");json_object_set_boolean_member(schema,"additionalProperties",FALSE);
    json_array_add_string_element(required,"action_id");json_object_set_array_member(schema,"required",required);json_object_set_string_member(id,"type","integer");
    for(unsigned i=0;i<count;i++)json_array_add_int_element(values,i);
    json_object_set_array_member(id,"enum",values);json_object_set_object_member(properties,"action_id",id);json_object_set_object_member(schema,"properties",properties);return schema;
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

static gboolean choose_one(const Game *game, const GArray *choices,
                           const char *model, const char *url, guint timeout_ms,
                           GCancellable *cancel, const OllamaOptions *options,
                           OllamaResult *result, gint64 started,
                           GError **error) {
  g_autofree char *prompt = ollama_build_prompt(game, choices);
  g_autoptr(JsonObject) schema = move_schema(choices->len);
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

  return response &&
         action_from_object(response, choices->len, &result->action_id, error);
}

typedef struct { GCancellable *cancel;gint64 start;guint timeout; } RuleDeadline;
static gboolean rule_deadline(gpointer data)
{
    RuleDeadline *d=data;return !(d->cancel && g_cancellable_is_cancelled(d->cancel)) && g_get_monotonic_time()-d->start<(gint64)d->timeout*1000;
}
gboolean ollama_choose_with_options(const Game *game,const char *model,const char *url,guint timeout_ms,GCancellable *cancel,const OllamaOptions *options,OllamaResult *result,GError **error)
{
    OllamaOptions defaults={0};if(!options)options=&defaults;
    *result=(OllamaResult){.action_id=-1,.thinking_requested=options->thinking,.decision={.checker=game->phase==PLAY}};
    g_auto(OllamaRun) run = {0};
    ollama_run_begin(&run, &result->run, options->harness);
    gint64 started=g_get_monotonic_time();g_autofree char *id=g_uuid_string_random();
    OllamaTrace trace={.id=id,.operation="choose_turn",.model=model,.started=started,.thinking=options->thinking};
    OllamaTrace *previous=g_private_get(&operation_trace);g_private_set(&operation_trace,&trace);GError *failure=NULL;gboolean ok=FALSE;
    if(!model || !*model || game_finished(game) || game->phase==OPENING){g_set_error_literal(&failure,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Model missing or not a decision phase.");goto done;}
    Game draft=*game;RuleDeadline deadline={cancel,started,timeout_ms};RuleControl control={rule_deadline,&deadline,64u*1024*1024,NULL};
    for(unsigned stage=0;stage<=4;stage++) {
        if(cancel && g_cancellable_set_error_if_cancelled(cancel,&failure))goto done;
        if(!remaining_time(started,timeout_ms,&failure))goto done;
        GArray *choices=game_choices(&draft,&control);
        if(!choices){
            if(!(cancel && g_cancellable_set_error_if_cancelled(cancel,&failure)) && remaining_time(started,timeout_ms,&failure))g_set_error_literal(&failure,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Legal-turn generation exceeded its memory limit.");
            goto done;
        }
        if(choices->len==1 && g_array_index(choices,Step,0).kind==ACTION_END){g_array_unref(choices);ok=TRUE;break;}
        result->allowed=choices->len;result->requests++;
        if(!choose_one(&draft,choices,model,url,timeout_ms,cancel,options,result,started,&failure)){g_array_unref(choices);goto done;}
        Step chosen=g_array_index(choices,Step,result->action_id);g_array_unref(choices);
        if(!result->decision.checker){result->decision.action=chosen.kind;ok=TRUE;break;}
        if(result->decision.plan.length==4 || !game_apply_step_generated(&draft,chosen)){g_set_error_literal(&failure,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Illegal assembled turn.");goto done;}
        result->decision.plan.steps[result->decision.plan.length++]=chosen;
        if(game_finished(&draft)){ok=TRUE;break;}
    }
    if(ok && result->decision.checker){Game verify=*game;if(!game_play_plan(&verify,&result->decision.plan)){ok=FALSE;g_set_error_literal(&failure,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Incomplete assembled turn.");}}
done:
    result->elapsed_us=g_get_monotonic_time()-started;
    if(!ok)log_operation_failure(url,started,game,result,failure);
    g_private_set(&operation_trace,previous);
    if(!ok)ollama_event_error(cancel,failure);
    if(failure)g_propagate_error(error,failure);
    return ok;
}
void ollama_record(GString *history,const char *model,const OllamaResult *result)
{
    ollama_run_record(history, &result->run);
    g_autofree char *safe=g_strescape(model?model:"",NULL);
    g_string_append_printf(history,"Ollama model: %s; pure mode; thinking: %s; reasoning: %u; requests: %u; elapsed: %.3f ms.\n\n",safe,result->thinking_requested?"yes":"no",result->reasoning,result->requests,result->elapsed_us/1000.);
}
