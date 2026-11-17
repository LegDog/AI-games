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
#define B(n) (G_GUINT64_CONSTANT(1) << (n))
const double game_x[33] = {2, 3, 4, 2, 3, 4, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3,
                           4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 2, 3, 4, 2, 3, 4};
const double game_y[33] = {6, 6, 6, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3,
                           3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 0, 0, 0};
const char *const game_labels[33] = {
    "c1", "d1", "e1", "c2", "d2", "e2", "a3", "b3", "c3", "d3", "e3",
    "f3", "g3", "a4", "b4", "c4", "d4", "e4", "f4", "g4", "a5", "b5",
    "c5", "d5", "e5", "f5", "g5", "c6", "d6", "e6", "c7", "d7", "e7"};
const guint64 game_neighbors[33] = {
    G_GUINT64_CONSTANT(26),         G_GUINT64_CONSTANT(21),
    G_GUINT64_CONSTANT(50),         G_GUINT64_CONSTANT(273),
    G_GUINT64_CONSTANT(1839),       G_GUINT64_CONSTANT(1044),
    G_GUINT64_CONSTANT(24704),      G_GUINT64_CONSTANT(16704),
    G_GUINT64_CONSTANT(115352),     G_GUINT64_CONSTANT(66832),
    G_GUINT64_CONSTANT(461360),     G_GUINT64_CONSTANT(267264),
    G_GUINT64_CONSTANT(788480),     G_GUINT64_CONSTANT(1065024),
    G_GUINT64_CONSTANT(7381440),    G_GUINT64_CONSTANT(4276480),
    G_GUINT64_CONSTANT(29525760),   G_GUINT64_CONSTANT(17105920),
    G_GUINT64_CONSTANT(118103040),  G_GUINT64_CONSTANT(67375104),
    G_GUINT64_CONSTANT(2121728),    G_GUINT64_CONSTANT(5259264),
    G_GUINT64_CONSTANT(413253632),  G_GUINT64_CONSTANT(289472512),
    G_GUINT64_CONSTANT(847708160),  G_GUINT64_CONSTANT(84148224),
    G_GUINT64_CONSTANT(34340864),   G_GUINT64_CONSTANT(1346371584),
    G_GUINT64_CONSTANT(8216641536), G_GUINT64_CONSTANT(4580179968),
    G_GUINT64_CONSTANT(2550136832), G_GUINT64_CONSTANT(5637144576),
    G_GUINT64_CONSTANT(2952790016)};
static unsigned count(guint64 bits) {
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
  return variant == GAME_SEVENTEEN ? "seventeen" : "halatafl";
}
static guint64 key(const Game *g) {
  unsigned fox = 0;
  while (fox < GAME_POINTS && !(g->pieces[0] & B(fox)))
    fox++;
  return g->pieces[1] | ((guint64)fox << 33) | ((guint64)(g->turn - 1) << 39) |
         ((guint64)g->variant << 40);
}
void game_init_variant(Game *g, GameVariant variant) {
  *g = (Game){.variant = variant, .turn = 1, .history_count = 1};
  g->pieces[0] = B(16);
  for (int p = 0; p < GAME_POINTS; p++)
    if (game_y[p] >= 4 || (variant == GAME_SEVENTEEN && game_y[p] >= 2 &&
                           (game_x[p] == 0 || game_x[p] == 6)))
      g->pieces[1] |= B(p);
  g->history[0] = key(g);
}
void game_init(Game *g) { game_init_variant(g, GAME_HALATAFL); }
static int at(double x, double y) {
  for (int p = 0; p < GAME_POINTS; p++)
    if (game_x[p] == x && game_y[p] == y)
      return p;
  return -1;
}
int game_midpoint(int from, int to) {
  return at((game_x[from] + game_x[to]) / 2, (game_y[from] + game_y[to]) / 2);
}
/* Only the fox captures, along two collinear marked edges. */
static int landing(const Game *g, int from, int mid) {
  if (g->turn != 1)
    return -1;
  int to = at(2 * game_x[mid] - game_x[from], 2 * game_y[mid] - game_y[from]);
  return to >= 0 && (game_neighbors[from] & B(mid)) &&
                 (game_neighbors[mid] & B(to)) && game_cell(g, mid) == 2 &&
                 !game_cell(g, to)
             ? to
             : -1;
}
static guint64 destinations(const Game *g, int from, gboolean capture) {
  guint64 out = 0;
  for (int q = 0; q < GAME_POINTS; q++) {
    if (capture) {
      int to = landing(g, from, q);
      if (to >= 0)
        out |= B(to);
    } else if ((game_neighbors[from] & B(q)) && !game_cell(g, q)) {
      if (g->variant == GAME_SEVENTEEN && g->turn == 2 &&
          game_y[q] > game_y[from])
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
    int mid = game_midpoint(from, to);
    g->pieces[2 - g->turn] &= ~B(mid);
  }
}
static gboolean chains(const Game *g, GameMove move, int from, GArray *out,
                       gboolean (*cancelled)(gpointer), gpointer data) {
  if (cancelled && cancelled(data))
    return FALSE;
  guint64 targets = destinations(g, from, TRUE);
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
        guint64 targets = destinations(g, from, FALSE);
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
  if (game_count(g, 2) < 4) {
    g->result = 1;
    g->end = GAME_MATERIAL;
  } else if (!game_mobility(g, 1) || !game_mobility(g, g->turn)) {
    g->result = 2;
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
  for (int p = 0; p < GAME_POINTS; p++)
    if (s[0] == game_labels[p][0] && s[1] == game_labels[p][1])
      return p;
  return -1;
}
gboolean game_parse_move(const char *label, GameMove *move) {
  *move = (GameMove){.from = -1};
  if (!label)
    return FALSE;
  gsize len = strlen(label);
  if (len < 5 || len > 2 + 3 * GAME_MAX_JUMPS || (len - 2) % 3)
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
