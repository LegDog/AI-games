// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <gio/gio.h>
#include <json-glib/json-glib.h>
typedef enum {
  OLLAMA_SENT,
  OLLAMA_THINKING,
  OLLAMA_RECEIVED,
  OLLAMA_FAILURE
} OllamaMessageKind;
typedef void (*OllamaMessageFunc)(OllamaMessageKind kind,
                                  const char *request_id, const char *text,
                                  gpointer data);
/* Attach before starting the worker. The cancellable owns data until the last
 * operation reference is released; callbacks run on the operation's thread. */
void ollama_observe(GCancellable *operation, OllamaMessageFunc callback,
                    gpointer data, GDestroyNotify destroy);
void ollama_event_request(GCancellable *operation, const char *id,
                          const char *path, JsonObject *body);
void ollama_event_response(GCancellable *operation, const char *id,
                           const char *path, gboolean analysis, GBytes *bytes);
void ollama_event_error(GCancellable *operation, const GError *error);
