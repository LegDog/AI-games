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
/* Board 1: no extensions, joined diagonals, as specified by the Ludii model.
 *  I(8)-----J(9)-----G(6)
 *  H(7)      |      F(5)
 *  D(3)      |      E(4)
 *  A(0)-----B(1)-----C(2)
 * Diagonals D-B-E and H-J-F, with side edges D-H and E-F.
 */
const double game_x[10] = {0, 2, 4, 0, 4, 4, 4, 0, 0, 2};
const double game_y[10] = {0, 0, 0, 1, 1, 1.5, 2.5, 1.5, 2.5, 2.5};
const char *const game_labels[10] = {"A", "B", "C", "D", "E",
                                     "F", "G", "H", "I", "J"};
#define B(n) (1u << (n))
const guint16 game_neighbors[10] = {
    B(1) | B(3),        B(0) | B(2) | B(3) | B(4) | B(9),
    B(1) | B(4),        B(0) | B(1) | B(7),
    B(1) | B(2) | B(5), B(4) | B(6) | B(9),
    B(5) | B(9),        B(3) | B(8) | B(9),
    B(7) | B(9),        B(1) | B(5) | B(6) | B(7) | B(8)};
#undef B
static void setup_round(Game *g) {
  memset(g->cells, 0, sizeof g->cells);
  const int dogs[] = {0, 3, 7, 8}, hares[] = {4, 5};
  for (unsigned i = 0; i < G_N_ELEMENTS(dogs); i++)
    g->cells[dogs[i]] = 1;
  for (unsigned i = 0; i < G_N_ELEMENTS(hares); i++)
    g->cells[hares[i]] = 2;
  g->turn = 1;
  g->hare_moves = 0;
  g->round_over = FALSE;
}
void game_init_limit(Game *g, unsigned limit) {
  *g = (Game){.limit = CLAMP(limit, 1u, 1000u)};
  setup_round(g);
}
void game_init(Game *g) { game_init_limit(g, GAME_DEFAULT_LIMIT); }
unsigned game_actor(const Game *g) { return g->round ? 3 - g->turn : g->turn; }
unsigned game_hare_player(const Game *g) { return g->round ? 1 : 2; }
unsigned game_mobility(const Game *g, int role) {
  unsigned count = 0;
  for (int from = 0; from < GAME_POINTS; from++)
    if (g->cells[from] == role)
      for (int to = 0; to < GAME_POINTS; to++)
        if (!g->cells[to] && (game_neighbors[from] & (1u << to)))
          count++;
  return count;
}
gboolean game_hares_blocked(const Game *g) { return game_mobility(g, 2) == 0; }
void game_legal_moves(const Game *g, GameMoves *moves) {
  moves->count = 0;
  if (g->round_over)
    return;
  for (int from = 0; from < GAME_POINTS; from++)
    if (g->cells[from] == g->turn)
      for (int to = 0; to < GAME_POINTS; to++)
        if (!g->cells[to] && (game_neighbors[from] & (1u << to)))
          moves->moves[moves->count++] = (GameMove){from, to};
  if (!moves->count && g->turn == 1)
    moves->moves[moves->count++] = (GameMove){-1, -1};
}
gboolean game_move_legal(const Game *g, GameMove move) {
  GameMoves legal;
  game_legal_moves(g, &legal);
  for (unsigned i = 0; i < legal.count; i++)
    if (move.from == legal.moves[i].from && move.to == legal.moves[i].to)
      return TRUE;
  return FALSE;
}
static void finish_round(Game *g, gboolean capped) {
  unsigned player = game_hare_player(g) - 1;
  g->survival[player] = g->hare_moves;
  g->capped[player] = capped;
  g->round_over = TRUE;
}
gboolean game_play(Game *g, GameMove move) {
  if (!game_move_legal(g, move))
    return FALSE;
  if (move.from >= 0) {
    g->cells[move.to] = g->cells[move.from];
    g->cells[move.from] = 0;
    if (g->turn == 2)
      g->hare_moves++;
  }
  g->actions++;
  g->turn = 3 - g->turn;
  if (game_hares_blocked(g))
    finish_round(g, FALSE);
  else if (g->hare_moves >= g->limit)
    finish_round(g, TRUE);
  return TRUE;
}
gboolean game_next_round(Game *g) {
  if (!g->round_over || g->round != 0)
    return FALSE;
  g->round = 1;
  setup_round(g);
  return TRUE;
}
int game_result(const Game *g) {
  if (!g->round_over || g->round != 1)
    return 0;
  return g->survival[0] == g->survival[1]  ? GAME_DRAW
         : g->survival[0] > g->survival[1] ? 1
                                           : 2;
}
void game_move_label(GameMove move, char label[16]) {
  if (move.from == -1 && move.to == -1)
    g_strlcpy(label, "pass", 16);
  else if (move.from >= 0 && move.from < 10 && move.to >= 0 && move.to < 10)
    g_snprintf(label, 16, "%s-%s", game_labels[move.from],
               game_labels[move.to]);
  else
    g_strlcpy(label, "invalid", 16);
}
gboolean game_parse_move(const char *label, GameMove *move) {
  if (!label)
    return FALSE;
  if (g_str_equal(label, "pass")) {
    *move = (GameMove){-1, -1};
    return TRUE;
  }
  if (strlen(label) != 3 || label[1] != '-' || label[0] < 'A' ||
      label[0] > 'J' || label[2] < 'A' || label[2] > 'J')
    return FALSE;
  *move = (GameMove){label[0] - 'A', label[2] - 'A'};
  return TRUE;
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
