// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../tools/ollama-events.h"
#include "../../tools/ollama-harness.h"
#include "search.h"
#include <gio/gio.h>
#define OLLAMA_DEFAULT_LOG_FILE GAME_ID "-ollama.log"
#define OLLAMA_DEFAULT_ERROR_LOG_FILE GAME_ID "-ollama-error.log"
#define OLLAMA_DEFAULT_URL "http://127.0.0.1:11434"
enum { OLLAMA_LIST_TIMEOUT_MS = 1000000, OLLAMA_MOVE_TIMEOUT_MS = 18000000 };
typedef struct {
  gboolean thinking;
  gboolean harness;
} OllamaOptions;
typedef enum {
  OLLAMA_REASONING_DEFAULT,
  OLLAMA_REASONING_NATIVE,
  OLLAMA_REASONING_COGITO
} OllamaReasoning;
typedef struct {
  MoveList allowed;
} OllamaAnalysis;
typedef struct {
  int move;
  gint64 elapsed_us;
  gboolean thinking_requested;
  MoveList allowed;
  OllamaReasoning reasoning;
  OllamaRunStats run;
} OllamaResult;
char **ollama_list_models(const char *url, guint timeout_ms,
                          GCancellable *cancel, GError **error);
char **ollama_parse_models(const char *json, GError **error);
gboolean ollama_choose_with_options(const Game *game, const char *model,
                                    const char *url, guint timeout_ms,
                                    GCancellable *cancel,
                                    const OllamaOptions *options,
                                    OllamaResult *result, GError **error);
gboolean ollama_analyze(const Game *game, const OllamaOptions *options,
                        GCancellable *cancel, OllamaAnalysis *analysis,
                        GError **error);
char *ollama_build_prompt(const Game *game, const OllamaAnalysis *analysis);
gboolean ollama_parse_move(const Game *game, const char *json, int *move,
                           GError **error);
void ollama_record(GString *history, const char *model,
                   const OllamaResult *result);
