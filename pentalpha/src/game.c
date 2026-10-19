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
const char * const game_labels[10] = {"v1", "v2", "v3", "v4", "v5", "i1", "i2", "i3", "i4", "i5"};
const unsigned game_lines[5][4] = {{0,5,6,2}, {2,7,8,4}, {4,9,5,1}, {1,6,7,3}, {3,8,9,0}};
#define SEGMENTS(a,b,c,d) {a,b,c}, {c,b,a}, {b,c,d}, {d,c,b}
const Move game_moves[20] = {
    SEGMENTS(0,5,6,2), SEGMENTS(2,7,8,4), SEGMENTS(4,9,5,1),
    SEGMENTS(1,6,7,3), SEGMENTS(3,8,9,0)
};
#undef SEGMENTS
void game_init(Game *game) { memset(game, 0, sizeof *game); }
unsigned game_count(guint32 mask)
{
    unsigned count = 0;
    for (; mask; mask &= mask - 1) count++;
    return count;
}
gboolean game_legal(const Game *game, unsigned move)
{
    return move < 20 && game->moves < 9 &&
        !(game->occupied & ((1u << game_moves[move].entry) | (1u << game_moves[move].destination)));
}
guint32 game_legal_moves(const Game *game)
{
    guint32 mask = 0;
    for (unsigned m = 0; m < 20; m++) if (game_legal(game, m)) mask |= 1u << m;
    return mask;
}
int game_result(const Game *game)
{
    return game->moves == 9 ? 1 : game_legal_moves(game) ? 0 : 2;
}
gboolean game_play(Game *game, unsigned move)
{
    if (!game_legal(game, move)) return FALSE;
    game->history[game->moves++] = game_moves[move];
    game->occupied |= 1u << game_moves[move].destination;
    return TRUE;
}
gboolean game_undo(Game *game)
{
    if (!game->moves) return FALSE;
    game->occupied &= ~(1u << game->history[--game->moves].destination);
    return TRUE;
}
int game_find_move(unsigned entry, unsigned destination)
{
    for (unsigned m = 0; m < 20; m++)
        if (game_moves[m].entry == entry && game_moves[m].destination == destination) return (int) m;
    return -1;
}
int game_point(const char *label)
{
    for (unsigned p = 0; p < 10; p++) if (g_strcmp0(label, game_labels[p]) == 0) return (int) p;
    return -1;
}
int game_random_move(guint32 mask, GRand *random)
{
    if (!mask) return -1;
    unsigned index = g_rand_int_range(random, 0, game_count(mask));
    for (unsigned m = 0; m < 20; m++) if (mask & (1u << m)) { if (!index--) return (int) m; }
    return -1;
}
int game_heuristic(const Game *game, unsigned move)
{
    Game next = *game;
    if (!game_play(&next, move)) return -1;
    return (int) next.moves * 100 + (int) game_count(game_legal_moves(&next));
}
const char *player_name(PlayerKind kind)
{
    static const char * const names[] = {"Human", "Random", "Heuristic", "Search", "Ollama"};
    return kind <= PLAYER_OLLAMA ? names[kind] : "Unknown";
}

gboolean game_append_report(const char *path, const char *report, GError **error)
{
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
                    rollback_failed ? _(". Could not remove the partial write; check the file before retrying") : "");
    return saved_errno == 0;
}
