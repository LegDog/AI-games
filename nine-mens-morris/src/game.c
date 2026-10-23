// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include "game.h"
#include "i18n.h"
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
/* Three nested rings, clockwise from upper left. Midpoints join rings;
 * corners do not. Labels use conventional board coordinates. */
const double game_x[24] = {0, 3, 6, 6, 6, 3, 0, 0, 1, 3, 5, 5,
                           5, 3, 1, 1, 2, 3, 4, 4, 4, 3, 2, 2};
const double game_y[24] = {0, 0, 0, 3, 6, 6, 6, 3, 1, 1, 1, 3,
                           5, 5, 5, 3, 2, 2, 2, 3, 4, 4, 4, 3};
const char *const game_labels[24] = {
    "a7", "d7", "g7", "g4", "g1", "d1", "a1", "a4", "b6", "d6", "f6", "f4",
    "f2", "d2", "b2", "b4", "c5", "d5", "e5", "e4", "e3", "d3", "c3", "c4"};
#define B(n) (1u << (n))
const guint32 game_neighbors[24] = {B(1) | B(7),   B(0) | B(2) | B(9),
                                    B(1) | B(3),   B(2) | B(4) | B(11),
                                    B(3) | B(5),   B(4) | B(6) | B(13),
                                    B(5) | B(7),   B(6) | B(0) | B(15),
                                    B(9) | B(15),  B(8) | B(10) | B(1) | B(17),
                                    B(9) | B(11),  B(10) | B(12) | B(3) | B(19),
                                    B(11) | B(13), B(12) | B(14) | B(5) | B(21),
                                    B(13) | B(15), B(14) | B(8) | B(7) | B(23),
                                    B(17) | B(23), B(16) | B(18) | B(9),
                                    B(17) | B(19), B(18) | B(20) | B(11),
                                    B(19) | B(21), B(20) | B(22) | B(13),
                                    B(21) | B(23), B(22) | B(16) | B(15)};
#define M(a, b, c) (B(a) | B(b) | B(c))
const guint32 game_mills[16] = {
    M(0, 1, 2),    M(2, 3, 4),    M(4, 5, 6),    M(6, 7, 0),
    M(8, 9, 10),   M(10, 11, 12), M(12, 13, 14), M(14, 15, 8),
    M(16, 17, 18), M(18, 19, 20), M(20, 21, 22), M(22, 23, 16),
    M(1, 9, 17),   M(3, 11, 19),  M(5, 13, 21),  M(7, 15, 23)};
