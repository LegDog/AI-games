// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include "i18n.h"
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
static Board boards[2];
static gsize initialized;
static int point_at(const Board *board, int row, int column)
{
    for (unsigned p = 0; p < board->points; p++) if (board->row[p] == row && board->column[p] == column) return p;
    return -1;
}
static void prepare_board(Board *board, BoardKind kind)
{
    board->name = kind == BOARD_ENGLISH ? "English" : "Triangular";
    for (int r = 0; r < (kind == BOARD_ENGLISH ? 7 : 5); r++) for (int c = 0; c < (kind == BOARD_ENGLISH ? 7 : r+1); c++) {
        if (kind == BOARD_ENGLISH && !(r >= 2 && r <= 4) && !(c >= 2 && c <= 4)) continue;
        unsigned p = board->points++;
        board->row[p] = r; board->column[p] = c;
        g_snprintf(board->labels[p], sizeof board->labels[p], "%c%d", 'a'+c, r+1);
        board->x[p] = kind == BOARD_ENGLISH ? 62 + c*63 : 251 + (c-r*.5)*91;
        board->y[p] = kind == BOARD_ENGLISH ? 62 + r*63 : 82 + r*78;
    }
    board->opening = kind == BOARD_ENGLISH ? point_at(board, 3, 3) : 0;
    board->full = POINT_BIT(board->points)-1;
    static const int english[4][2] = {{-1,0},{0,1},{1,0},{0,-1}};
    static const int triangle[6][2] = {{-1,0},{0,1},{1,1},{1,0},{0,-1},{-1,-1}};
    const int (*directions)[2] = kind == BOARD_ENGLISH ? english : triangle;
    unsigned count = kind == BOARD_ENGLISH ? 4 : 6;
    for (unsigned p = 0; p < board->points; p++) for (unsigned d = 0; d < count; d++) {
        int middle = point_at(board, board->row[p]+directions[d][0], board->column[p]+directions[d][1]);
        int destination = point_at(board, board->row[p]+2*directions[d][0], board->column[p]+2*directions[d][1]);
        if (middle >= 0) board->neighbors[p] |= POINT_BIT(middle);
        if (middle >= 0 && destination >= 0) board->jumps[board->choices++] = (Move) {p, middle, destination};
    }
    board->symmetries = kind == BOARD_ENGLISH ? 8 : 6;
    static const unsigned permutations[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    for (unsigned s = 0; s < board->symmetries; s++) {
        for (unsigned p = 0; p < board->points; p++) {
            int r = board->row[p], c = board->column[p];
            if (kind == BOARD_ENGLISH) {
                if (s >= 4) c = 6-c;
                for (unsigned turn = 0; turn < s%4; turn++) { int old = r; r = c; c = 6-old; }
            } else {
                int barycentric[3] = {r-c, c, 4-r};
                c = barycentric[permutations[s][1]];
                r = c + barycentric[permutations[s][0]];
            }
            board->transform[s][p] = point_at(board, r, c);
        }
        for (unsigned byte = 0; byte < 5; byte++) for (unsigned value = 0; value < 256; value++) {
            guint64 transformed = 0;
            for (unsigned bit = 0; bit < 8; bit++) if ((value & (1u << bit)) && byte*8+bit < board->points)
                transformed |= POINT_BIT(board->transform[s][byte*8+bit]);
            board->transform_bytes[s][byte][value] = transformed;
        }
    }
}
const Board *game_board(BoardKind kind)
{
    if (g_once_init_enter(&initialized)) { prepare_board(&boards[0], BOARD_ENGLISH); prepare_board(&boards[1], BOARD_TRIANGLE); g_once_init_leave(&initialized, 1); }
    return &boards[kind == BOARD_TRIANGLE ? 1 : 0];
}
gboolean game_init(Game *game, BoardKind board, GoalKind goal)
{
    if ((board != BOARD_ENGLISH && board != BOARD_TRIANGLE) || (goal != GOAL_ANY && goal != GOAL_CENTER) ||
        (board == BOARD_TRIANGLE && goal == GOAL_CENTER)) return FALSE;
    const Board *b = game_board(board);
    *game = (Game) {.board = board, .goal = goal, .occupied = b->full ^ POINT_BIT(b->opening)};
    return TRUE;
}
unsigned game_count(guint64 mask) { unsigned n = 0; for (; mask; mask &= mask-1) n++; return n; }
int game_point(const Board *board, const char *label)
{
    for (unsigned p = 0; p < board->points; p++) if (g_strcmp0(label, board->labels[p]) == 0) return p;
    return -1;
}
int game_find_move(const Board *board, unsigned source, unsigned destination)
{
    for (unsigned m = 0; m < board->choices; m++) if (board->jumps[m].source == source && board->jumps[m].destination == destination) return m;
    return -1;
}
gboolean game_legal(const Game *game, unsigned move)
{
    const Board *b = game_board(game->board);
    if (move >= b->choices || game->moves >= MAX_JUMPS) return FALSE;
    Move m = b->jumps[move]; guint64 required = POINT_BIT(m.source) | POINT_BIT(m.middle);
    return (game->occupied & required) == required && !(game->occupied & POINT_BIT(m.destination));
}
MoveList game_legal_moves(const Game *game)
{
    MoveList moves = {0}; const Board *board = game_board(game->board);
    for (unsigned m = 0; m < board->choices; m++) if (game_legal(game, m)) moves.items[moves.count++] = m;
    return moves;
}
GameResult game_result(const Game *game)
{
    if (game_count(game->occupied) == 1) return game->goal == GOAL_ANY || game->occupied == POINT_BIT(game_board(game->board)->opening) ? GAME_SOLVED : GAME_WRONG_FINISH;
    return game_legal_moves(game).count ? GAME_ONGOING : GAME_STUCK;
}
gboolean game_play(Game *game, unsigned move)
{
    if (!game_legal(game, move)) return FALSE;
    Move m = game_board(game->board)->jumps[move];
    game->occupied ^= POINT_BIT(m.source) | POINT_BIT(m.middle) | POINT_BIT(m.destination);
    game->history[game->moves++] = move;
    return TRUE;
}
gboolean game_undo(Game *game)
{
    if (!game->moves) return FALSE;
    Move m = game_board(game->board)->jumps[game->history[--game->moves]];
    game->occupied ^= POINT_BIT(m.source) | POINT_BIT(m.middle) | POINT_BIT(m.destination);
    return TRUE;
}
int game_random_move(const MoveList *moves, GRand *random)
{
    return moves->count ? (int) moves->items[g_rand_int_range(random, 0, moves->count)] : -1;
}
gint64 game_position_heuristic(const Game *game)
{
    const Board *b = game_board(game->board);
    if (game_count(game->occupied) == 1 && (game->goal == GOAL_ANY || game->occupied == POINT_BIT(b->opening))) return G_GINT64_CONSTANT(1000000000000000);
    unsigned isolated = 0; int distance = 0;
    for (unsigned p = 0; p < b->points; p++) if (game->occupied & POINT_BIT(p)) {
        isolated += !(b->neighbors[p] & game->occupied);
        distance += (int) hypot(b->x[p]-251, b->y[p]-(game->board == BOARD_ENGLISH ? 251 : 290));
    }
    return (gint64) game_legal_moves(game).count*1000000000 - (gint64) isolated*1000000 - distance;
}
gint64 game_heuristic(const Game *game, unsigned move)
{
    Game next = *game;
    return game_play(&next, move) ? game_position_heuristic(&next) : G_MININT64;
}
guint64 game_canonical(const Game *game)
{
    const Board *b = game_board(game->board); guint64 best = game->occupied;
    for (unsigned s = 1; s < b->symmetries; s++) {
        guint64 transformed = 0, mask = game->occupied;
        for (unsigned byte = 0; byte < 5; byte++, mask >>= 8) transformed |= b->transform_bytes[s][byte][mask & 255];
        best = MIN(best, transformed);
    }
    return best;
}
const char *player_name(PlayerKind kind)
{
    static const char * const names[] = {"Human", "Random", "Heuristic", "Backtracking", "A*", "Ollama"};
    return kind <= PLAYER_OLLAMA ? names[kind] : "Unknown";
}
const char *game_result_name(GameResult result)
{
    static const char * const names[] = {"ongoing", "solved", "stuck", "wrong-finish"}; return names[result];
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
