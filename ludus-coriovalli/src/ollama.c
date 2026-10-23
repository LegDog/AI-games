// SPDX-License-Identifier: GPL-3.0-or-later
#include "ollama.h"
#include "i18n.h"
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <string.h>
#include <unistd.h>

/* A private context makes the total deadline cover connection, model loading
 * and generation, rather than just inactivity on a socket. */
typedef struct {
  GMainLoop *loop;
  GCancellable *cancel;
  GBytes *bytes;
  GError *error;
  gboolean timed_out;
} Request;

static void cancel_request(GCancellable *parent, gpointer data) {
  (void)parent;
  g_cancellable_cancel(data);
}

static gboolean deadline(gpointer data) {
  Request *request = data;
  request->timed_out = TRUE;
  g_cancellable_cancel(request->cancel);
  return G_SOURCE_REMOVE;
}

static void received(GObject *source, GAsyncResult *result, gpointer data) {
  Request *request = data;
  request->bytes = soup_session_send_and_read_finish(SOUP_SESSION(source),
                                                     result, &request->error);
  g_main_loop_quit(request->loop);
}

static char *serialize(JsonObject *object) {
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
 * Save/restore at public API boundaries; no pointers outlive their operation.
 */
static GPrivate operation_trace = G_PRIVATE_INIT(NULL);

static gboolean append_log(const char *path, const char *line) {
  /* The report writer requires regular files; explicitly support discarding
   * logs without opening or locking the shared /dev/null device. */
  if (g_str_equal(path, "/dev/null"))
    return TRUE;
  GError *error = NULL;
  if (game_append_report(path, line, &error))
    return TRUE;
  g_printerr(_("Could not write Ollama log: %s\n"), error->message);
  g_clear_error(&error);
  return FALSE;
}

/* JSON Lines retain the exact payload text, including thinking and malformed
 * error bodies. The existing append helper locks across processes and rolls
 * back partial writes, so both games may safely share the same log. */
static void log_exchange(const char *id, const char *event, const char *method,
                         const char *url, SoupMessage *message, GBytes *body,
                         gint64 started, const char *error) {
  g_autoptr(JsonObject) entry = json_object_new();
  ollama_run_annotate(entry);
  g_autoptr(GDateTime) now = g_date_time_new_now_local();
  g_autofree char *timestamp =
      g_date_time_format(now, "%Y-%m-%dT%H:%M:%S.%f%:z");
  json_object_set_string_member(entry, "timestamp", timestamp);
  json_object_set_string_member(entry, "game", GAME_ID);
  json_object_set_int_member(entry, "pid", getpid());
  json_object_set_string_member(entry, "id", id);
  OllamaTrace *trace = g_private_get(&operation_trace);
  if (trace) {
    json_object_set_string_member(entry, "operation_id", trace->id);
    json_object_set_string_member(entry, "operation", trace->operation);
    json_object_set_int_member(entry, "operation_elapsed_us",
                               g_get_monotonic_time() - trace->started);
    if (trace->model)
      json_object_set_string_member(entry, "model", trace->model);
    json_object_set_boolean_member(entry, "assisted", trace->assisted);
    json_object_set_boolean_member(entry, "thinking_requested",
                                   trace->thinking);
    if (*trace->request_id)
      json_object_set_string_member(entry, "request_id", trace->request_id);
    if (!message && trace->status)
      json_object_set_int_member(entry, "status", trace->status);
  }
  json_object_set_string_member(entry, "event", event);
  json_object_set_string_member(entry, "method", method);
  json_object_set_string_member(entry, "url", url);
  json_object_set_int_member(entry, "elapsed_us",
                             g_get_monotonic_time() - started);
  if (message) {
    gboolean outgoing = g_str_equal(event, "request");
    if (!outgoing)
      json_object_set_int_member(entry, "status",
                                 soup_message_get_status(message));
    SoupMessageHeaders *headers =
        outgoing ? soup_message_get_request_headers(message)
                 : soup_message_get_response_headers(message);
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
      g_autofree char *encoded = g_base64_encode((const guchar *)data, size);
      json_object_set_string_member(entry, "body_base64", encoded);
    }
  }
  if (error)
    json_object_set_string_member(entry, "error", error);
  g_autofree char *json = serialize(entry);
  g_autofree char *line = g_strconcat(json, "\n", NULL);
  const char *configured = g_getenv("OLLAMA_LOG_FILE");
  const char *path =
      configured && *configured ? configured : OLLAMA_DEFAULT_LOG_FILE;
  gboolean logged = append_log(path, line);
  const char *configured_error = g_getenv("OLLAMA_ERROR_LOG_FILE");
  const char *error_path = configured_error && *configured_error
                               ? configured_error
                               : OLLAMA_DEFAULT_ERROR_LOG_FILE;
  if (error) {
    append_log(error_path, line);
    if (trace)
      trace->error_logged = TRUE;
  } else if (!logged) {
    /* Report failure of the full trace in the separate error file without
     * recursing or changing the result of an otherwise valid model call. */
    json_object_set_string_member(entry, "event", "logging_error");
    json_object_set_string_member(entry, "error",
                                  _("Could not write the complete log."));
    json_object_set_string_member(entry, "failed_log", path);
    g_autofree char *failure = serialize(entry);
    g_autofree char *failure_line = g_strconcat(failure, "\n", NULL);
    append_log(error_path, failure_line);
  }
}

