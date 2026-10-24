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
#define B(n) (1u << (n))
const double game_x[25] = {0.0, 1.5, 3.0, 4.5, 6.0, 0.0, 1.5, 3.0, 4.5,
                           6.0, 0.0, 1.5, 3.0, 4.5, 6.0, 0.0, 1.5, 3.0,
                           4.5, 6.0, 0.0, 1.5, 3.0, 4.5, 6.0};
const double game_y[25] = {6.0, 6.0, 6.0, 6.0, 6.0, 4.5, 4.5, 4.5, 4.5,
                           4.5, 3.0, 3.0, 3.0, 3.0, 3.0, 1.5, 1.5, 1.5,
                           1.5, 1.5, 0.0, 0.0, 0.0, 0.0, 0.0};
const char *const game_labels[25] = {"a1", "b1", "c1", "d1", "e1", "a2", "b2",
                                     "c2", "d2", "e2", "a3", "b3", "c3", "d3",
                                     "e3", "a4", "b4", "c4", "d4", "e4", "a5",
                                     "b5", "c5", "d5", "e5"};
const guint32 game_neighbors[25] = {
    0x62u,     0x45u,     0x1cau,    0x114u,     0x308u,
    0x441u,    0x1ca7u,   0x1144u,   0x729cu,    0x4110u,
    0x18860u,  0x11440u,  0x729c0u,  0x45100u,   0xc2300u,
    0x110400u, 0x729c00u, 0x451000u, 0x1ca7000u, 0x1044000u,
    0x218000u, 0x510000u, 0xa70000u, 0x1440000u, 0x8c0000u};
