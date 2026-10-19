// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include "game.h"
#include "i18n.h"
#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
static unsigned neighbors(unsigned p, unsigned out[4]) {
  unsigned count = 0;
  if (p % 9)
    out[count++] = p - 1;
  if (p % 9 < 8)
    out[count++] = p + 1;
  if (p >= 9)
    out[count++] = p - 9;
  if (p < 72)
    out[count++] = p + 9;
  return count;
}
int game_at(const Board *b, unsigned p) {
  if (p >= GO_POINTS)
    return -1;
  for (unsigned c = 0; c < 2; c++)
    if (b->stones[c][p / 64] & (G_GUINT64_CONSTANT(1) << (p % 64)))
      return c;
  return -1;
}
void game_set(Board *b, unsigned p, int c) {
  g_return_if_fail(p < GO_POINTS && c >= -1 && c <= 1);
  guint64 bit = G_GUINT64_CONSTANT(1) << (p % 64);
  b->stones[0][p / 64] &= ~bit;
  b->stones[1][p / 64] &= ~bit;
  if (c >= 0)
    b->stones[c][p / 64] |= bit;
}
Group game_group(const Board *b, unsigned p) {
  Group group = {0};
  int color = game_at(b, p);
  if (color < 0)
    return group;
  gboolean visited[GO_POINTS] = {0}, liberties[GO_POINTS] = {0};
  unsigned queue[GO_POINTS], head = 0, tail = 0;
  queue[tail++] = p;
  visited[p] = TRUE;
  while (head < tail) {
    unsigned q = queue[head++], adjacent[4], n = neighbors(q, adjacent);
    group.stones[q / 64] |= G_GUINT64_CONSTANT(1) << (q % 64);
    group.size++;
    for (unsigned i = 0; i < n; i++) {
      unsigned a = adjacent[i];
      int at = game_at(b, a);
      if (at < 0 && !liberties[a]) {
        liberties[a] = TRUE;
        group.liberties++;
      } else if (at == color && !visited[a]) {
        visited[a] = TRUE;
        queue[tail++] = a;
      }
    }
  }
  return group;
}
static guint64 board_hash(const Board *b) {
  guint64 hash = G_GUINT64_CONSTANT(0xcbf29ce484222325);
  for (unsigned c = 0; c < 2; c++)
    for (unsigned w = 0; w < 2; w++) {
      guint64 x = b->stones[c][w] +
                  G_GUINT64_CONSTANT(0x9e3779b97f4a7c15) * (1 + c * 2 + w);
      x = (x ^ (x >> 30)) * G_GUINT64_CONSTANT(0xbf58476d1ce4e5b9);
      x = (x ^ (x >> 27)) * G_GUINT64_CONSTANT(0x94d049bb133111eb);
      hash ^= x ^ (x >> 31);
      hash *= G_GUINT64_CONSTANT(0x100000001b3);
    }
  return hash;
}
static void remember(Game *g, guint64 h) {
  unsigned a = h % 4096, b = (h >> 32) % 4096;
  g->seen[a / 64] |= G_GUINT64_CONSTANT(1) << (a % 64);
  g->seen[b / 64] |= G_GUINT64_CONSTANT(1) << (b % 64);
}
static gboolean repeated(const Game *g, const State *s) {
  unsigned a = s->hash % 4096, b = (s->hash >> 32) % 4096;
  if (!(g->seen[a / 64] & (G_GUINT64_CONSTANT(1) << (a % 64))) ||
      !(g->seen[b / 64] & (G_GUINT64_CONSTANT(1) << (b % 64))))
    return FALSE;
  for (unsigned i = 0; i < g->history->len; i++) {
    const State *old = &g_array_index(g->history, State, i);
    if (old->hash == s->hash && !memcmp(&old->board, &s->board, sizeof(Board)))
      return TRUE;
  }
  return FALSE;
}
void game_init(Game *g, double komi) {
  *g = (Game){.komi = komi, .state = {.last = -1}};
  g->history = g_array_sized_new(FALSE, FALSE, sizeof(State), 128);
  g->state.hash = board_hash(&g->state.board);
  g_array_append_val(g->history, g->state);
  remember(g, g->state.hash);
}
void game_clear(Game *g) { g_clear_pointer(&g->history, g_array_unref); }
void game_copy(Game *target, const Game *source, unsigned extra) {
  *target = *source;
  target->history = g_array_sized_new(FALSE, FALSE, sizeof(State),
                                      source->history->len + extra);
  g_array_append_vals(target->history, source->history->data,
                      source->history->len);
}
gboolean game_undo(Game *g) {
  if (g->history->len <= 1)
    return FALSE;
  g_array_set_size(g->history, g->history->len - 1);
  g->state = g_array_index(g->history, State, g->history->len - 1);
  memset(g->seen, 0, sizeof g->seen);
  for (unsigned i = 0; i < g->history->len; i++)
    remember(g, g_array_index(g->history, State, i).hash);
  return TRUE;
}
gboolean game_finished(const Game *g) { return g->state.passes >= 2; }
MoveError game_try(const Game *g, unsigned move, State *out) {
  if (game_finished(g))
    return MOVE_FINISHED;
  if (move >= GO_CHOICES)
    return MOVE_OUTSIDE;
  State s = g->state;
  s.removed[0] = s.removed[1] = 0;
  s.last = move;
  s.turn ^= 1;
  if (move == GO_PASS) {
    s.passes++;
    if (out)
      *out = s;
    return MOVE_OK;
  }
  if (game_at(&s.board, move) >= 0)
    return MOVE_OCCUPIED;
  unsigned player = g->state.turn;
  s.passes = 0;
  game_set(&s.board, move, player);
  unsigned adjacent[4], n = neighbors(move, adjacent);
  for (unsigned i = 0; i < n; i++)
    if (game_at(&s.board, adjacent[i]) == (int)(player ^ 1)) {
      Group group = game_group(&s.board, adjacent[i]);
      if (group.liberties)
        continue;
      for (unsigned w = 0; w < 2; w++) {
        s.board.stones[player ^ 1][w] &= ~group.stones[w];
        s.removed[w] |= group.stones[w];
      }
      s.captures[player] += group.size;
    }
  if (!game_group(&s.board, move).liberties)
    return MOVE_SUICIDE;
  s.hash = board_hash(&s.board);
  if (repeated(g, &s))
    return MOVE_SUPERKO;
  if (out)
    *out = s;
  return MOVE_OK;
}
gboolean game_play(Game *g, unsigned move) {
  State next;
  if (game_try(g, move, &next) != MOVE_OK)
    return FALSE;
  g->state = next;
  g_array_append_val(g->history, next);
  remember(g, next.hash);
  return TRUE;
}
MoveList game_legal(const Game *g) {
  MoveList list = {0};
  if (game_finished(g))
    return list;
  for (unsigned i = 0; i < GO_CHOICES; i++)
    if (game_try(g, i, NULL) == MOVE_OK)
      list.items[list.count++] = i;
  return list;
}
gboolean game_valid(const Game *g) {
  if (!g->history || !g->history->len || g->state.turn > 1 ||
      g->state.passes > 2 || !isfinite(g->komi) || g->komi < 0 ||
      g->komi > 20 || floor(g->komi * 2) != g->komi * 2)
    return FALSE;
  for (unsigned w = 0; w < 2; w++)
    if (g->state.board.stones[0][w] & g->state.board.stones[1][w])
      return FALSE;
  if ((g->state.board.stones[0][1] | g->state.board.stones[1][1]) >> 17)
    return FALSE;
  gboolean seen[GO_POINTS] = {0};
  for (unsigned p = 0; p < GO_POINTS; p++)
    if (game_at(&g->state.board, p) >= 0 && !seen[p]) {
      Group group = game_group(&g->state.board, p);
      if (!group.liberties)
        return FALSE;
      for (unsigned q = 0; q < GO_POINTS; q++)
        if (group.stones[q / 64] & (G_GUINT64_CONSTANT(1) << (q % 64)))
          seen[q] = TRUE;
    }
  const State *last = &g_array_index(g->history, State, g->history->len - 1);
  return !memcmp(&g->state.board, &last->board, sizeof(Board)) &&
         g->state.turn == last->turn && g->state.passes == last->passes &&
         g->state.last == last->last &&
         g->state.captures[0] == last->captures[0] &&
         g->state.captures[1] == last->captures[1] &&
         g->state.removed[0] == last->removed[0] &&
         g->state.removed[1] == last->removed[1] &&
         g->state.hash == last->hash &&
         g->state.hash == board_hash(&g->state.board);
}
void game_score(const Game *g, Score *s) {
  *s = (Score){0};
  gboolean seen[GO_POINTS] = {0};
  for (unsigned p = 0; p < GO_POINTS; p++)
    s->owner[p] = -1;
  for (unsigned p = 0; p < GO_POINTS; p++) {
    int color = game_at(&g->state.board, p);
    if (color >= 0) {
      s->stones[color]++;
      continue;
    }
    if (seen[p])
      continue;
    unsigned queue[GO_POINTS], head = 0, tail = 0, border = 0;
    queue[tail++] = p;
    seen[p] = TRUE;
    while (head < tail) {
      unsigned q = queue[head++], adj[4], n = neighbors(q, adj);
      for (unsigned i = 0; i < n; i++) {
        int c = game_at(&g->state.board, adj[i]);
        if (c >= 0)
          border |= 1u << c;
        else if (!seen[adj[i]]) {
          seen[adj[i]] = TRUE;
          queue[tail++] = adj[i];
        }
      }
    }
    int owner = border == 1 ? BLACK : border == 2 ? WHITE : -1;
    if (owner >= 0)
      s->territory[owner] += tail;
    else
      s->neutral += tail;
    for (unsigned i = 0; i < tail; i++)
      s->owner[queue[i]] = owner;
  }
  for (unsigned c = 0; c < 2; c++)
    s->area[c] = s->stones[c] + s->territory[c] + (c == WHITE ? g->komi : 0);
  s->margin = s->area[BLACK] - s->area[WHITE];
}
int game_winner(const Game *g) {
  if (!game_finished(g))
    return -1;
  Score score;
  game_score(g, &score);
  return score.margin > 0 ? BLACK : score.margin < 0 ? WHITE : 2;
}
int game_random(const MoveList *legal, GRand *r) {
  return legal->count ? legal->items[g_rand_int_range(r, 0, legal->count)] : -1;
}
int game_evaluate(const Game *g, unsigned color) {
  Score s;
  game_score(g, &s);
  int liberties[2] = {0}, atari[2] = {0};
  gboolean seen[GO_POINTS] = {0};
  for (unsigned p = 0; p < GO_POINTS; p++) {
    int c = game_at(&g->state.board, p);
    if (c < 0 || seen[p])
      continue;
    Group group = game_group(&g->state.board, p);
    liberties[c] += group.liberties;
    if (group.liberties == 1)
      atari[c] += group.size;
    for (unsigned q = 0; q < GO_POINTS; q++)
      if (group.stones[q / 64] & (G_GUINT64_CONSTANT(1) << (q % 64)))
        seen[q] = TRUE;
  }
  return (int)round(10 * (s.area[color] - s.area[color ^ 1])) +
         2 * (liberties[color] - liberties[color ^ 1]) -
         15 * (atari[color] - atari[color ^ 1]);
}
int game_heuristic(const Game *g, GRand *r, int values[GO_CHOICES]) {
  int best = G_MININT, chosen = -1;
  unsigned ties = 0;
  for (unsigned m = 0; m < GO_CHOICES; m++)
    values[m] = G_MININT;
  MoveList legal = game_legal(g);
  for (unsigned i = 0; i < legal.count; i++) {
    State next;
    unsigned m = legal.items[i];
    game_try(g, m, &next);
    Game successor = *g;
    successor.state = next;
    int value =
        game_evaluate(&successor, g->state.turn) +
        8 * (next.captures[g->state.turn] - g->state.captures[g->state.turn]);
    if (game_finished(&successor)) {
      int winner = game_winner(&successor);
      value = winner == 2 ? 0 : winner == (int)g->state.turn ? 100000 : -100000;
    }
    values[m] = value;
    if (value > best) {
      best = value;
      chosen = m;
      ties = 1;
    } else if (value == best && g_rand_int_range(r, 0, ++ties) == 0)
      chosen = m;
  }
  return chosen;
}
const char *game_label(unsigned p) {
  static char labels[GO_CHOICES][5];
  static gsize initialized;
  if (g_once_init_enter(&initialized)) {
    const char *cols = "ABCDEFGHJ";
    for (unsigned i = 0; i < GO_POINTS; i++)
      g_snprintf(labels[i], sizeof labels[i], "%c%u", cols[i % 9], i / 9 + 1);
    g_strlcpy(labels[GO_PASS], "pass", 5);
    g_once_init_leave(&initialized, 1);
  }
  return p < GO_CHOICES ? labels[p] : "invalid";
}
int game_point(const char *text) {
  if (!text)
    return -1;
  for (unsigned m = 0; m < GO_CHOICES; m++)
    if (!g_ascii_strcasecmp(text, game_label(m)))
      return m;
  return -1;
}
const char *move_error_name(MoveError e) {
  static const char *names[] = {
      "Legal move",           "Game finished",
      "Outside board",        "Occupied intersection",
      "Suicide is forbidden", "Positional superko forbids this repetition"};
  return e <= MOVE_SUPERKO ? names[e] : "Invalid move";
}
const char *player_name(PlayerKind p) {
  static const char *names[] = {"Human", "Random", "Heuristic", "MCTS",
                                "Ollama"};
  return p < PLAYER_COUNT ? names[p] : "Unknown";
}
char *game_map(const Game *g) {
  GString *s = g_string_new("   A B C D E F G H J\n");
  for (int row = 8; row >= 0; row--) {
    g_string_append_printf(s, "%d  ", row + 1);
    for (unsigned col = 0; col < 9; col++) {
      int color = game_at(&g->state.board, row * 9 + col);
      g_string_append_printf(s, "%c ",
                             color < 0        ? '.'
                             : color == BLACK ? 'B'
                                              : 'W');
    }
    g_string_append_c(s, '\n');
  }
  return g_string_free(s, FALSE);
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
