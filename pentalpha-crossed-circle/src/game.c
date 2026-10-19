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
#include "board.h"
#include "game-data.inc"
void game_init(Game *game) { memset(game, 0, sizeof *game); }
unsigned game_count(guint64 mask)
{
    unsigned count = 0;
    for (; mask; mask &= mask - 1) count++;
    return count;
}
gboolean game_legal(const Game *game, unsigned move)
{
    return move < GAME_CHOICES && game->moves < GAME_MAX_DEPTH &&
        !(game->occupied & (POINT_BIT(game_moves[move].entry) | POINT_BIT(game_moves[move].destination)));
}
gboolean move_list_contains(const MoveList *list, unsigned move)
{
    for (unsigned i=0;i<list->count;i++) if (list->items[i]==move) return TRUE;
    return FALSE;
}
MoveList game_legal_moves(const Game *game)
{
    MoveList list={0};
    for (unsigned m=0;m<GAME_CHOICES;m++) if (game_legal(game,m)) list.items[list.count++]=m;
    return list;
}
int game_result(const Game *game)
{
    return game->moves == GAME_MAX_DEPTH ? 1 : game_legal_moves(game).count ? 0 : 2;
}
gboolean game_play(Game *game, unsigned move)
{
    if (!game_legal(game, move)) return FALSE;
    game->history[game->moves++] = game_moves[move];
    game->occupied |= POINT_BIT(game_moves[move].destination);
    return TRUE;
}
gboolean game_undo(Game *game)
{
    if (!game->moves) return FALSE;
    game->occupied &= ~(POINT_BIT(game->history[--game->moves].destination));
    return TRUE;
}
int game_find_move(unsigned entry, unsigned destination)
{
    for (unsigned m = 0; m < GAME_CHOICES; m++)
        if (game_moves[m].entry == entry && game_moves[m].destination == destination) return (int) m;
    return -1;
}
int game_point(const char *label)
{
    for (unsigned p = 0; p < GAME_POINTS; p++) if (g_strcmp0(label, game_labels[p]) == 0) return (int) p;
    return -1;
}
int game_random_move(MoveList list, GRand *random)
{
    return list.count ? (int)list.items[g_rand_int_range(random,0,list.count)] : -1;
}
int game_heuristic(const Game *game, unsigned move)
{
    Game next = *game;
    if (!game_play(&next, move)) return -1;
    return (int) next.moves * 256 + (int) game_legal_moves(&next).count;
}
const char *player_name(PlayerKind kind)
{
    static const char * const names[] = {"Human", "Random", "Heuristic", "Search", "Ollama", "Connectivity", "Spanning Forest", "Gauss–Jordan GF(2)", "Search — sibling symmetry pruning"};
    return kind < PLAYER_COUNT ? names[kind] : "Unknown";
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

static guint64 transforms[10][5][256];
static gsize symmetries_ready;
guint64 game_canonical(guint64 occupied)
{
    if (g_once_init_enter(&symmetries_ready)) {
        for (unsigned symmetry=0;symmetry<10;symmetry++) {
            int sign=symmetry<5?1:-1, k=symmetry%5;
            for (unsigned byte=0;byte<5;byte++) for (unsigned value=0;value<256;value++) {
                guint64 out=0;
                for (unsigned bit=0;bit<8;bit++) {
                    unsigned p=byte*8+bit; if (p>=GAME_POINTS || !(value&(1u<<bit))) continue;
                    unsigned dest=p;
                    if (p<5) dest=(sign*(int)p+k+10)%5;
                    else if (p<25) dest=5+(sign*((int)p-5)+4*k+40)%20;
                    else if (p<35) dest=25+(sign*((int)p-25)+2*k+20)%10;
                    out|=POINT_BIT(dest);
                }
                transforms[symmetry][byte][value]=out;
            }
        }
        g_once_init_leave(&symmetries_ready,1);
    }
    guint64 best=occupied;
    for (unsigned s=0;s<10;s++) {
        guint64 out=0;
        for (unsigned b=0;b<5;b++) out|=transforms[s][b][(occupied>>(8*b))&255];
        best=MIN(best,out);
    }
    return best;
}