static JsonObject *request_json(const char *base, const char *path,
                                JsonObject *body, guint timeout_ms,
                                GCancellable *cancel, GError **error) {
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
  g_autofree char *url =
      g_strconcat(base ? base : OLLAMA_DEFAULT_URL, path, NULL);
  ollama_run_request(path, body);
  g_autofree char *json = body ? serialize(body) : NULL;
  g_autoptr(GBytes) sent = json ? g_bytes_new(json, strlen(json)) : NULL;
  g_autoptr(SoupMessage) message = soup_message_new(method, url);
  if (message && sent)
    soup_message_set_request_body_from_bytes(message, "application/json", sent);
  log_exchange(id, "request", method, url, message, sent, started, NULL);
  if (!message) {
    log_exchange(id, "error", method, url, NULL, NULL, started,
                 _("Invalid Ollama address."));
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        _("Invalid Ollama address."));
    return NULL;
  }
  if (cancel && g_cancellable_set_error_if_cancelled(cancel, error)) {
    log_exchange(id, "cancelled", method, url, message, NULL, started,
                 _("Cancelled before sending."));
    return NULL;
  }
  g_autoptr(GMainContext) context = g_main_context_new();
  g_main_context_push_thread_default(context);
  g_autoptr(SoupSession) session = soup_session_new();
  Request request = {0};
  request.loop = g_main_loop_new(context, FALSE);
  request.cancel = g_cancellable_new();
  gulong handler =
      cancel
          ? g_cancellable_connect(cancel, G_CALLBACK(cancel_request),
                                  g_object_ref(request.cancel), g_object_unref)
          : 0;
  GSource *timer = g_timeout_source_new(MAX(timeout_ms, 1));
  g_source_set_callback(timer, deadline, &request, NULL);
  g_source_attach(timer, context);
  ollama_event_request(cancel, id, path, body);
  soup_session_send_and_read_async(session, message, G_PRIORITY_DEFAULT,
                                   request.cancel, received, &request);
  g_main_loop_run(request.loop);
  g_source_destroy(timer);
  g_source_unref(timer);
  if (handler)
    g_cancellable_disconnect(cancel, handler);
  soup_session_abort(session);
  g_clear_object(&session);
  g_main_context_pop_thread_default(context);
  g_main_loop_unref(request.loop);
  g_object_unref(request.cancel);
  /* Record the body before HTTP-status or JSON validation, including model
   * thinking fields, inline <think> text and service error responses. */
  ollama_event_response(cancel, id, path,
                        body && !json_object_has_member(body, "format"),
                        request.bytes);
  if (request.bytes)
    log_exchange(id, "response", method, url, message, request.bytes, started,
                 NULL);
  if (request.timed_out) {
    log_exchange(id, "timeout", method, url, message, NULL, started,
                 _("Ollama request timed out."));
    g_clear_error(&request.error);
    g_clear_pointer(&request.bytes, g_bytes_unref);
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                        _("Ollama request timed out."));
    return NULL;
  }
  if (request.error) {
    log_exchange(
        id,
        g_error_matches(request.error, G_IO_ERROR, G_IO_ERROR_CANCELLED)
            ? "cancelled"
            : "error",
        method, url, message, NULL, started, request.error->message);
    g_clear_pointer(&request.bytes, g_bytes_unref);
    g_propagate_prefixed_error(error, request.error, "Ollama: ");
    return NULL;
  }
  g_autoptr(GBytes) bytes = request.bytes;
  guint status = soup_message_get_status(message);
  if (trace)
    trace->status = status;
  if (status < 200 || status >= 300) {
    g_autoptr(JsonParser) error_parser = json_parser_new();
    gsize error_size;
    const char *error_data = g_bytes_get_data(bytes, &error_size);
    const char *detail = _("Check the service and selected model.");
    if (json_parser_load_from_data(error_parser, error_data, error_size,
                                   NULL)) {
      JsonNode *root = json_parser_get_root(error_parser);
      JsonNode *description =
          root && JSON_NODE_HOLDS_OBJECT(root)
              ? json_object_get_member(json_node_get_object(root), "error")
              : NULL;
      if (description && json_node_get_value_type(description) == G_TYPE_STRING)
        detail = json_node_get_string(description);
    }
    g_autofree char *failure =
        g_strdup_printf(_("Ollama returned HTTP %u (%s): %s"), status,
                        soup_message_get_reason_phrase(message), detail);
    log_exchange(id, "http_error", method, url, message, bytes, started,
                 failure);
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
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        _("The Ollama response is not a JSON object."));
    return NULL;
  }
  ollama_run_response(path, json_node_get_object(root));
  return json_object_ref(json_node_get_object(root));
}

