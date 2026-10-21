// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <gio/gio.h>
#include <json-glib/json-glib.h>
typedef JsonObject *(*OllamaTransport)(const char *base, const char *path,
                                       JsonObject *body, guint timeout_ms,
                                       GCancellable *cancel, GError **error);
typedef struct {
  gboolean harness;
  unsigned generations, completed_generations, token_reports;
  guint64 prompt_tokens, output_tokens;
} OllamaRunStats;
/* A thread-local operation scope; nested calls and simultaneous workers are
 * isolated. */
typedef struct {
  gpointer previous;
  OllamaRunStats *stats;
  const char *stage;
} OllamaRun;
void ollama_run_begin(OllamaRun *run, OllamaRunStats *stats, gboolean harness);
void ollama_run_end(OllamaRun *run);
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(OllamaRun, ollama_run_end)
const char *ollama_run_stage(void);
void ollama_run_annotate(JsonObject *entry);
void ollama_run_request(const char *path, JsonObject *body);
void ollama_run_response(const char *path, JsonObject *response);
/* Optional evaluation-only generation settings. NULL clears the current
 * thread's override. */
void ollama_run_set_generation_options(JsonObject *options);
void ollama_run_record(GString *history, const OllamaRunStats *stats);
JsonObject *ollama_harness_run(const char *url, const char *model,
                               const char *position, JsonObject *schema,
                               gboolean thinking, gint64 started,
                               guint total_ms, GCancellable *cancel,
                               OllamaTransport transport, int *reasoning,
                               GError **error);