static unsigned count(guint32 bits) {
  unsigned n = 0;
  for (; bits; bits &= bits - 1)
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
const char *game_variant_name(GameVariant variant) {
  return variant == GAME_DIRECTIONAL ? "directional" : "free";
}
static guint64 key(const Game *g) {
  return g->pieces[0] | ((guint64)g->pieces[1] << 25) |
         ((guint64)(g->turn - 1) << 50) | ((guint64)g->variant << 51);
}
void game_init_variant(Game *g, GameVariant variant) {
  *g = (Game){.pieces = {0x63ffu, 0x1ff8c00u},
              .variant = variant,
              .turn = 1,
              .history_count = 1};
  g->history[0] = key(g);
}
void game_init(Game *g) { game_init_variant(g, GAME_FREE); }
/* Return the landing point for a straight two-edge capture, or -1. */
static int landing(const Game *g, int from, int mid) {
  int x = 2 * (mid % 5) - from % 5, y = 2 * (mid / 5) - from / 5;
  if (x < 0 || x > 4 || y < 0 || y > 4)
    return -1;
  int to = 5 * y + x;
  return (game_neighbors[from] & B(mid)) && (game_neighbors[mid] & B(to)) &&
                 game_cell(g, mid) == (unsigned)(3 - g->turn) &&
                 !game_cell(g, to)
             ? to
             : -1;
}
static guint32 destinations(const Game *g, int from, gboolean capture) {
  guint32 out = 0;
  for (int q = 0; q < GAME_POINTS; q++) {
    if (capture) {
      int to = landing(g, from, q);
      if (to >= 0)
        out |= B(to);
    } else if ((game_neighbors[from] & B(q)) && !game_cell(g, q)) {
      if (g->variant == GAME_DIRECTIONAL &&
          ((g->turn == 1 && (from / 5 == 4 || q / 5 < from / 5)) ||
           (g->turn == 2 && (from / 5 == 0 || q / 5 > from / 5))))
        continue;
      out |= B(q);
    }
  }
  return out;
}
unsigned game_mobility(const Game *g, int side) {
  Game view = *g;
  view.turn = side;
  unsigned captures = 0, quiet = 0;
  for (int p = 0; p < GAME_POINTS; p++)
    if (game_cell(&view, p) == (unsigned)side) {
      captures += count(destinations(&view, p, TRUE));
      quiet += count(destinations(&view, p, FALSE));
    }
  return captures ? captures : quiet;
}
void game_moves_clear(GameMoves *moves) {
  g_clear_pointer(&moves->moves, g_free);
  moves->count = 0;
}
void game_preview_hop(Game *g, int from, int to, gboolean capture) {
  g->pieces[g->turn - 1] = (g->pieces[g->turn - 1] & ~B(from)) | B(to);
  if (capture) {
    int mid = ((from / 5 + to / 5) / 2) * 5 + (from % 5 + to % 5) / 2;
    g->pieces[2 - g->turn] &= ~B(mid);
  }
}
static gboolean chains(const Game *g, GameMove move, int from, GArray *out,
                       gboolean (*cancelled)(gpointer), gpointer data) {
  if (cancelled && cancelled(data))
    return FALSE;
  guint32 targets = destinations(g, from, TRUE);
  if (!targets && move.length)
    g_array_append_val(out, move);
  for (int to = 0; to < GAME_POINTS; to++)
    if (targets & B(to)) {
      g_assert(move.length < GAME_MAX_JUMPS);
      Game next = *g;
      GameMove extended = move;
      extended.land[extended.length++] = to;
      game_preview_hop(&next, from, to, TRUE);
      if (!chains(&next, extended, to, out, cancelled, data))
        return FALSE;
    }
  return TRUE;
}
gboolean game_legal_moves_full(const Game *g, GameMoves *moves,
                               gboolean (*cancelled)(gpointer), gpointer data) {
  game_moves_clear(moves);
  if (g->result)
    return TRUE;
  g_autoptr(GArray) out = g_array_new(FALSE, FALSE, sizeof(GameMove));
  for (int from = 0; from < GAME_POINTS; from++) {
    if (cancelled && cancelled(data))
      return FALSE;
    if (game_cell(g, from) == (unsigned)g->turn &&
        !chains(g, (GameMove){.from = from, .capture = TRUE}, from, out,
                cancelled, data))
      return FALSE;
  }
  if (!out->len) {
    for (int from = 0; from < GAME_POINTS; from++)
      if (game_cell(g, from) == (unsigned)g->turn) {
        guint32 targets = destinations(g, from, FALSE);
        for (int to = 0; to < GAME_POINTS; to++)
          if (targets & B(to)) {
            GameMove m = {.from = from, .length = 1, .land = {to}};
            g_array_append_val(out, m);
          }
      }
  }
  moves->count = out->len;
  moves->moves = (GameMove *)g_array_free(g_steal_pointer(&out), FALSE);
  return TRUE;
}
void game_legal_moves(const Game *g, GameMoves *moves) {
  game_legal_moves_full(g, moves, NULL, NULL);
}
gboolean game_move_prefix(GameMove complete, GameMove prefix) {
  if (complete.length > GAME_MAX_JUMPS || prefix.length > GAME_MAX_JUMPS)
    return FALSE;
  if (prefix.from != complete.from || prefix.length > complete.length ||
      (prefix.length && prefix.capture != complete.capture))
    return FALSE;
  for (unsigned i = 0; i < prefix.length; i++)
    if (prefix.land[i] != complete.land[i])
      return FALSE;
  return TRUE;
}
gboolean game_prefix_valid(const Game *g, GameMove prefix) {
  g_auto(GameMoves) moves = {0};
  game_legal_moves(g, &moves);
  for (unsigned i = 0; i < moves.count; i++)
    if (game_move_prefix(moves.moves[i], prefix))
      return TRUE;
  return FALSE;
}
gboolean game_move_legal(const Game *g, GameMove move) {
  if (g->result || move.from < 0 || move.from >= GAME_POINTS ||
      game_cell(g, move.from) != (unsigned)g->turn || !move.length ||
      move.length > GAME_MAX_JUMPS ||
      (move.capture != FALSE && move.capture != TRUE))
    return FALSE;
  for (unsigned i = 0; i < move.length; i++)
    if (move.land[i] < 0 || move.land[i] >= GAME_POINTS)
      return FALSE;
  gboolean forced = FALSE;
  for (int p = 0; p < GAME_POINTS; p++)
    if (game_cell(g, p) == (unsigned)g->turn && destinations(g, p, TRUE)) {
      forced = TRUE;
      break;
    }
  if (forced != move.capture)
    return FALSE;
  if (!move.capture)
    return move.length == 1 &&
           (destinations(g, move.from, FALSE) & B(move.land[0]));
  Game preview = *g;
  int from = move.from;
  for (unsigned i = 0; i < move.length; i++) {
    if (!(destinations(&preview, from, TRUE) & B(move.land[i])))
      return FALSE;
    game_preview_hop(&preview, from, move.land[i], TRUE);
    from = move.land[i];
  }
  return !destinations(&preview, from, TRUE);
}
gboolean game_play(Game *g, GameMove move) {
  if (!game_move_legal(g, move))
    return FALSE;
  int from = move.from;
  for (unsigned i = 0; i < move.length; i++) {
    game_preview_hop(g, from, move.land[i], move.capture);
    from = move.land[i];
  }
  g->actions++;
  g->turn = 3 - g->turn;
  g->quiet = move.capture ? 0 : g->quiet + 1;
  if (move.capture)
    g->history_count = 0;
  guint64 position = key(g);
  unsigned occurrences = 1;
  for (unsigned i = 0; i < g->history_count; i++)
    if (g->history[i] == position)
      occurrences++;
  g_assert(g->history_count < G_N_ELEMENTS(g->history));
  g->history[g->history_count++] = position;
  if (!game_count(g, g->turn)) {
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
void game_move_label(GameMove move, char label[GAME_LABEL_SIZE]) {
  g_strlcpy(label, "invalid", GAME_LABEL_SIZE);
  if (move.from < 0 || move.from >= GAME_POINTS || !move.length ||
      move.length > GAME_MAX_JUMPS)
    return;
  for (unsigned i = 0; i < move.length; i++)
    if (move.land[i] < 0 || move.land[i] >= GAME_POINTS)
      return;
  g_strlcpy(label, game_labels[move.from], GAME_LABEL_SIZE);
  for (unsigned i = 0; i < move.length; i++) {
    g_strlcat(label, move.capture ? "x" : "-", GAME_LABEL_SIZE);
    g_strlcat(label, game_labels[move.land[i]], GAME_LABEL_SIZE);
  }
}
static int point(const char *s) {
  return s[0] >= 'a' && s[0] <= 'e' && s[1] >= '1' && s[1] <= '5'
             ? (s[1] - '1') * 5 + s[0] - 'a'
             : -1;
}
gboolean game_parse_move(const char *label, GameMove *move) {
  *move = (GameMove){.from = -1};
  if (!label)
    return FALSE;
  gsize len = strlen(label);
  if (len < 5 || len > 38 || (len - 2) % 3)
    return FALSE;
  GameMove m = {.from = point(label), .capture = label[2] == 'x'};
  if (m.from < 0 || (label[2] != '-' && label[2] != 'x'))
    return FALSE;
  for (gsize i = 2; i < len; i += 3) {
    if (m.length == GAME_MAX_JUMPS || label[i] != (m.capture ? 'x' : '-'))
      return FALSE;
    int p = point(label + i + 1);
    if (p < 0)
      return FALSE;
    m.land[m.length++] = p;
  }
  if (!m.capture && m.length != 1)
    return FALSE;
  *move = m;
  return TRUE;
}
#include "report.inc"