static void log_operation_failure(const char *base, gint64 started,
                                  const Game *game, const OllamaResult *result,
                                  const GError *error) {
  (void)game;
  (void)result;
  OllamaTrace *trace = g_private_get(&operation_trace);
  if (trace && trace->error_logged)
    return;
  log_exchange(trace ? trace->id : "", "operation_error", "POST",
               base ? base : OLLAMA_DEFAULT_URL, NULL, NULL, started,
               error ? error->message : "Ollama failure");
}

static const char *string_member(JsonObject *object, const char *key) {
  JsonNode *node = json_object_get_member(object, key);
  return node && json_node_get_value_type(node) == G_TYPE_STRING
             ? json_node_get_string(node)
             : NULL;
}

static gint compare_names(gconstpointer a, gconstpointer b) {
  return g_strcmp0(*(char *const *)a, *(char *const *)b);
}

static char **models_from_object(JsonObject *object, GError **error) {
  JsonNode *node = json_object_get_member(object, "models");
  if (!node || !JSON_NODE_HOLDS_ARRAY(node)) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        _("Invalid Ollama model list."));
    return NULL;
  }
  g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func(g_free);
  JsonArray *models = json_node_get_array(node);
  for (unsigned i = 0; i < json_array_get_length(models); i++) {
    JsonNode *entry = json_array_get_element(models, i);
    const char *name = entry && JSON_NODE_HOLDS_OBJECT(entry)
                           ? string_member(json_node_get_object(entry), "name")
                           : NULL;
    if (!name || !*name) {
      g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                          _("Invalid Ollama model name."));
      return NULL;
    }
    if (!g_ptr_array_find_with_equal_func(names, name, g_str_equal, NULL))
      g_ptr_array_add(names, g_strdup(name));
  }
  g_ptr_array_sort(names, compare_names);
  g_ptr_array_add(names, NULL);
  return (char **)g_ptr_array_free(g_steal_pointer(&names), FALSE);
}

static JsonObject *parse_object(const char *json, GError **error) {
  g_autoptr(JsonParser) parser = json_parser_new();
  if (!json_parser_load_from_data(parser, json, -1, error))
    return NULL;
  JsonNode *root = json_parser_get_root(parser);
  if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        _("The Ollama response is not a JSON object."));
    return NULL;
  }
  return json_object_ref(json_node_get_object(root));
}

char **ollama_parse_models(const char *json, GError **error) {
  g_autoptr(JsonObject) object = parse_object(json, error);
  return object ? models_from_object(object, error) : NULL;
}

char **ollama_list_models(const char *url, guint timeout_ms,
                          GCancellable *cancel, GError **error) {
  g_autofree char *id = g_uuid_string_random();
  OllamaTrace trace = {
      .id = id, .operation = "list_models", .started = g_get_monotonic_time()};
  OllamaTrace *previous = g_private_get(&operation_trace);
  g_private_set(&operation_trace, &trace);
  gint64 started = g_get_monotonic_time();
  GError *failure = NULL;
  g_autoptr(JsonObject) object =
      request_json(url, "/api/tags", NULL, timeout_ms, cancel, &failure);
  char **models = object ? models_from_object(object, &failure) : NULL;
  if (!models)
    log_operation_failure(url, started, NULL, NULL, failure);
  g_private_set(&operation_trace, previous);
  if (failure)
    g_propagate_error(error, failure);
  return models;
}

