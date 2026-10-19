// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>
#define GAME_ID "pentalpha-crossed-circle"
#define GAME_NAME "AI Games: Pentalfa Cruzado Circulado"
#define GAME_HISTORY_FILE GAME_ID "-history.md"
enum { GAME_POINTS=36, GAME_CHOICES=120, GAME_MAX_DEPTH=35, GAME_PATHS=11, GAME_PATH_POINTS=20 };
#define POINT_BIT(p) (G_GUINT64_CONSTANT(1) << (p))
typedef enum { PLAYER_HUMAN, PLAYER_RANDOM, PLAYER_HEURISTIC, PLAYER_SEARCH, PLAYER_OLLAMA, PLAYER_CONNECTIVITY, PLAYER_FOREST, PLAYER_GF2, PLAYER_SEARCH_SYMMETRY, PLAYER_COUNT } PlayerKind;
typedef struct { unsigned entry, middle, destination, path; } Move;
typedef struct { unsigned count, items[GAME_CHOICES]; } MoveList;
typedef struct { guint64 occupied; unsigned moves; Move history[GAME_MAX_DEPTH]; } Game;
extern const char * const game_labels[GAME_POINTS];
extern const unsigned game_lines[GAME_PATHS][GAME_PATH_POINTS], game_path_lengths[GAME_PATHS];
extern const Move game_moves[GAME_CHOICES];
void game_init(Game *game);
unsigned game_count(guint64 mask);
gboolean move_list_contains(const MoveList *list, unsigned move);
gboolean game_legal(const Game *game, unsigned move);
MoveList game_legal_moves(const Game *game);
int game_result(const Game *game);
gboolean game_play(Game *game, unsigned move);
gboolean game_undo(Game *game);
int game_find_move(unsigned entry, unsigned destination);
int game_point(const char *label);
int game_random_move(MoveList choices, GRand *random);
int game_heuristic(const Game *game, unsigned move);
guint64 game_canonical(guint64 occupied);
const char *player_name(PlayerKind kind);
gboolean game_append_report(const char *path, const char *report, GError **error);
