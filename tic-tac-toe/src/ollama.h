// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <gio/gio.h>
#include "game.h"

#define OLLAMA_DEFAULT_LOG_FILE GAME_ID "-ollama.log"
#define OLLAMA_DEFAULT_ERROR_LOG_FILE GAME_ID "-ollama-error.log"
#define OLLAMA_DEFAULT_URL "http://127.0.0.1:11434"
enum { OLLAMA_LIST_TIMEOUT_MS = 100000, OLLAMA_MOVE_TIMEOUT_MS = 1800000 };
typedef struct { gboolean assisted, thinking; } OllamaOptions;
typedef enum { OLLAMA_REASONING_DEFAULT, OLLAMA_REASONING_NATIVE, OLLAMA_REASONING_COGITO } OllamaReasoning;
typedef struct {
    unsigned legal, winning, safe, allowed, non_losing;
    int landing_rows[GAME_CHOICES];
    unsigned opponent_wins[GAME_CHOICES];
} OllamaAnalysis;
typedef struct {
    int move;
    gint64 elapsed_us;
    gboolean assisted, thinking_requested, complete_defense;
    unsigned allowed;
    OllamaReasoning reasoning;
} OllamaResult;

/* Blocking, cancellable operations intended for GTask workers. NULL url uses
 * the local service. Timeout arguments permit fast deterministic tests. */
char **ollama_list_models(const char *url, guint timeout_ms, GCancellable *cancel, GError **error);
gboolean ollama_choose(const Game *game, const char *model, const char *url,
                       guint timeout_ms, GCancellable *cancel, OllamaResult *result, GError **error);
gboolean ollama_choose_with_options(const Game *game, const char *model, const char *url,
    guint timeout_ms, GCancellable *cancel, const OllamaOptions *options, OllamaResult *result, GError **error);
/* Assisted tic-tac-toe excludes forced losses through exhaustive Boolean
 * search; Four in a Row filters immediate wins/losses. Pure mode stays legal-only. */
void ollama_analyze(const Game *game, gboolean assisted, OllamaAnalysis *analysis);
char *ollama_build_prompt(const Game *game, const OllamaAnalysis *analysis, gboolean assisted);
/* Offline protocol validation, shared with the transport operations. */
char **ollama_parse_models(const char *json, GError **error);
gboolean ollama_parse_move(const Game *game, const char *json, int *move, GError **error);
unsigned ollama_move_number(unsigned move);
void ollama_record(GString *history, const char *model, const OllamaResult *result);