static gboolean move_from_object(const Game *game, JsonObject *response,
                                 GameMove *chosen, GError **error) {
  *chosen = (GameMove){-1, -1};
  const char *answer = string_member(response, "response");
  JsonNode *done = json_object_get_member(response, "done");
  g_autoptr(JsonParser) parser = json_parser_new();
  if (done && json_node_get_value_type(done) == G_TYPE_BOOLEAN &&
      json_node_get_boolean(done) && answer &&
      json_parser_load_from_data(parser, answer, -1, NULL)) {
    JsonNode *root = json_parser_get_root(parser);
    if (root && JSON_NODE_HOLDS_OBJECT(root)) {
      JsonObject *object = json_node_get_object(root);
      GameMove move;
      if (json_object_get_size(object) == 1 &&
          game_parse_move(string_member(object, "move"), &move) &&
          game_move_legal(game, move)) {
        *chosen = move;
        return TRUE;
      }
    }
  }
  g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                      _("The model returned an invalid or illegal move."));
  return FALSE;
}
gboolean ollama_parse_move(const Game *game, const char *json, GameMove *move,
                           GError **error) {
  *move = (GameMove){-1, -1};
  g_autoptr(JsonObject) object = parse_object(json, error);
  return object && move_from_object(game, object, move, error);
}
void ollama_analyze(const Game *game, OllamaAnalysis *analysis) {
  game_legal_moves(game, &analysis->allowed);
}
char *ollama_build_prompt(const Game *game, const OllamaAnalysis *analysis) {
  GString *prompt = g_string_new(
      "Play Ludus Coriovalli, candidate Haretavl reconstruction: "
      "four dogs (role 1) and two hares (role 2). Dogs move first, then "
      "alternate. "
      "Move one piece along one listed edge to an empty adjacent vertex. No "
      "captures, "
      "jumps, stacking or directional restrictions. Dogs aim to immobilize "
      "BOTH hares; "
      "hares try to survive as many hare moves as possible. If dogs have no "
      "moves they "
      "must pass. A round ends on blocked hares or the hare-move limit. Two "
      "rounds "
      "exchange roles; longer hare survival wins, equal capped scores draw. "
      "Choose for the CURRENT round. No search assistance.\n");
  g_string_append_printf(
      prompt,
      "Round %u. You are role %d (%s), player %u. Hare moves %u of %u.\n",
      game->round + 1, game->turn, game->turn == 1 ? "dogs" : "hares",
      game_actor(game), game->hare_moves, game->limit);
  for (int p = 0; p < 10; p++) {
    g_string_append_printf(prompt, "%s: owner role %u; neighbors",
                           game_labels[p], game->cells[p]);
    for (int q = 0; q < 10; q++)
      if (game_neighbors[p] & (1u << q))
        g_string_append_printf(prompt, " %s", game_labels[q]);
    g_string_append_c(prompt, '\n');
  }
  g_string_append(prompt, "Allowed moves:\n");
  for (unsigned i = 0; i < analysis->allowed.count; i++) {
    char notation[16];
    game_move_label(analysis->allowed.moves[i], notation);
    g_string_append_printf(prompt, "%s\n", notation);
  }
  g_string_append(prompt, "Return only {\"move\":\"A-B\"}, replacing the value "
                          "with one allowed edge move or pass.\n");
  return g_string_free(prompt, FALSE);
}
static JsonObject *move_schema(const GameMoves *allowed) {
  JsonObject *schema = json_object_new();
  json_object_set_string_member(schema, "type", "object");
  json_object_set_boolean_member(schema, "additionalProperties", FALSE);
  JsonArray *required = json_array_new();
  json_array_add_string_element(required, "move");
  json_object_set_array_member(schema, "required", required);
  JsonObject *properties = json_object_new(), *property = json_object_new();
  json_object_set_string_member(property, "type", "string");
  JsonArray *values = json_array_new();
  for (unsigned i = 0; i < allowed->count; i++) {
    char notation[16];
    game_move_label(allowed->moves[i], notation);
    json_array_add_string_element(values, notation);
  }
  json_object_set_array_member(property, "enum", values);
  json_object_set_object_member(properties, "move", property);
  json_object_set_object_member(schema, "properties", properties);
  return schema;
}
static gboolean cogito_v1(const char *model) {
  const char *name = strrchr(model, '/');
  name = name ? name + 1 : model;
  return g_str_equal(name, "cogito") || g_str_has_prefix(name, "cogito:");
}

/* Send only advertised controls: true, high, or a supported default. Older
 * servers without this metadata retain the model's own default behavior. */
