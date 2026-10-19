// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include "game.h"
#include "i18n.h"
#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
static gint cell_compare(gconstpointer a, gconstpointer b) {
  return (int)*(const guint16 *)a - *(const guint16 *)b;
}
int game_neighbor(const Level *l, unsigned c, Direction d) {
  if (c >= l->width * l->height || d > RIGHT)
    return -1;
  int x = c % l->width, y = c / l->width;
  x += (d == RIGHT) - (d == LEFT);
  y += (d == DOWN) - (d == UP);
  return x < 0 || y < 0 || x >= (int)l->width || y >= (int)l->height
             ? -1
             : y * (int)l->width + x;
}
static gboolean floor_at(const Level *l, int c) {
  return c >= 0 && c < (int)(l->width * l->height) &&
         (l->terrain[c] & TILE_FLOOR);
}
static gboolean parse_level(GPtrArray *pack, GPtrArray *rows, const char *name,
                            GError **error) {
  if (!rows->len)
    return TRUE;
  unsigned width = 0;
  for (unsigned y = 0; y < rows->len; y++)
    width = MAX(width, strlen(g_ptr_array_index(rows, y)));
  if (pack->len >= 256 || rows->len > MAX_SIDE || width > MAX_SIDE || !width)
    goto invalid;
  Level *l = g_new0(Level, 1);
  l->width = width;
  l->height = rows->len;
  g_snprintf(l->name, sizeof l->name, "%s",
             name && *name ? name : "Imported level");
  const char *valid_end = NULL;
  if (!g_utf8_validate(l->name, -1, &valid_end))
    l->name[valid_end - l->name] = 0;
  unsigned players = 0, goals = 0;
  gboolean spaces[MAX_CELLS] = {0};
  for (unsigned y = 0; y < l->height; y++) {
    const char *row = g_ptr_array_index(rows, y);
    unsigned length = strlen(row);
    for (unsigned x = 0; x < width; x++) {
      unsigned c = y * width + x;
      char ch = x < length ? row[x] : ' ';
      if (!strchr("# .$@*+", ch)) {
        g_free(l);
        goto invalid;
      }
      if (ch == '#')
        continue;
      l->terrain[c] = TILE_FLOOR;
      spaces[c] = ch == ' ';
      if (ch == '.' || ch == '*' || ch == '+') {
        l->terrain[c] |= TILE_GOAL;
        goals++;
      }
      if (ch == '@' || ch == '+') {
        players++;
        l->start.player = c;
      }
      if (ch == '$' || ch == '*') {
        if (l->boxes == MAX_BOXES) {
          g_free(l);
          goto invalid;
        }
        l->start.boxes[l->boxes++] = c;
      }
    }
  }
  /* Flood exterior through spaces only. Any exposed actor/goal is invalid. */
  gboolean outside[MAX_CELLS] = {0};
  unsigned queue[MAX_CELLS], head = 0, tail = 0;
  for (unsigned c = 0; c < width * l->height; c++)
    if (c < width || c >= width * (l->height - 1) || c % width == 0 ||
        c % width == width - 1) {
      if (l->terrain[c]) {
        if (!spaces[c]) {
          g_free(l);
          goto invalid;
        }
        outside[c] = TRUE;
        queue[tail++] = c;
      }
    }
  while (head < tail) {
    unsigned c = queue[head++];
    for (Direction d = UP; d <= RIGHT; d++) {
      int n = game_neighbor(l, c, d);
      if (n < 0 || !l->terrain[n] || outside[n])
        continue;
      if (!spaces[n]) {
        g_free(l);
        goto invalid;
      }
      outside[n] = TRUE;
      queue[tail++] = n;
    }
  }
  for (unsigned c = 0; c < width * l->height; c++)
    if (outside[c])
      l->terrain[c] = 0;
  if (players != 1 || !l->boxes || goals != l->boxes) {
    g_free(l);
    goto invalid;
  }
  qsort(l->start.boxes, l->boxes, sizeof(guint16), cell_compare);
  g_ptr_array_add(pack, l);
  g_ptr_array_set_size(rows, 0);
  return TRUE;
invalid:
  g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
              "Invalid XSB level %u: enclosed map, one player, equal "
              "goals/boxes (1–16), and size at most 32×32 required.",
              pack->len + 1);
  return FALSE;
}
GPtrArray *levels_parse(const char *text, gsize size, GError **error) {
  if (size > 1024 * 1024 || memchr(text, 0, size) ||
      !g_utf8_validate(text, size, NULL)) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "XSB must be UTF-8 text without NUL, at most 1 MiB.");
    return NULL;
  }
  g_autofree char *copy = g_strndup(text, size);
  g_auto(GStrv) lines = g_strsplit(copy, "\n", -1);
  GPtrArray *pack = g_ptr_array_new_with_free_func(g_free);
  g_autoptr(GPtrArray) rows = g_ptr_array_new_with_free_func(g_free);
  g_autofree char *name = NULL;
  for (unsigned i = 0; lines[i]; i++) {
    char *line = lines[i];
    gsize length = strlen(line);
    if (length && line[length - 1] == '\r')
      line[--length] = 0;
    if (line[0] == ';' || strspn(line, " \t") == length) {
      if (!parse_level(pack, rows, name, error)) {
        g_ptr_array_unref(pack);
        return NULL;
      }
      if (line[0] == ';') {
        g_free(name);
        name = g_strdup(g_strstrip(line + 1));
      } else
        g_clear_pointer(&name, g_free);
    } else
      g_ptr_array_add(rows, g_strdup(line));
  }
  if (!parse_level(pack, rows, name, error)) {
    g_ptr_array_unref(pack);
    return NULL;
  }
  if (!pack->len) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "No XSB levels found.");
    g_ptr_array_unref(pack);
    return NULL;
  }
  return pack;
}
GPtrArray *levels_load(const char *path, GError **error) {
  /* Read at most the cap plus one, including files that grow while reading. */
  FILE *file = fopen(path, "rb");
  if (!file) {
    g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "%s: %s",
                path, g_strerror(errno));
    return NULL;
  }
  g_autofree char *text = g_malloc(1024 * 1024 + 1);
  gsize size = fread(text, 1, 1024 * 1024 + 1, file);
  gboolean failed = ferror(file);
  fclose(file);
  if (failed) {
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "Could not read XSB file.");
    return NULL;
  }
  return levels_parse(text, size, error);
}
GPtrArray *levels_builtin(void) {
  static const char text[] =
      "; First delivery\n######\n#@ $.#\n######\n\n"
      "; Two deliveries\n########\n#      #\n# .  . #\n# $  $ #\n#  @   "
      "#\n########\n\n"
      "; Corner approach\n#######\n#  .  #\n#  $  #\n#     #\n# @   "
      "#\n#######\n\n"
      "; Three aisles\n#########\n# . . . #\n# $ $ $ #\n#       #\n#   @   "
      "#\n#########\n\n"
      "; Around the pillar\n#########\n#  . .  #\n#  $ $  #\n#   #   #\n#   @  "
      " #\n#########\n\n"
      "; Cross traffic\n#########\n#       #\n# . $ . #\n#   $   #\n#   @   "
      "#\n#########\n\n"
      "; Four shelves\n##########\n# . . . .#\n# $ $ $ $#\n#        #\n#    @  "
      " #\n##########\n\n"
      "; Six deliveries\n###############\n# . . . . . . #\n# $ $ $ $ $ $ #\n#  "
      "           #\n#      @      #\n###############\n";
  GError *error = NULL;
  GPtrArray *pack = levels_parse(text, strlen(text), &error);
  g_assert_no_error(error);
  return pack;
}
void game_init(Game *g, const Level *l) {
  *g = (Game){.level = l, .pos = l->start};
}
gboolean game_box(const Game *g, unsigned c) {
  for (unsigned i = 0; i < g->level->boxes; i++)
    if (g->pos.boxes[i] == c)
      return TRUE;
  return FALSE;
}
gboolean game_step(Game *g, Direction d, gboolean *pushed) {
  int n = game_neighbor(g->level, g->pos.player, d);
  if (pushed)
    *pushed = FALSE;
  if (!floor_at(g->level, n))
    return FALSE;
  if (game_box(g, n)) {
    int dest = game_neighbor(g->level, n, d);
    if (!floor_at(g->level, dest) || game_box(g, dest))
      return FALSE;
    for (unsigned i = 0; i < g->level->boxes; i++)
      if (g->pos.boxes[i] == n) {
        g->pos.boxes[i] = dest;
        break;
      }
    qsort(g->pos.boxes, g->level->boxes, sizeof(guint16), cell_compare);
    g->pushes++;
    if (pushed)
      *pushed = TRUE;
  } else
    g->walking++;
  g->pos.player = n;
  return TRUE;
}
gboolean game_solved(const Game *g) {
  for (unsigned i = 0; i < g->level->boxes; i++)
    if (!(g->level->terrain[g->pos.boxes[i]] & TILE_GOAL))
      return FALSE;
  return TRUE;
}
gboolean game_valid(const Game *g) {
  if (!g->level || !floor_at(g->level, g->pos.player) ||
      game_box(g, g->pos.player))
    return FALSE;
  for (unsigned i = 0; i < g->level->boxes; i++)
    if (!floor_at(g->level, g->pos.boxes[i]) ||
        (i && g->pos.boxes[i] <= g->pos.boxes[i - 1]))
      return FALSE;
  return TRUE;
}
void game_reachable(const Game *g, gboolean reach[MAX_CELLS],
                    int parent[MAX_CELLS], guint8 dirs[MAX_CELLS]) {
  memset(reach, 0, MAX_CELLS * sizeof *reach);
  unsigned queue[MAX_CELLS], head = 0, tail = 0;
  queue[tail++] = g->pos.player;
  reach[g->pos.player] = TRUE;
  if (parent)
    parent[g->pos.player] = -1;
  while (head < tail) {
    unsigned c = queue[head++];
    for (Direction d = UP; d <= RIGHT; d++) {
      int n = game_neighbor(g->level, c, d);
      if (!floor_at(g->level, n) || reach[n] || game_box(g, n))
        continue;
      reach[n] = TRUE;
      queue[tail++] = n;
      if (parent)
        parent[n] = c;
      if (dirs)
        dirs[n] = d;
    }
  }
}
MoveList game_pushes(const Game *g) {
  MoveList list = {0};
  gboolean reach[MAX_CELLS];
  game_reachable(g, reach, NULL, NULL);
  for (unsigned i = 0; i < g->level->boxes; i++)
    for (Direction d = UP; d <= RIGHT; d++) {
      unsigned box = g->pos.boxes[i];
      int behind = game_neighbor(g->level, box, d ^ 1),
          to = game_neighbor(g->level, box, d);
      if (behind >= 0 && reach[behind] && floor_at(g->level, to) &&
          !game_box(g, to))
        list.items[list.count++] = box * 4 + d;
    }
  return list;
}
GByteArray *game_push_path(const Game *g, unsigned move) {
  MoveList list = game_pushes(g);
  gboolean found = FALSE;
  for (unsigned i = 0; i < list.count; i++)
    found |= list.items[i] == move;
  if (!found)
    return NULL;
  gboolean reach[MAX_CELLS];
  int parent[MAX_CELLS];
  guint8 dirs[MAX_CELLS], reverse[MAX_CELLS];
  game_reachable(g, reach, parent, dirs);
  int c = game_neighbor(g->level, move / 4, (move % 4) ^ 1);
  unsigned length = 0;
  while (c != (int)g->pos.player) {
    reverse[length++] = dirs[c];
    c = parent[c];
  }
  GByteArray *path = g_byte_array_sized_new(length + 1);
  while (length)
    g_byte_array_append(path, &reverse[--length], 1);
  guint8 dir = move % 4;
  g_byte_array_append(path, &dir, 1);
  return path;
}
void game_analyze(const Level *l, Analysis *a) {
  memset(a, 0, sizeof *a);
  unsigned goal = 0;
  for (unsigned c = 0; c < l->width * l->height; c++)
    if (l->terrain[c] & TILE_GOAL)
      a->goals[goal++] = c;
  for (unsigned i = 0; i < goal; i++) {
    for (unsigned c = 0; c < MAX_CELLS; c++)
      a->distance[i][c] = G_MAXUINT16;
    unsigned queue[MAX_CELLS], head = 0, tail = 0;
    queue[tail++] = a->goals[i];
    a->distance[i][a->goals[i]] = 0;
    while (head < tail) {
      unsigned c = queue[head++];
      for (Direction d = UP; d <= RIGHT; d++) {
        int prev = game_neighbor(l, c, d),
            support = prev < 0 ? -1 : game_neighbor(l, prev, d);
        if (floor_at(l, prev) && floor_at(l, support) &&
            a->distance[i][prev] == G_MAXUINT16) {
          a->distance[i][prev] = a->distance[i][c] + 1;
          queue[tail++] = prev;
        }
      }
    }
  }
  for (unsigned c = 0; c < l->width * l->height; c++)
    if (l->terrain[c] & TILE_FLOOR) {
      a->dead[c] = TRUE;
      for (unsigned i = 0; i < goal; i++)
        if (a->distance[i][c] != G_MAXUINT16)
          a->dead[c] = FALSE;
    }
}
unsigned game_estimate(const Game *g, const Analysis *a, gboolean *impossible) {
  /* Hungarian minimum-cost perfect assignment; missing edges cost INF. */
  enum { INF = 1000000 };
  int u[MAX_BOXES + 1] = {0}, v[MAX_BOXES + 1] = {0}, p[MAX_BOXES + 1] = {0},
                    way[MAX_BOXES + 1] = {0};
  unsigned n = g->level->boxes;
  for (unsigned i = 1; i <= n; i++) {
    p[0] = i;
    unsigned j0 = 0;
    int minv[MAX_BOXES + 1];
    gboolean used[MAX_BOXES + 1] = {0};
    for (unsigned j = 1; j <= n; j++)
      minv[j] = INF * 4;
    do {
      used[j0] = TRUE;
      unsigned i0 = p[j0], j1 = 0;
      int delta = INF * 4;
      for (unsigned j = 1; j <= n; j++)
        if (!used[j]) {
          unsigned distance = a->distance[j - 1][g->pos.boxes[i0 - 1]];
          int cost = distance == G_MAXUINT16 ? INF : (int)distance;
          int current = cost - u[i0] - v[j];
          if (current < minv[j]) {
            minv[j] = current;
            way[j] = j0;
          }
          if (minv[j] < delta) {
            delta = minv[j];
            j1 = j;
          }
        }
      for (unsigned j = 0; j <= n; j++)
        if (used[j]) {
          u[p[j]] += delta;
          v[j] -= delta;
        } else if (j)
          minv[j] -= delta;
      j0 = j1;
    } while (p[j0]);
    do {
      unsigned j1 = way[j0];
      p[j0] = p[j1];
      j0 = j1;
    } while (j0);
  }
  *impossible = -v[0] >= INF;
  return *impossible ? 0 : (unsigned)-v[0];
}
gboolean game_deadlocked(const Game *g, const Analysis *a) {
  if (game_solved(g))
    return FALSE;
  for (unsigned i = 0; i < g->level->boxes; i++)
    if (a->dead[g->pos.boxes[i]])
      return TRUE;
  gboolean impossible;
  game_estimate(g, a, &impossible);
  if (impossible)
    return TRUE;
  for (unsigned y = 0; y + 1 < g->level->height; y++)
    for (unsigned x = 0; x + 1 < g->level->width; x++) {
      unsigned c = y * g->level->width + x,
               cells[] = {c, c + 1, c + g->level->width,
                          c + g->level->width + 1};
      gboolean full = TRUE, bad = FALSE;
      for (unsigned k = 0; k < 4; k++) {
        gboolean box = game_box(g, cells[k]);
        full &= !floor_at(g->level, cells[k]) || box;
        bad |= box && !(g->level->terrain[cells[k]] & TILE_GOAL);
      }
      if (full && bad)
        return TRUE;
    }
  return !game_pushes(g).count;
}
char *game_map(const Game *g, const char *separator) {
  GString *s = g_string_new(NULL);
  for (unsigned y = 0; y < g->level->height; y++) {
    if (y)
      g_string_append(s, separator);
    for (unsigned x = 0; x < g->level->width; x++) {
      unsigned c = y * g->level->width + x;
      gboolean goal = g->level->terrain[c] & TILE_GOAL;
      char ch = !(g->level->terrain[c] & TILE_FLOOR) ? '#'
                : game_box(g, c)                     ? (goal ? '*' : '$')
                : g->pos.player == c                 ? (goal ? '+' : '@')
                : goal                               ? '.'
                                                     : ' ';
      g_string_append_c(s, ch);
    }
  }
  return g_string_free(s, FALSE);
}
const char *player_name(PlayerKind p) {
  static const char *names[] = {"Human", "Random", "Greedy Best-first", "A*",
                                "Ollama"};
  return p <= PLAYER_OLLAMA ? names[p] : "Unknown";
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
