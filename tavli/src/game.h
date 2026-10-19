// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>
#define GAME_ID "tavli"
#define GAME_NAME "AI Games: Tavli"
#define GAME_HISTORY_FILE GAME_ID "-history.md"
enum { POINTS=24, CHECKERS=15, SOURCE_RESERVE=24, SOURCE_BAR=25, DEST_OFF=26, MAX_STEPS=4 };
typedef enum { PORTES, PLAKOTO, FEVGA, BACKGAMMON, RUNNING, VARIANT_COUNT } Variant;
typedef enum { SESSION_SINGLE, SESSION_FIXED, SESSION_TRADITIONAL } Session;
typedef enum { OPENING, PRE_ROLL, CUBE_RESPONSE, PLAY, GAME_OVER, MATCH_OVER } Phase;
typedef enum { REASON_NONE, REASON_SINGLE, REASON_GAMMON, REASON_BACKGAMMON, REASON_MOTHER, REASON_DROP, REASON_DRAW } ResultReason;
typedef enum { ACTION_STEP, ACTION_ROLL, ACTION_DOUBLE, ACTION_TAKE, ACTION_DROP, ACTION_END } ActionKind;
typedef struct { ActionKind kind; guint8 from,to,die; } Step;
typedef struct { unsigned length; Step steps[MAX_STEPS]; } TurnPlan;
typedef struct {
    Session session;
    Variant selected;
    unsigned target,score[2],number;
    int previous_winner;
    gboolean crawford_used;
} Match;
typedef struct {
    guint8 point[POINTS][2],reserve[2],bar[2],off[2];
    gint8 pinned[POINTS]; /* -1 unpinned; otherwise the color underneath. */
    gboolean escaped[2];
    Variant variant;
    Match match;
    Phase phase;
    int active,actor,winner,cube_owner; /* winner -2 ongoing, -1 draw. */
    unsigned cube,award,turns,steps;
    ResultReason reason;
    gboolean crawford,release_required;
    guint8 roll[2],remaining[7];
} Game;
/* Optional bounded traversal hook. FALSE means interrupted, never a partial legal set. */
typedef struct { gboolean (*check)(gpointer data); gpointer data; gsize memory_bytes; gsize *peak_bytes; } RuleControl;
const char *variant_name(Variant variant);
const char *phase_name(Phase phase);
const char *reason_name(ResultReason reason);
const char *action_name(ActionKind kind);
gboolean game_init(Game *game,Variant variant,Session session,unsigned target);
gboolean game_next(Game *game);
gboolean game_opening(Game *game,unsigned first,unsigned second);
gboolean game_roll(Game *game,unsigned first,unsigned second);
gboolean game_can_double(const Game *game);
gboolean game_control(Game *game,ActionKind action);
int game_point_at(const Game *game,int player,unsigned index);
int game_index(const Game *game,int player,unsigned point);
gboolean game_finished(const Game *game);
gboolean game_valid(const Game *game);
/* All complete turns, including the empty plan for a forced pass. Caller owns array. */
GArray *game_turns(const Game *game,const RuleControl *control);
GArray *game_choices(const Game *game,const RuleControl *control);
gboolean game_step(Game *game,Step step);
gboolean game_apply_step_generated(Game *game,Step step);
gboolean game_play_plan(Game *game,const TurnPlan *plan);
gboolean game_end_turn(Game *game);
/* Apply a generated legal turn without enumerating it again; internal search/UI transaction API. */
gboolean game_apply_generated(Game *game,const TurnPlan *plan);
gboolean step_equal(Step a,Step b);
void step_label(Step step,char *buffer,gsize size);
int game_pips(const Game *game,int player);
int game_heuristic(const Game *game,int player);
double game_value(const Game *game,int player);
gboolean game_trapping_prime(const Game *game,int owner);
gboolean game_append_report(const char *path,const char *report,GError **error);
