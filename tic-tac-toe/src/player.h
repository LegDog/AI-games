// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>

typedef enum { PLAYER_HUMAN, PLAYER_POSITIONAL, PLAYER_MINIMAX, PLAYER_OLLAMA } PlayerKind;
typedef struct { PlayerKind kind; unsigned depth; char *model; gboolean ollama_assisted, ollama_thinking, ollama_harness; } PlayerConfig;

static inline const char *player_kind_name(PlayerKind kind)
{
    switch (kind) {
    case PLAYER_HUMAN: return "Humano";
    case PLAYER_POSITIONAL: return "Posicional";
    case PLAYER_MINIMAX: return "Minimax";
    case PLAYER_OLLAMA: return "Ollama";
    default: return "Desconhecido";
    }
}
