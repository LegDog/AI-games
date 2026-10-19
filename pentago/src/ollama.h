// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../../tools/ollama-events.h"
#include <gio/gio.h>
#include "game.h"
#define OLLAMA_DEFAULT_LOG_FILE GAME_ID "-ollama.log"
#define OLLAMA_DEFAULT_ERROR_LOG_FILE GAME_ID "-ollama-error.log"
#define OLLAMA_DEFAULT_URL "http://127.0.0.1:11434"
enum { OLLAMA_LIST_TIMEOUT_MS=1000000, OLLAMA_MOVE_TIMEOUT_MS=18000000 };
typedef struct { gboolean thinking; } OllamaOptions;
typedef enum { OLLAMA_REASONING_DEFAULT, OLLAMA_REASONING_NATIVE, OLLAMA_REASONING_COGITO } OllamaReasoning;
typedef struct { GameMoves allowed; } OllamaAnalysis;
typedef struct {
    GameMove move;gint64 elapsed_us;gboolean thinking_requested;
    unsigned allowed;OllamaReasoning reasoning;
} OllamaResult;
char **ollama_list_models(const char *url,guint timeout_ms,GCancellable *cancel,GError **error);
char **ollama_parse_models(const char *json,GError **error);
gboolean ollama_choose_with_options(const Game *game,const char *model,const char *url,guint timeout_ms,GCancellable *cancel,const OllamaOptions *options,OllamaResult *result,GError **error);
void ollama_analyze(const Game *game,OllamaAnalysis *analysis);
char *ollama_build_prompt(const Game *game,const OllamaAnalysis *analysis);
gboolean ollama_parse_move(const Game *game,const char *json,GameMove *move,GError **error);
void ollama_record(GString *history,const char *model,const OllamaResult *result);