static JsonNode *thinking_control(JsonObject *info) {
  JsonNode *node = json_object_get_member(info, "thinking");
  if (!node || !JSON_NODE_HOLDS_OBJECT(node))
    return NULL;
  JsonObject *thinking = json_node_get_object(node);
  node = json_object_get_member(thinking, "values");
  if (!node || !JSON_NODE_HOLDS_ARRAY(node))
    return NULL;
  JsonArray *values = json_node_get_array(node);
  JsonNode *fallback = json_object_get_member(thinking, "default");
  JsonNode *supported_default = NULL;
  for (unsigned i = 0; i < json_array_get_length(values); i++) {
    JsonNode *value = json_array_get_element(values, i);
    GType type = json_node_get_value_type(value);
    if ((type == G_TYPE_BOOLEAN && json_node_get_boolean(value)) ||
        (type == G_TYPE_STRING &&
         g_str_equal(json_node_get_string(value), "high")))
      return json_node_copy(value);
    if (fallback && type == G_TYPE_STRING &&
        json_node_get_value_type(fallback) == G_TYPE_STRING &&
        g_str_equal(json_node_get_string(value),
                    json_node_get_string(fallback)))
      supported_default = value;
  }
  return supported_default ? json_node_copy(supported_default) : NULL;
}

static guint remaining_time(gint64 start, guint total_ms, GError **error) {
  gint64 left = (gint64)total_ms * 1000 - (g_get_monotonic_time() - start);
  if (left <= 0) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                        _("The Ollama turn exceeded its total time limit."));
    return 0;
  }
  return (guint)MAX(1, left / 1000);
}

static gboolean complete_response(JsonObject *object) {
  JsonNode *done = json_object_get_member(object, "done");
  return done && json_node_get_value_type(done) == G_TYPE_BOOLEAN &&
         json_node_get_boolean(done);
}

static gboolean choose_turn(const Game *game, const char *model,
                            const char *url, guint timeout_ms,
                            GCancellable *cancel, const OllamaOptions *options,
                            OllamaResult *result, gint64 started,
                            GError **error) {
  if (!model || !*model || game->round_over) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                        _("Model missing or game finished."));
    return FALSE;
  }
  OllamaAnalysis analysis;
  if (cancel && g_cancellable_set_error_if_cancelled(cancel, error))
    return FALSE;
  ollama_analyze(game, &analysis);
  result->allowed = analysis.allowed.count;
  g_autofree char *prompt = ollama_build_prompt(game, &analysis);
  g_autoptr(JsonObject) schema = move_schema(&analysis.allowed);
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
      json_object_set_string_member(
          body, "system",
          "Select the final legal move and output only "
          "the requested JSON object.");
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
  if (!game_move_legal(game, result->move)) {
    result->move = (GameMove){-1, -1};
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        _("The model returned an invalid or illegal move."));
    return FALSE;
  }
  return TRUE;
}

gboolean ollama_choose_with_options(const Game *game, const char *model,
                                    const char *url, guint timeout_ms,
                                    GCancellable *cancel,
                                    const OllamaOptions *options,
                                    OllamaResult *result, GError **error) {
  OllamaOptions defaults = {0};
  if (!options)
    options = &defaults;
  *result =
      (OllamaResult){.move = {-1, -1}, .thinking_requested = options->thinking};
  g_auto(OllamaRun) run = {0};
  ollama_run_begin(&run, &result->run, options->harness);
  gint64 started = g_get_monotonic_time();
  g_autofree char *id = g_uuid_string_random();
  OllamaTrace trace = {.id = id,
                       .operation = "choose_move",
                       .model = model,
                       .started = started,
                       .thinking = options->thinking};
  OllamaTrace *previous = g_private_get(&operation_trace);
  g_private_set(&operation_trace, &trace);
  GError *failure = NULL;
  gboolean ok = choose_turn(game, model, url, timeout_ms, cancel, options,
                            result, started, &failure);
  result->elapsed_us = g_get_monotonic_time() - started;
  if (!ok)
    log_operation_failure(url, started, game, result, failure);
  g_private_set(&operation_trace, previous);
  if (!ok)
    ollama_event_error(cancel, failure);

  if (failure)
    g_propagate_error(error, failure);
  return ok;
}

void ollama_record(GString *history, const char *model,
                   const OllamaResult *result) {
  ollama_run_record(history, &result->run);
  g_autofree char *safe = g_strescape(model ? model : "", NULL);
  g_string_append_printf(
      history,
      "Ollama model: %s; pure mode; thinking: %s; reasoning: %u; elapsed: %.3f "
      "ms; legal turns: %u.\n\n",
      safe, result->thinking_requested ? "yes" : "no", result->reasoning,
      result->elapsed_us / 1000., result->allowed);
}