static unsigned count(guint32 mask) {
  unsigned n = 0;
  for (; mask; mask &= mask - 1)
    n++;
  return n;
}
unsigned game_cell(const Game *g, int p) {
  if (p < 0 || p >= GAME_POINTS)
    return 0;
  return (g->pieces[0] & B(p)) ? 1 : (g->pieces[1] & B(p)) ? 2 : 0;
}
unsigned game_count(const Game *g, int side) {
  return count(g->pieces[side - 1]);
}
unsigned game_actor(const Game *g) { return g->turn; }
gboolean game_placing(const Game *g) { return g->reserve[0] || g->reserve[1]; }
static guint64 key(const Game *g) {
  return g->pieces[0] | ((guint64)g->pieces[1] << 24) |
         ((guint64)(g->turn - 1) << 48) | ((guint64)g->reserve[0] << 49) |
         ((guint64)g->reserve[1] << 53);
}
void game_init(Game *g) {
  *g = (Game){.reserve = {9, 9}, .turn = 1, .history_count = 1};
  g->history[0] = key(g);
}
guint32 game_mill_pieces(guint32 pieces) {
  guint32 result = 0;
  for (int i = 0; i < GAME_MILLS; i++)
    if ((pieces & game_mills[i]) == game_mills[i])
      result |= game_mills[i];
  return result;
}
unsigned game_mobility(const Game *g, int side) {
  guint32 empty = 0xffffffu & ~(g->pieces[0] | g->pieces[1]);
  if (g->reserve[side - 1])
    return count(empty);
  unsigned n = 0;
  for (int p = 0; p < GAME_POINTS; p++)
    if (g->pieces[side - 1] & B(p))
      n += count(empty & ((!game_placing(g) && game_count(g, side) == 3)
                              ? 0xffffffu
                              : game_neighbors[p]));
  return n;
}
static void add_moves(const Game *g, GameMoves *moves, int from, int to) {
  guint32 pieces = g->pieces[g->turn - 1];
  if (from >= 0)
    pieces &= ~B(from);
  pieces |= B(to);
  /* Only a mill newly closed through the destination earns a capture. */
  gboolean mill = FALSE;
  for (int i = 0; i < GAME_MILLS; i++)
    if ((game_mills[i] & B(to)) && (pieces & game_mills[i]) == game_mills[i])
      mill = TRUE;
  guint32 opponent = g->pieces[2 - g->turn];
  guint32 targets = opponent & ~game_mill_pieces(opponent);
  if (!targets)
    targets = opponent;
  if (mill && targets) {
    for (int p = 0; p < GAME_POINTS; p++)
      if (targets & B(p)) {
        g_assert(moves->count < GAME_CHOICES);
        moves->moves[moves->count++] = (GameMove){from, to, p};
      }
  } else {
    g_assert(moves->count < GAME_CHOICES);
    moves->moves[moves->count++] = (GameMove){from, to, -1};
  }
}
void game_legal_moves(const Game *g, GameMoves *moves) {
  moves->count = 0;
  if (g->result)
    return;
  for (int to = 0; to < GAME_POINTS; to++)
    if (!game_cell(g, to)) {
      if (g->reserve[g->turn - 1])
        add_moves(g, moves, -1, to);
      else
        for (int from = 0; from < GAME_POINTS; from++)
          if (game_cell(g, from) == (unsigned)g->turn &&
              ((!game_placing(g) && game_count(g, g->turn) == 3) ||
               (game_neighbors[from] & B(to))))
            add_moves(g, moves, from, to);
    }
}
gboolean game_move_legal(const Game *g, GameMove move) {
  GameMoves moves;
  game_legal_moves(g, &moves);
  for (unsigned i = 0; i < moves.count; i++)
    if (move.from == moves.moves[i].from && move.to == moves.moves[i].to &&
        move.capture == moves.moves[i].capture)
      return TRUE;
  return FALSE;
}
gboolean game_play(Game *g, GameMove move) {
  if (!game_move_legal(g, move))
    return FALSE;
  int side = g->turn - 1;
  if (move.from < 0)
    g->reserve[side]--;
  else
    g->pieces[side] &= ~B(move.from);
  g->pieces[side] |= B(move.to);
  if (move.capture >= 0)
    g->pieces[1 - side] &= ~B(move.capture);
  g->actions++;
  g->turn = 3 - g->turn;
  gboolean irreversible = move.from < 0 || move.capture >= 0;
  g->quiet = irreversible ? 0 : g->quiet + 1;
  if (irreversible)
    g->history_count = 0;
  guint64 position = key(g);
  unsigned occurrences = 1;
  for (unsigned i = 0; i < g->history_count; i++)
    if (g->history[i] == position)
      occurrences++;
  g_assert(g->history_count < G_N_ELEMENTS(g->history));
  g->history[g->history_count++] = position;
  if (game_count(g, g->turn) + g->reserve[g->turn - 1] < 3) {
    g->result = 3 - g->turn;
    g->end = GAME_MATERIAL;
  } else if (!game_mobility(g, g->turn)) {
    g->result = 3 - g->turn;
    g->end = GAME_BLOCKED;
  } else if (occurrences >= 3) {
    g->result = GAME_DRAW;
    g->end = GAME_REPETITION;
  } else if (g->quiet >= GAME_QUIET_LIMIT) {
    g->result = GAME_DRAW;
    g->end = GAME_QUIET;
  }
  return TRUE;
}
int game_result(const Game *g) { return g->result; }
void game_move_label(GameMove move, char label[16]) {
  if (move.to < 0 || move.to >= 24 || move.from < -1 || move.from >= 24 ||
      move.capture < -1 || move.capture >= 24) {
    g_strlcpy(label, "invalid", 16);
    return;
  }
  if (move.from < 0)
    g_snprintf(label, 16, "%s", game_labels[move.to]);
  else
    g_snprintf(label, 16, "%s-%s", game_labels[move.from],
               game_labels[move.to]);
  if (move.capture >= 0) {
    g_strlcat(label, "x", 16);
    g_strlcat(label, game_labels[move.capture], 16);
  }
}
static int point(const char *s) {
  for (int i = 0; i < 24; i++)
    if (strncmp(s, game_labels[i], 2) == 0)
      return i;
  return -1;
}
gboolean game_parse_move(const char *label, GameMove *move) {
  *move = (GameMove){-1, -1, -1};
  if (!label || strlen(label) < 2)
    return FALSE;
  GameMove m = {-1, point(label), -1};
  const char *s = label + 2;
  if (*s == '-') {
    m.from = m.to;
    if (strlen(++s) < 2)
      return FALSE;
    m.to = point(s);
    s += 2;
  }
  if (*s == 'x') {
    if (strlen(++s) < 2)
      return FALSE;
    m.capture = point(s);
    s += 2;
    if (m.capture < 0)
      return FALSE;
  }
  if (*s || m.to < 0 || (label[2] == '-' && m.from < 0))
    return FALSE;
  *move = m;
  return TRUE;
}
#include "report.inc"
