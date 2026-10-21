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

void game_init(Game *g) {
  *g = (Game){.turn = 1, .roll = -1};
  for (int p = 1; p <= 10; p++)
    g->cells[p] = (p % 2) ? 1 : 2;
}
int game_result(const Game *g) {
  return g->finished[0] == GAME_PIECES   ? 1
         : g->finished[1] == GAME_PIECES ? 2
                                         : 0;
}
int game_sticks_value(unsigned faces) {
  if (faces > 15)
    return -1;
  int sum = 0;
  for (int i = 0; i < 4; i++)
    sum += (faces >> i) & 1;
  return sum ? sum : 5;
}
gboolean game_set_roll(Game *g, int roll) {
  if (game_result(g) || g->roll != -1 || roll < 1 || roll > 5)
    return FALSE;
  g->roll = roll;
  return TRUE;
}
int game_roll(Game *g, GRand *rng) {
  if (game_result(g) || g->roll != -1)
    return -1;
  int roll = game_sticks_value(g_rand_int_range(rng, 0, 16));
  game_set_roll(g, roll);
  return roll;
}
gboolean game_protected(const Game *g, int p) {
  if (p < 1 || p > 30 || !g->cells[p])
    return FALSE;
  int side = g->cells[p];
  /* Adjacency follows the numbered track, including row bends. */
  return (p > 1 && g->cells[p - 1] == side) ||
         (p < 30 && g->cells[p + 1] == side);
}
static gboolean legal_source(const Game *g, int p) {
  if (p < 1 || p > 30 || g->cells[p] != g->turn || g->roll < 1 || g->roll > 5)
    return FALSE;
  int dest = p + g->roll;
  if (p >= 28)
    return dest == 31;
  /* Every piece must stop on Happiness. Bearing off is from 28..30 only. */
  if ((p < 26 && dest > 26) || dest > 30)
    return FALSE;
  return g->cells[dest] != g->turn &&
         (!g->cells[dest] || !game_protected(g, dest));
}
void game_legal_moves(const Game *g, GameMoves *moves) {
  moves->count = 0;
  if (game_result(g) || g->roll < 0)
    return;
  for (int p = 1; p <= 30; p++)
    if (legal_source(g, p))
      moves->moves[moves->count++] = (GameMove){p};
  if (!moves->count)
    moves->moves[moves->count++] = (GameMove){-1};
}
gboolean game_move_legal(const Game *g, GameMove move) {
  GameMoves moves;
  game_legal_moves(g, &moves);
  for (unsigned i = 0; i < moves.count; i++)
    if (moves.moves[i].cell == move.cell)
      return TRUE;
  return FALSE;
}
int game_destination(const Game *g, GameMove move) {
  if (!game_move_legal(g, move) || move.cell == -1)
    return -1;
  int dest = move.cell + g->roll;
  if (dest != 27)
    return dest;
  /* The vacated source counts as empty when returning from Water. */
  for (int p = 15; p >= 1; p--)
    if (!g->cells[p] || p == move.cell)
      return p;
  g_assert_not_reached(); /* At most ten pieces exist, so a vacancy must exist.
                           */
}
gboolean game_play(Game *g, GameMove move) {
  if (!game_move_legal(g, move))
    return FALSE;
  gboolean pass = move.cell == -1;
  if (!pass) {
    int dest = game_destination(g, move), owner = g->turn;
    if (dest == 31) {
      g->cells[move.cell] = 0;
      g->finished[owner - 1]++;
    } else {
      /* Water returns to a vacant house; other landings exchange opponents. */
      g->cells[move.cell] = g->cells[dest];
      g->cells[dest] = owner;
    }
  }
  if (pass || (g->roll != 1 && g->roll != 4 && g->roll != 5))
    g->turn = 3 - g->turn;
  g->roll = -1;
  g->actions++;
  return TRUE;
}
void game_move_label(GameMove move, char label[16]) {
  if (move.cell == -1)
    g_strlcpy(label, "pass", 16);
  else
    g_snprintf(label, 16, "%d", move.cell);
}
gboolean game_parse_move(const char *label, GameMove *move) {
  if (!label)
    return FALSE;
  if (g_str_equal(label, "pass")) {
    *move = (GameMove){-1};
    return TRUE;
  }
  for (int p = 1; p <= 30; p++) {
    char text[16];
    game_move_label((GameMove){p}, text);
    if (g_str_equal(text, label)) {
      *move = (GameMove){p};
      return TRUE;
    }
  }
  return FALSE;
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
