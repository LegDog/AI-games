#define _POSIX_C_SOURCE 200809L
#include "game.h"

#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static const unsigned lines[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
    {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}
};

static int cell_at(const Game *game, unsigned cell)
{
    return game->cells[cell / 3][cell % 3];
}

void game_init(Game *game)
{
    memset(game, 0, sizeof *game);
    game->turn = 1;
}

char game_symbol(int player)
{
    return player == 1 ? 'X' : player == -1 ? 'O' : '.';
}

const unsigned *game_winning_line(const Game *game)
{
    for (unsigned i = 0; i < G_N_ELEMENTS(lines); i++) {
        int a = cell_at(game, lines[i][0]);
        if (a && a == cell_at(game, lines[i][1]) && a == cell_at(game, lines[i][2]))
            return lines[i];
    }
    return NULL;
}

int game_result(const Game *game)
{
    const unsigned *winning = game_winning_line(game);
    if (winning)
        return cell_at(game, winning[0]);
    for (unsigned cell = 0; cell < 9; cell++)
        if (!cell_at(game, cell))
            return 0;
    return 2;
}

gboolean game_play(Game *game, unsigned cell)
{
    if (cell >= 9 || game_result(game) || cell_at(game, cell))
        return FALSE;
    game->cells[cell / 3][cell % 3] = game->turn;
    game->moves++;
    game->turn = -game->turn;
    return TRUE;
}

/* Bit mask counts distinct winning destinations, rather than lines: two
 * winning lines sharing one empty square are only one threat. */
static unsigned winning_cells(const Game *game, int player)
{
    unsigned mask = 0;
    for (unsigned i = 0; i < G_N_ELEMENTS(lines); i++) {
        unsigned own = 0, empty = 0, destination = 0;
        for (unsigned j = 0; j < 3; j++) {
            unsigned cell = lines[i][j];
            int piece = cell_at(game, cell);
            own += piece == player;
            if (!piece) {
                empty++;
                destination = cell;
            }
        }
        if (own == 2 && empty == 1)
            mask |= 1u << destination;
    }
    return mask;
}

static gboolean multiple_threats(unsigned mask)
{
    return mask && (mask & (mask - 1));
}

static void evaluate_forks(const Game *game, unsigned cell, Score *score)
{
    Game next = *game;
    next.cells[cell / 3][cell % 3] = game->turn;
    if (game_winning_line(&next))
        return;
    score->immediate_loss = winning_cells(&next, -game->turn) != 0;
    score->fork = multiple_threats(winning_cells(&next, game->turn));
    if (score->immediate_loss || score->fork)
        return;
    for (unsigned reply = 0; reply < 9; reply++) {
        if (cell_at(&next, reply))
            continue;
        Game response = next;
        response.cells[reply / 3][reply % 3] = -game->turn;
        /* An apparent opponent fork is harmless if we can win immediately
         * instead of defending it. This also handles forcing counter-threats. */
        if (!winning_cells(&response, game->turn) &&
            multiple_threats(winning_cells(&response, -game->turn)))
            score->opponent_forks++;
    }
}

void game_evaluate(const Game *game, GRand *random, Score scores[9])
{
    memset(scores, 0, sizeof(Score) * 9);
    for (unsigned cell = 0; cell < 9; cell++) {
        if (cell_at(game, cell))
            continue;
        Score *score = &scores[cell];
        score->empty = TRUE;
        for (unsigned i = 0; i < G_N_ELEMENTS(lines); i++) {
            gboolean contains = FALSE;
            unsigned own = 0, opponent = 0;
            for (unsigned j = 0; j < 3; j++) {
                unsigned other = lines[i][j];
                contains |= other == cell;
                own += cell_at(game, other) == game->turn;
                opponent += cell_at(game, other) == -game->turn;
            }
            if (game->moves > 0 && contains && !opponent)
                score->open_lines++;
            if (!contains || (own && opponent))
                continue;
            score->wins += own == 2;
            score->blocks += opponent == 2;
            score->own_lines += own == 1;
            score->opponent_lines += opponent == 1;
        }
        score->center_bonus = cell == 4 && game->moves > 0 ? 5 : 0;
        evaluate_forks(game, cell, score);
        score->random = g_rand_double_range(random, 0.0, 0.1);
        score->total = 1000.0 * score->wins + 100.0 * score->blocks +
                       10.0 * score->own_lines + score->opponent_lines +
                       score->center_bonus + 50.0 * score->fork -
                       (score->opponent_forks ? 50.0 : 0.0) -
                       500.0 * score->immediate_loss +
                       0.2 * score->open_lines + score->random;
    }
}

