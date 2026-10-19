// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>
#define GAME_ID "sokoban"
#define GAME_NAME "AI Games: Sokoban"
#define GAME_HISTORY_FILE "sokoban-history.md"
enum {
  MAX_SIDE = 32,
  MAX_CELLS = 1024,
  MAX_BOXES = 16,
  MAX_PUSHES = 64,
  TILE_FLOOR = 1,
  TILE_GOAL = 2
};
typedef enum { UP, DOWN, LEFT, RIGHT } Direction;
typedef enum {
  PLAYER_HUMAN,
  PLAYER_RANDOM,
  PLAYER_GREEDY,
  PLAYER_ASTAR,
  PLAYER_OLLAMA
} PlayerKind;
typedef struct {
  guint16 player, boxes[MAX_BOXES];
} Position;
typedef struct {
  char name[128];
  unsigned width, height, boxes;
  guint8 terrain[MAX_CELLS];
  Position start;
} Level;
typedef struct {
  const Level *level;
  Position pos;
  unsigned pushes, walking;
} Game;
typedef struct {
  unsigned count;
  guint16 items[MAX_PUSHES];
} MoveList;
typedef struct {
  guint16 distance[MAX_BOXES][MAX_CELLS];
  guint16 goals[MAX_BOXES];
  gboolean dead[MAX_CELLS];
} Analysis;
GPtrArray *levels_parse(const char *text, gsize size, GError **error);
GPtrArray *levels_load(const char *path, GError **error);
GPtrArray *levels_builtin(void);
void game_init(Game *g, const Level *level);
int game_neighbor(const Level *level, unsigned cell, Direction dir);
gboolean game_box(const Game *g, unsigned cell);
gboolean game_step(Game *g, Direction dir, gboolean *pushed);
gboolean game_solved(const Game *g);
gboolean game_valid(const Game *g);
void game_reachable(const Game *g, gboolean reachable[MAX_CELLS],
                    int parent[MAX_CELLS], guint8 directions[MAX_CELLS]);
MoveList game_pushes(const Game *g);
GByteArray *game_push_path(const Game *g, unsigned move);
void game_analyze(const Level *level, Analysis *analysis);
unsigned game_estimate(const Game *g, const Analysis *analysis,
                       gboolean *impossible);
gboolean game_deadlocked(const Game *g, const Analysis *analysis);
char *game_map(const Game *g, const char *separator);
const char *player_name(PlayerKind kind);
gboolean game_append_report(const char *path, const char *report,
                            GError **error);
