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
const double game_x[32] = {0, 2, 4, 6, 1, 3, 5, 7, 0, 2, 4, 6, 1, 3, 5, 7,
                           0, 2, 4, 6, 1, 3, 5, 7, 0, 2, 4, 6, 1, 3, 5, 7};
const double game_y[32] = {7, 7, 7, 7, 6, 6, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4,
                           3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 0, 0, 0, 0};
const char *const game_labels[32] = {
    "a1", "c1", "e1", "g1", "b2", "d2", "f2", "h2", "a3", "c3", "e3",
    "g3", "b4", "d4", "f4", "h4", "a5", "c5", "e5", "g5", "b6", "d6",
    "f6", "h6", "a7", "c7", "e7", "g7", "b8", "d8", "f8", "h8"};

static int square(int x, int y) {
  return x >= 0 && x < 8 && y >= 0 && y < 8 && !((x + y) & 1) ? y * 4 + x / 2
                                                              : -1;
}
static int col(int p) { return (p % 4) * 2 + (p / 4) % 2; }
static unsigned count(guint32 bits) {
  unsigned n = 0;
  for (; bits; bits &= bits - 1)
    n++;
  return n;
}
unsigned game_cell(const Game *g, int p) {
  if (p < 0 || p >= GAME_POINTS)
    return 0;
  return g->pieces[0] & B(p) ? 1 : g->pieces[1] & B(p) ? 2 : 0;
}
gboolean game_is_king(const Game *g, int p) {
  return p >= 0 && p < GAME_POINTS && !!(g->kings & B(p));
}
unsigned game_count(const Game *g, int side) {
  return side >= 1 && side <= 2 ? count(g->pieces[side - 1]) : 0;
}
unsigned game_actor(const Game *g) { return g->turn; }
unsigned game_quiet_limit(const Game *g) {
  return g->variant == GAME_BRAZILIAN ? 40 : 80;
}
const char *game_variant_name(GameVariant v) {
  return v == GAME_BRAZILIAN ? "brazilian" : "english";
}
static GamePosition position(const Game *g) {
  return (GamePosition){.pieces = {g->pieces[0], g->pieces[1]},
                        .kings = g->kings,
                        .turn = g->turn};
}
void game_init_variant(Game *g, GameVariant v) {
  *g = (Game){.pieces = {0xfffu, 0xfff00000u},
              .variant = v,
              .turn = v == GAME_BRAZILIAN ? 1 : 2,
              .history_count = 1};
  g->history[0] = position(g);
}
void game_init(Game *g) { game_init_variant(g, GAME_ENGLISH); }
void game_moves_clear(GameMoves *m) {
  g_clear_pointer(&m->moves, g_free);
  m->count = 0;
}
/* The first occupied square along a validated capture ray. */
int game_capture_square(const Game *g, int from, int to) {
  if (from < 0 || from >= 32 || to < 0 || to >= 32)
    return -1;
  int dx = col(to) - col(from), dy = to / 4 - from / 4;
  if (!dx || ABS(dx) != ABS(dy))
    return -1;
  int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
  for (int x = col(from) + sx, y = from / 4 + sy; x != col(to);
       x += sx, y += sy) {
    int p = square(x, y);
    if (game_cell(g, p))
      return p;
  }
  return -1;
}
void game_preview_hop(Game *g, int from, int to, gboolean capture) {
  gboolean king = game_is_king(g, from);
  int victim = capture ? game_capture_square(g, from, to) : -1;
  g->pieces[g->turn - 1] = (g->pieces[g->turn - 1] & ~B(from)) | B(to);
  g->kings &= ~B(from);
  if (king)
    g->kings |= B(to);
  if (victim >= 0) {
    if (g->variant == GAME_BRAZILIAN)
      g->captured |= B(victim);
    else {
      g->pieces[2 - g->turn] &= ~B(victim);
      g->kings &= ~B(victim);
    }
  }
}
/* Enumerate single hops. Brazilian captured victims still obstruct rays. */
static unsigned hops(const Game *g, int from, gboolean capture, int to[28],
                     int victims[28]) {
  unsigned n = 0;
  gboolean king = game_is_king(g, from),
           flying = king && g->variant == GAME_BRAZILIAN;
  for (int dy = -1; dy <= 1; dy += 2)
    for (int dx = -1; dx <= 1; dx += 2) {
      if (!king && (!capture || g->variant == GAME_ENGLISH) &&
          dy != (g->turn == 1 ? 1 : -1))
        continue;
      int x = col(from) + dx, y = from / 4 + dy;
      int victim = -1;
      for (int distance = 1;; distance++, x += dx, y += dy) {
        int p = square(x, y);
        if (p < 0)
          break;
        unsigned owner = game_cell(g, p);
        if (owner) {
          if (!capture || victim >= 0 || owner == (unsigned)g->turn ||
              (g->captured & B(p)))
            break;
          victim = p;
        } else if (!capture || victim >= 0) {
          to[n] = p;
          victims[n++] = victim;
          if (!flying)
            break;
        } else if (!flying)
          break;
        if (!flying && distance >= 2)
          break;
      }
    }
  return n;
}
static gboolean crown_rank(const Game *g, int p) {
  return p / 4 == (g->turn == 1 ? 7 : 0);
}
static gboolean chains(const Game *g, GameMove move, int from, GArray *out,
                       gboolean (*cancelled)(gpointer), gpointer data) {
  if (cancelled && cancelled(data))
    return FALSE;
  int targets[28], victims[28];
  unsigned n = hops(g, from, TRUE, targets, victims);
  if (!n && move.length)
    g_array_append_val(out, move);
  for (unsigned i = 0; i < n; i++) {
    g_assert(move.length < GAME_MAX_JUMPS);
    Game next = *g;
    GameMove extended = move;
    extended.victim[extended.length] = victims[i];
    extended.land[extended.length++] = targets[i];
    gboolean stop = g->variant == GAME_ENGLISH && !game_is_king(g, from) &&
                    crown_rank(g, targets[i]);
    game_preview_hop(&next, from, targets[i], TRUE);
    if (stop)
      g_array_append_val(out, extended);
    else if (!chains(&next, extended, targets[i], out, cancelled, data))
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
  for (int p = 0; p < 32; p++) {
    if (cancelled && cancelled(data))
      return FALSE;
    if (game_cell(g, p) == (unsigned)g->turn &&
        !chains(g, (GameMove){.from = p, .capture = TRUE}, p, out, cancelled,
                data))
      return FALSE;
  }
  if (out->len && g->variant == GAME_BRAZILIAN) {
    unsigned max = 0, keep = 0;
    for (unsigned i = 0; i < out->len; i++)
      max = MAX(max, g_array_index(out, GameMove, i).length);
    for (unsigned i = 0; i < out->len; i++) {
      GameMove m = g_array_index(out, GameMove, i);
      if (m.length == max)
        g_array_index(out, GameMove, keep++) = m;
    }
    g_array_set_size(out, keep);
  }
  if (!out->len)
    for (int p = 0; p < 32; p++) {
      if (cancelled && cancelled(data))
        return FALSE;
      if (game_cell(g, p) != (unsigned)g->turn)
        continue;
      int targets[28], victims[28];
      unsigned n = hops(g, p, FALSE, targets, victims);
      for (unsigned i = 0; i < n; i++) {
        GameMove m = {.from = p, .length = 1, .land = {targets[i]}};
        g_array_append_val(out, m);
      }
    }
  moves->count = out->len;
  moves->moves = (GameMove *)g_array_free(g_steal_pointer(&out), FALSE);
  return TRUE;
}
void game_legal_moves(const Game *g, GameMoves *m) {
  game_legal_moves_full(g, m, NULL, NULL);
}
unsigned game_mobility(const Game *g, int side) {
  Game view = *g;
  view.turn = side;
  view.result = 0;
  g_auto(GameMoves) moves = {0};
  game_legal_moves(&view, &moves);
  guint32 seen[32] = {0};
  for (unsigned i = 0; i < moves.count; i++)
    seen[moves.moves[i].from] |= B(moves.moves[i].land[0]);
  unsigned n = 0;
  for (int i = 0; i < 32; i++)
    n += count(seen[i]);
  return n;
}
gboolean game_move_prefix(GameMove complete, GameMove prefix) {
  if (prefix.from != complete.from || prefix.length > complete.length ||
      complete.length > GAME_MAX_JUMPS ||
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
  if (!move.length || move.length > GAME_MAX_JUMPS ||
      (move.capture != TRUE && move.capture != FALSE))
    return FALSE;
  g_auto(GameMoves) moves = {0};
  game_legal_moves(g, &moves);
  for (unsigned i = 0; i < moves.count; i++)
    if (move.length == moves.moves[i].length &&
        game_move_prefix(moves.moves[i], move))
      return TRUE;
  return FALSE;
}
gboolean game_play(Game *g, GameMove move) {
  if (!game_move_legal(g, move))
    return FALSE;
  gboolean man = !game_is_king(g, move.from);
  int from = move.from;
  for (unsigned i = 0; i < move.length; i++) {
    game_preview_hop(g, from, move.land[i], move.capture);
    from = move.land[i];
  }
  g->pieces[0] &= ~g->captured;
  g->pieces[1] &= ~g->captured;
  g->kings &= ~g->captured;
  g->captured = 0;
  if (man && crown_rank(g, from))
    g->kings |= B(from);
  g->actions++;
  g->turn = 3 - g->turn;
  g->quiet = move.capture || man ? 0 : g->quiet + 1;
  if (move.capture || man)
    g->history_count = 0;
  GamePosition pos = position(g);
  unsigned occurrences = 1;
  for (unsigned i = 0; i < g->history_count; i++) {
    GamePosition h = g->history[i];
    if (h.pieces[0] == pos.pieces[0] && h.pieces[1] == pos.pieces[1] &&
        h.kings == pos.kings && h.turn == pos.turn)
      occurrences++;
  }
  g_assert(g->history_count < G_N_ELEMENTS(g->history));
  g->history[g->history_count++] = pos;
  if (!game_count(g, g->turn)) {
    g->result = 3 - g->turn;
    g->end = GAME_MATERIAL;
  } else {
    g_auto(GameMoves) legal = {0};
    game_legal_moves(g, &legal);
    if (!legal.count) {
      g->result = 3 - g->turn;
      g->end = GAME_BLOCKED;
    } else if (occurrences >= 3) {
      g->result = GAME_DRAW;
      g->end = GAME_REPETITION;
    } else if (g->quiet >= game_quiet_limit(g)) {
      g->result = GAME_DRAW;
      g->end = GAME_QUIET;
    }
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
static int point(const char *s) { return square(s[0] - 'a', s[1] - '1'); }
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
