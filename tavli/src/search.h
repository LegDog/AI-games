// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game.h"
typedef enum { PLAYER_HUMAN,PLAYER_RANDOM,PLAYER_HEURISTIC,PLAYER_EXPECTI,PLAYER_MCTS,PLAYER_OLLAMA,PLAYER_COUNT } PlayerKind;
typedef enum { SEARCH_COMPLETE,SEARCH_LIMIT,SEARCH_CANCELLED,SEARCH_ERROR } SearchStatus;
typedef struct { unsigned depth,iterations;double seconds;gsize memory_bytes;guint64 node_limit; } SearchOptions;
typedef struct { gboolean (*cancelled)(gpointer);void (*progress)(unsigned,guint64,gpointer);gpointer data; } SearchControl;
typedef struct { gboolean checker;ActionKind action;TurnPlan plan; } Decision;
typedef struct { Decision decision;double value;unsigned visits; } CandidateValue;
typedef struct {
    SearchStatus status;gboolean valid;Decision choice;GArray *candidates;
    unsigned requested,completed,rollout_cutoffs;guint64 nodes;gsize peak_memory;gint64 elapsed_us;
} SearchResult;
const char *player_name(PlayerKind kind);
const char *search_status_name(SearchStatus status);
SearchOptions search_defaults(void);
void search_result_clear(SearchResult *result);
gboolean decision_equal(const Decision *a,const Decision *b);
/* Checker/cube actions only: ACTION_ROLL requires caller-supplied dice via game_roll. */
gboolean decision_apply(Game *game,const Decision *decision);
gboolean search_decide(const Game *game,PlayerKind kind,const SearchOptions *options,const SearchControl *control,GRand *random,SearchResult *result);
