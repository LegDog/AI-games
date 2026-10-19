// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include "i18n.h"
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
const char *const game_labels[GAME_POINTS] = {
    "a1",  "b1",  "c1",  "d1",  "e1",  "f1",  "g1",  "h1",  "i1",  "j1",  "k1",
    "l1",  "m1",  "n1",  "o1",  "a2",  "b2",  "c2",  "d2",  "e2",  "f2",  "g2",
    "h2",  "i2",  "j2",  "k2",  "l2",  "m2",  "n2",  "o2",  "a3",  "b3",  "c3",
    "d3",  "e3",  "f3",  "g3",  "h3",  "i3",  "j3",  "k3",  "l3",  "m3",  "n3",
    "o3",  "a4",  "b4",  "c4",  "d4",  "e4",  "f4",  "g4",  "h4",  "i4",  "j4",
    "k4",  "l4",  "m4",  "n4",  "o4",  "a5",  "b5",  "c5",  "d5",  "e5",  "f5",
    "g5",  "h5",  "i5",  "j5",  "k5",  "l5",  "m5",  "n5",  "o5",  "a6",  "b6",
    "c6",  "d6",  "e6",  "f6",  "g6",  "h6",  "i6",  "j6",  "k6",  "l6",  "m6",
    "n6",  "o6",  "a7",  "b7",  "c7",  "d7",  "e7",  "f7",  "g7",  "h7",  "i7",
    "j7",  "k7",  "l7",  "m7",  "n7",  "o7",  "a8",  "b8",  "c8",  "d8",  "e8",
    "f8",  "g8",  "h8",  "i8",  "j8",  "k8",  "l8",  "m8",  "n8",  "o8",  "a9",
    "b9",  "c9",  "d9",  "e9",  "f9",  "g9",  "h9",  "i9",  "j9",  "k9",  "l9",
    "m9",  "n9",  "o9",  "a10", "b10", "c10", "d10", "e10", "f10", "g10", "h10",
    "i10", "j10", "k10", "l10", "m10", "n10", "o10", "a11", "b11", "c11", "d11",
    "e11", "f11", "g11", "h11", "i11", "j11", "k11", "l11", "m11", "n11", "o11",
    "a12", "b12", "c12", "d12", "e12", "f12", "g12", "h12", "i12", "j12", "k12",
    "l12", "m12", "n12", "o12", "a13", "b13", "c13", "d13", "e13", "f13", "g13",
    "h13", "i13", "j13", "k13", "l13", "m13", "n13", "o13", "a14", "b14", "c14",
    "d14", "e14", "f14", "g14", "h14", "i14", "j14", "k14", "l14", "m14", "n14",
    "o14", "a15", "b15", "c15", "d15", "e15", "f15", "g15", "h15", "i15", "j15",
    "k15", "l15", "m15", "n15", "o15"};
static const int dr[] = {0, 1, 1, 1}, dc[] = {1, 0, 1, -1};
static gboolean inside(int r, int c) {
  return r >= 0 && r < GAME_SIZE && c >= 0 && c < GAME_SIZE;
}
void game_init(Game *g) { *g = (Game){.turn = 1, .last = -1}; }
unsigned game_count(const Game *g, int player) {
  unsigned n = 0;
  for (unsigned p = 0; p < GAME_POINTS; p++)
    n += g->cells[p] == player;
  return n;
}
static unsigned run(const Game *g, int r, int c, int dy, int dx, int owner) {
  unsigned n = 0;
  while (inside(r, c) && g->cells[r * GAME_SIZE + c] == owner) {
    n++;
    r += dy;
    c += dx;
  }
  return n;
}
gboolean game_winning_point(const Game *g, unsigned p) {
  if (p >= GAME_POINTS || !g->cells[p])
    return FALSE;
  int r = p / GAME_SIZE, c = p % GAME_SIZE, owner = g->cells[p];
  for (unsigned d = 0; d < 4; d++)
    if (run(g, r, c, dr[d], dc[d], owner) +
            run(g, r, c, -dr[d], -dc[d], owner) - 1 >=
        5)
      return TRUE;
  return FALSE;
}
GameResult game_result(const Game *g) {
  unsigned filled = 0;
  for (int r = 0; r < GAME_SIZE; r++)
    for (int c = 0; c < GAME_SIZE; c++) {
      int owner = g->cells[r * GAME_SIZE + c];
      if (!owner)
        continue;
      filled++;
      for (unsigned d = 0; d < 4; d++) {
        int pr = r - dr[d], pc = c - dc[d];
        if (inside(pr, pc) && g->cells[pr * GAME_SIZE + pc] == owner)
          continue;
        if (run(g, r, c, dr[d], dc[d], owner) >= 5)
          return owner == 1 ? GAME_BLACK : GAME_WHITE;
      }
    }
  return filled == GAME_POINTS ? GAME_DRAW : GAME_ONGOING;
}
gboolean game_move_equal(GameMove a, GameMove b) { return a.cell == b.cell; }
void game_legal_moves(const Game *g, GameMoves *legal) {
  legal->count = 0;
  if (game_result(g))
    return;
  for (int p = 0; p < GAME_POINTS; p++)
    if (!g->cells[p])
      legal->moves[legal->count++] = (GameMove){p};
}
gboolean game_move_legal(const Game *g, GameMove m) {
  return m.cell >= 0 && m.cell < GAME_POINTS && !g->cells[m.cell] &&
         !game_result(g);
}
void game_apply_legal(Game *g, GameMove m) {
  g->cells[m.cell] = g->turn;
  g->last = m.cell;
  g->turn = -g->turn;
  g->actions++;
}
gboolean game_play(Game *g, GameMove m) {
  if (!game_move_legal(g, m))
    return FALSE;
  game_apply_legal(g, m);
  return TRUE;
}
int game_point(const char *s) {
  if (!s || s[0] < 'a' || s[0] > 'o' || s[1] < '1' || s[1] > '9')
    return -1;
  char *end = NULL;
  guint64 row = g_ascii_strtoull(s + 1, &end, 10);
  return *end || strlen(s) > 3 || row < 1 || row > GAME_SIZE
             ? -1
             : (int)(row - 1) * GAME_SIZE + s[0] - 'a';
}
void game_move_label(GameMove m, char label[16]) {
  g_strlcpy(label,
            m.cell >= 0 && m.cell < GAME_POINTS ? game_labels[m.cell]
                                                : "invalid",
            16);
}
gboolean game_parse_move(const char *label, GameMove *m) {
  int p = game_point(label);
  if (p < 0)
    return FALSE;
  *m = (GameMove){p};
  return TRUE;
}
int game_random_move(const GameMoves *legal, GRand *random) {
  return legal->count ? g_rand_int_range(random, 0, legal->count) : -1;
}
/* Score every unblocked length-five window. Terminal values dominate all
 * windows. */