int game_best_move(const Score scores[9])
{
    int best = -1;
    for (int cell = 0; cell < 9; cell++)
        if (scores[cell].empty && (best == -1 || scores[cell].total > scores[best].total))
            best = cell;
    return best;
}

static void record_board(GString *text, const Game *game)
{
    g_string_append(text, "```text\n");
    for (unsigned row = 0; row < 3; row++)
        g_string_append_printf(text, "%c %c %c\n", game_symbol(game->cells[row][0]),
                               game_symbol(game->cells[row][1]), game_symbol(game->cells[row][2]));
    g_string_append(text, "```\n\n");
}

void game_record(GString *history, const Game *before, const Score scores[9],
                 unsigned cell, PlayerKind controller)
{
    g_string_append_printf(history, "### Jogada %u — %c (%s)\n\nTabuleiro anterior:\n\n",
                           before->moves + 1, game_symbol(before->turn),
                           player_kind_name(controller));
    record_board(history, before);
    g_string_append(history,
        "| Tecla | Linha | Coluna | Vitória ×1000 | Bloqueio ×100 | Própria ×10 | Adversária ×1 | Bônus centro | Garfo ×50 | Respostas com garfo (−50 se houver) | Derrota imediata ×−500 | Linhas abertas ×0,2 | Aleatório | Total |\n"
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |\n");
    for (unsigned i = 0; i < 9; i++) {
        const Score *s = &scores[i];
        if (!s->empty)
            continue;
        char random[G_ASCII_DTOSTR_BUF_SIZE], total[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_dtostr(random, sizeof random, s->random);
        g_ascii_dtostr(total, sizeof total, s->total);
        g_string_append_printf(history, "| %u | %u | %u | %u | %u | %u | %u | %u | %u | %u | %u | %u | %s | %s |\n",
            7 - (i / 3) * 3 + i % 3, i / 3 + 1, i % 3 + 1,
            s->wins, s->blocks, s->own_lines, s->opponent_lines, s->center_bonus,
            s->fork, s->opponent_forks, s->immediate_loss, s->open_lines, random, total);
    }
    g_string_append_printf(history, "\nEscolha: tecla **%u**, linha %u, coluna %u.\n\n",
        7 - (cell / 3) * 3 + cell % 3, cell / 3 + 1, cell % 3 + 1);
}

char *game_report(const Game *game, const char *history, const char *configuration,
                  GDateTime *finished_at)
{
    int result = game_result(game);
    if (result != 1 && result != -1)
        return NULL;
    char *timestamp = g_date_time_format(finished_at, "%Y-%m-%d %H:%M:%S %:z");
    GString *report = g_string_new("\n## Partida — ");
    g_string_append_printf(report, "%s\n\n%s\n\nVencedor: **%c**.\n\n%s",
                           timestamp, configuration, game_symbol(result), history);
    g_string_append(report, "### Tabuleiro final\n\n");
    record_board(report, game);
    g_free(timestamp);
    return g_string_free(report, FALSE);
}

gboolean game_append_report(const char *path, const char *report, GError **error)
{
    int fd = g_open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
    if (fd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Não foi possível abrir %s: %s", path, g_strerror(errno));
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
                    "Não foi possível gravar %s: %s%s", path, g_strerror(saved_errno),
                    rollback_failed ? ". Falha ao remover gravação parcial; confira o arquivo antes de tentar novamente" : "");
    return saved_errno == 0;
}
