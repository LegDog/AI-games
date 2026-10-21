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
  *g = (Game){.reserve = {7, 7}, .turn = 1, .roll = -1};
}
int game_result(const Game *g) {
  return g->finished[0] == 7 ? 1 : g->finished[1] == 7 ? 2 : 0;
}
gboolean game_rosette(int p) { return p == 4 || p == 8 || p == 14; }
gboolean game_set_roll(Game *g, int roll) {
  if (game_result(g) || g->roll != -1 || roll < 0 || roll > 4)
    return FALSE;
  g->roll = roll;
  return TRUE;
}
int game_roll(Game *g, GRand *rng) {
  if (game_result(g) || g->roll != -1)
    return -1;
  int roll = 0;
  for (int i = 0; i < 4; i++)
    roll += g_rand_int_range(rng, 0, 2);
  game_set_roll(g, roll);
  return roll;
}
static gboolean legal_source(const Game *g, int p) {
  int s = g->turn - 1, dest = p + g->roll;
  if (g->roll <= 0 || p < 0 || p > 14 || dest > 15)
    return FALSE;
  if (p == 0 ? !g->reserve[s] : !g->piece[s][p])
    return FALSE;
  if (dest < 15 && g->piece[s][dest])
    return FALSE;
  return dest != 8 || !g->piece[1 - s][8];
}
void game_legal_moves(const Game *g, GameMoves *moves) {
  moves->count = 0;
  if (game_result(g) || g->roll < 0)
    return;
  for (int p = 0; p <= 14; p++)
    if (legal_source(g, p))
      moves->moves[moves->count++] = (GameMove){p};
  if (!moves->count)
    moves->moves[moves->count++] = (GameMove){-1};
}
gboolean game_move_legal(const Game *g, GameMove m) {
  GameMoves moves;
  game_legal_moves(g, &moves);
  for (unsigned i = 0; i < moves.count; i++)
    if (moves.moves[i].cell == m.cell)
      return TRUE;
  return FALSE;
}
gboolean game_play(Game *g, GameMove m) {
  if (!game_move_legal(g, m))
    return FALSE;
  int s = g->turn - 1, dest = m.cell + g->roll;
  gboolean again = FALSE;
  if (m.cell >= 0) {
    if (m.cell == 0)
      g->reserve[s]--;
    else
      g->piece[s][m.cell] = 0;
    if (dest == 15)
      g->finished[s]++;
    else {
      if (dest >= 5 && dest <= 12 && g->piece[1 - s][dest]) {
        g->piece[1 - s][dest] = 0;
        g->reserve[1 - s]++;
      }
      g->piece[s][dest] = 1;
      again = game_rosette(dest);
    }
  }
  if (!again)
    g->turn = 3 - g->turn;
  g->roll = -1;
  g->actions++;
  return TRUE;
}
void game_move_label(GameMove m, char label[16]) {
  if (m.cell == -1)
    g_strlcpy(label, "pass", 16);
  else
    g_snprintf(label, 16, "%d", m.cell);
}
gboolean game_parse_move(const char *label, GameMove *m) {
  if (!label)
    return FALSE;
  if (g_str_equal(label, "pass")) {
    *m = (GameMove){-1};
    return TRUE;
  }
  for (int i = 0; i <= 14; i++) {
    char text[16];
    game_move_label((GameMove){i}, text);
    if (g_str_equal(label, text)) {
      *m = (GameMove){i};
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
