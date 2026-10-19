// SPDX-License-Identifier: GPL-3.0-or-later
#include "ollama-events.h"
#include <string.h>
typedef struct {
  OllamaMessageFunc callback;
  gpointer data;
  GDestroyNotify destroy;
} Observer;
static void observer_free(gpointer data) {
  Observer *observer = data;
  if (observer->destroy)
    observer->destroy(observer->data);
  g_free(observer);
}
void ollama_observe(GCancellable *operation, OllamaMessageFunc callback,
                    gpointer data, GDestroyNotify destroy) {
  g_return_if_fail(G_IS_CANCELLABLE(operation));
  Observer *observer = g_new0(Observer, 1);
  *observer = (Observer){callback, data, destroy};
  g_object_set_data_full(G_OBJECT(operation), "ollama-message-observer",
                         observer, observer_free);
}
static Observer *observer_for(GCancellable *operation) {
  return operation
             ? g_object_get_data(G_OBJECT(operation), "ollama-message-observer")
             : NULL;
}
static void emit(Observer *observer, OllamaMessageKind kind, const char *id,
                 const char *text) {
  if (observer && observer->callback && text && *text)
    observer->callback(kind, id ? id : "", text, observer->data);
}
static const char *text_member(JsonObject *object, const char *key) {
  JsonNode *node = json_object_get_member(object, key);
  return node && json_node_get_value_type(node) == G_TYPE_STRING
             ? json_node_get_string(node)
             : NULL;
}
static char *pretty(const char *text) {
  g_autoptr(JsonParser) parser = json_parser_new();
  if (json_parser_load_from_data(parser, text, -1, NULL))
    return json_to_string(json_parser_get_root(parser), TRUE);
  return g_strdup(text);
}
void ollama_event_request(GCancellable *operation, const char *id,
                          const char *path, JsonObject *body) {
  Observer *observer = observer_for(operation);
  if (!observer || !g_str_equal(path, "/api/generate") || !body)
    return;
  g_autoptr(GString) text = g_string_new(NULL);
  const char *system = text_member(body, "system"),
             *prompt = text_member(body, "prompt");
  if (system && *system)
    g_string_append_printf(text, "System\n%s\n\n", system);
  if (prompt)
    g_string_append(text, prompt);
  JsonNode *schema = json_object_get_member(body, "format");
  if (schema) {
    g_autofree char *formatted = json_to_string(schema, TRUE);
    g_string_append_printf(text, "\n\nResponse schema\n%s", formatted);
  }
  emit(observer, OLLAMA_SENT, id, text->str);
}
/* Extract explicit reasoning without interpreting model text as markup. */
static char *split_thinking(Observer *observer, const char *id,
                            const char *answer, const char *native) {
  GString *reply = g_string_new(NULL);
  const char *at = answer;
  while (*at) {
    const char *start = strstr(at, "<think>");
    if (!start) {
      g_string_append(reply, at);
      break;
    }
    g_string_append_len(reply, at, start - at);
    start += strlen("<think>");
    const char *end = strstr(start, "</think>");
    g_autofree char *thinking =
        end ? g_strndup(start, end - start) : g_strdup(start);
    if (g_strcmp0(thinking, native) != 0)
      emit(observer, OLLAMA_THINKING, id, thinking);
    if (!end)
      break;
    at = end + strlen("</think>");
  }
  return g_string_free(reply, FALSE);
}
void ollama_event_response(GCancellable *operation, const char *id,
                           const char *path, gboolean analysis, GBytes *bytes) {
  Observer *observer = observer_for(operation);
  if (!observer || !bytes || !g_str_equal(path, "/api/generate"))
    return;
  gsize size;
  const char *raw = g_bytes_get_data(bytes, &size);
  g_autofree char *valid = g_utf8_make_valid(raw, size);
  g_autoptr(JsonParser) parser = json_parser_new();
  if (!json_parser_load_from_data(parser, valid, -1, NULL) ||
      !JSON_NODE_HOLDS_OBJECT(json_parser_get_root(parser))) {
    emit(observer, OLLAMA_RECEIVED, id, valid);
    return;
  }
  JsonObject *object = json_node_get_object(json_parser_get_root(parser));
  const char *thinking = text_member(object, "thinking"),
             *answer = text_member(object, "response");
  emit(observer, OLLAMA_THINKING, id, thinking);
  if (answer) {
    g_autofree char *reply = split_thinking(observer, id, answer, thinking);
    if (*reply) {
      g_autofree char *formatted = pretty(reply);
      emit(observer, analysis ? OLLAMA_THINKING : OLLAMA_RECEIVED, id,
           formatted);
    }
  } else {
    g_autofree char *formatted =
        json_to_string(json_parser_get_root(parser), TRUE);
    emit(observer, OLLAMA_RECEIVED, id, formatted);
  }
}
void ollama_event_error(GCancellable *operation, const GError *error) {
  if (error)
    emit(observer_for(operation), OLLAMA_FAILURE, "", error->message);
}