void game_position(const Game *g, int player, PositionValue *v) {
  *v = (PositionValue){0};
  const int weights[] = {0, 1, 10, 100, 10000, 0};
  for (int r = 0; r < GAME_SIZE; r++)
    for (int c = 0; c < GAME_SIZE; c++)
      for (unsigned d = 0; d < 4; d++) {
        if (!inside(r + 4 * dr[d], c + 4 * dc[d]))
          continue;
        unsigned own = 0, other = 0;
        for (int i = 0; i < 5; i++) {
          int stone = g->cells[(r + i * dr[d]) * GAME_SIZE + c + i * dc[d]];
          own += stone == player;
          other += stone == -player;
        }
        if (!other)
          v->own_score += weights[own];
        if (!own)
          v->other_score += weights[other];
      }
  GameResult result = game_result(g);
  v->total = result == GAME_DRAW ? 0
             : result ? (result == player ? GAME_WIN_SCORE : -GAME_WIN_SCORE)
                      : CLAMP(v->own_score - v->other_score,
                              -GAME_WIN_SCORE + 1, GAME_WIN_SCORE - 1);
}
int game_positional_move(const Game *g, GRand *random,
                         int values[GAME_CHOICES]) {
  GameMoves legal;
  game_legal_moves(g, &legal);
  int best = G_MININT, chosen = -1;
  unsigned ties = 0;
  for (unsigned i = 0; i < GAME_CHOICES; i++)
    values[i] = G_MININT;
  for (unsigned i = 0; i < legal.count; i++) {
    Game next = *g;
    game_apply_legal(&next, legal.moves[i]);
    PositionValue v;
    game_position(&next, g->turn, &v);
    values[i] = v.total;
    if (v.total > best) {
      best = v.total;
      chosen = i;
      ties = 1;
    } else if (v.total == best && g_rand_int_range(random, 0, ++ties) == 0)
      chosen = i;
  }
  return chosen;
}
const char *player_name(PlayerKind kind) {
  static const char *const names[] = {
      "Human", "Random", "Heuristic", "Minimax (plain)", "Minimax (alpha-beta)",
      "Ollama"};
  return (unsigned)kind < PLAYER_COUNT ? names[kind] : "Unknown";
}
gboolean game_append_report(const char *path, const char *report,
                            GError **error) {
  int fd = g_open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
  if (fd < 0) {
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                _("Could not open %s: %s"), path, g_strerror(errno));
    return FALSE;
  }
  /* Serialize separate app instances, and roll back an incomplete append so
   * the pending report can be retried without duplicating its prefix. */
  int saved_errno = 0;
  while (flock(fd, LOCK_EX) < 0) {
    if (errno != EINTR) {
      saved_errno = errno;
      break;
    }
  }
  struct stat info;
  gboolean can_rollback = FALSE;
  if (!saved_errno) {
    if (fstat(fd, &info) < 0)
      saved_errno = errno;
    else if (!S_ISREG(info.st_mode))
      saved_errno = EINVAL;
    else
      can_rollback = TRUE;
  }
  gsize remaining = strlen(report);
  const char *cursor = report;
  while (!saved_errno && remaining) {
    ssize_t written = write(fd, cursor, remaining);
    if (written < 0 && errno == EINTR)
      continue;
    if (written <= 0)
      saved_errno = written == 0 ? EIO : errno;
    else {
      cursor += written;
      remaining -= written;
    }
  }
  if (!saved_errno && fsync(fd) < 0)
    saved_errno = errno;
  gboolean rollback_failed = FALSE;
  if (saved_errno && can_rollback)
    rollback_failed = ftruncate(fd, info.st_size) < 0;
  /* Closing releases the advisory lock. Data has already been synchronized. */
  close(fd);
  if (saved_errno)
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                _("Could not write %s: %s%s"), path, g_strerror(saved_errno),
                rollback_failed ? _(". Could not remove the partial write; "
                                    "check the file before retrying")
                                : "");
  return saved_errno == 0;
}
