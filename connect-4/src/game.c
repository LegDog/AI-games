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

static const int directions[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
static const double window_weights[5] = {0, 10, 100, 1000, 10000};

static gboolean inside(int row, int col)
{
    return row >= 0 && row < GAME_ROWS && col >= 0 && col < GAME_COLS;
}

void game_init(Game *game)
{
    memset(game, 0, sizeof *game);
    game->turn = 1;
}

char game_symbol(int player)
{
    return player == 1 ? 'V' : player == -1 ? 'A' : '.';
}

const char *game_player_name(int player)
{
    return player == 1 ? "Vermelho" : "Amarelo";
}

int game_drop_row(const Game *game, unsigned column)
{
    if (column >= GAME_COLS)
        return -1;
    for (int row = GAME_ROWS - 1; row >= 0; row--)
        if (!game->cells[row][column])
            return row;
    return -1;
}

static gboolean wins_at(const Game *game, int row, int col, int player)
{
    for (unsigned d = 0; d < G_N_ELEMENTS(directions); d++) {
        int count = 1;
        for (int sign = -1; sign <= 1; sign += 2) {
            int dr = sign * directions[d][0], dc = sign * directions[d][1];
            int r = row + dr, c = col + dc;
            while (inside(r, c) && game->cells[r][c] == player) {
                count++;
                r += dr;
                c += dc;
            }
        }
        if (count >= 4)
            return TRUE;
    }
    return FALSE;
}

unsigned game_winning_lines(const Game *game, WinningLine output[GAME_MAX_LINES])
{
    unsigned count = 0;
    for (int row = 0; row < GAME_ROWS; row++) {
        for (int col = 0; col < GAME_COLS; col++) {
            int player = game->cells[row][col];
            if (!player)
                continue;
            for (unsigned d = 0; d < G_N_ELEMENTS(directions); d++) {
                int dr = directions[d][0], dc = directions[d][1];
                int previous_row = row - dr, previous_col = col - dc;
                if (inside(previous_row, previous_col) &&
                    game->cells[previous_row][previous_col] == player)
                    continue;
                int r = row, c = col, length = 1;
                while (inside(r + dr, c + dc) && game->cells[r + dr][c + dc] == player) {
                    r += dr;
                    c += dc;
                    length++;
                }
                if (length >= 4) {
                    if (output)
                        output[count] = (WinningLine) {row, col, r, c};
                    count++;
                }
            }
        }
    }
    return count;
}

int game_result(const Game *game)
{
    for (int row = 0; row < GAME_ROWS; row++)
        for (int col = 0; col < GAME_COLS; col++)
            if (game->cells[row][col] && wins_at(game, row, col, game->cells[row][col]))
                return game->cells[row][col];
    for (unsigned col = 0; col < GAME_COLS; col++)
        if (game_drop_row(game, col) >= 0)
            return 0;
    return 2;
}

gboolean game_play(Game *game, unsigned column)
{
    int row = game_drop_row(game, column);
    if (row < 0 || game_result(game))
        return FALSE;
    game->cells[row][column] = game->turn;
    game->moves++;
    game->turn = -game->turn;
    return TRUE;
}

static unsigned winning_columns(const Game *game, int player)
{
    unsigned mask = 0;
    for (unsigned col = 0; col < GAME_COLS; col++) {
        int row = game_drop_row(game, col);
        if (row < 0)
            continue;
        Game next = *game;
        next.cells[row][col] = player;
        if (wins_at(&next, row, col, player))
            mask |= 1u << col;
    }
    return mask;
}

static gboolean multiple_threats(unsigned mask)
{
    return mask && (mask & (mask - 1));
}

/* One forced block can provide the support for another winning disc in the
 * same column. Do not call this forced if the defender can win instead. */
static gboolean stacked_threat(const Game *game, int attacker, unsigned threats)
{
    if (!threats || winning_columns(game, -attacker))
        return FALSE;
    for (unsigned col = 0; col < GAME_COLS; col++) {
        if (!(threats & (1u << col)))
            continue;
        Game blocked = *game;
        int row = game_drop_row(game, col);
        blocked.cells[row][col] = -attacker;
        if (winning_columns(&blocked, attacker) & (1u << col))
            return TRUE;
    }
    return FALSE;
}

/* Winning targets regardless of missing support. These guide preparatory
 * moves only; an inaccessible target is never itself proof of a win. */
static guint64 winning_targets(const Game *game, int player)
{
    guint64 targets = 0;
    for (unsigned row = 0; row < GAME_ROWS; row++) {
        for (unsigned col = 0; col < GAME_COLS; col++) {
            if (game->cells[row][col])
                continue;
            Game next = *game;
            next.cells[row][col] = player;
            if (wins_at(&next, row, col, player))
                targets |= G_GUINT64_CONSTANT(1) << (row * GAME_COLS + col);
        }
    }
    return targets;
}

/* With preparations == 0, prove only continuous forcing attacks. Otherwise
 * also try moves that create a winning target or support an existing target.
 * A defender without an immediate threat may choose ANY legal reply: every
 * reply must be proved losing, including counterattacks and column fills.
 * FALSE means no proof within the limits, not that the position is safe. */
static gboolean forcing_win(const Game *game, int attacker, int player,
                            unsigned depth, unsigned preparations, unsigned *budget)
{
    if (!depth || !*budget)
        return FALSE;
    --*budget;
    unsigned attack = winning_columns(game, attacker);
    if (player == attacker) {
        if (attack)
            return TRUE;
        if (depth < 3)
            return FALSE;
        for (unsigned col = 0; col < GAME_COLS; col++) {
            int row = game_drop_row(game, col);
            if (row < 0)
                continue;
            Game next = *game;
            next.cells[row][col] = attacker;
            if (winning_columns(&next, attacker) &&
                forcing_win(&next, attacker, -attacker, depth - 1, preparations, budget))
                return TRUE;
        }
        if (!preparations || !*budget)
            return FALSE;
        guint64 targets = winning_targets(game, attacker);
        for (unsigned col = 0; col < GAME_COLS; col++) {
            int row = game_drop_row(game, col);
            if (row < 0)
                continue;
            Game next = *game;
            next.cells[row][col] = attacker;
            if (winning_columns(&next, attacker))
                continue; /* Already tried the forcing moves above. */
            guint64 supported = 0;
            for (int above = 0; above < row; above++)
                supported |= G_GUINT64_CONSTANT(1) << (above * GAME_COLS + col);
            if (!(targets & supported) && !(winning_targets(&next, attacker) & ~targets))
                continue;
            if (forcing_win(&next, attacker, -attacker, depth - 1, preparations - 1, budget))
                return TRUE;
            if (!*budget)
                break;
        }
        return FALSE;
    }
    if (depth < 2 || winning_columns(game, -attacker))
        return FALSE;
    if (!attack) {
        /* Also check free replies after the last allowed preparation. */
        gboolean has_reply = FALSE;
        for (unsigned col = 0; col < GAME_COLS; col++) {
            int row = game_drop_row(game, col);
            if (row < 0)
                continue;
            has_reply = TRUE;
            Game reply = *game;
            reply.cells[row][col] = -attacker;
            if (!forcing_win(&reply, attacker, attacker, depth - 1, preparations, budget))
                return FALSE;
        }
        return has_reply;
    }
    if (multiple_threats(attack))
        return TRUE;
    unsigned col = 0;
    while (!(attack & (1u << col)))
        col++;
    Game blocked = *game;
    int row = game_drop_row(game, col);
    blocked.cells[row][col] = -attacker;
    return forcing_win(&blocked, attacker, attacker, depth - 1, preparations, budget);
}

static void evaluate_forcing(const Game *next, int player, Score *score)
{
    if (score->win)
        return;
    /* Reuse short proofs instead of letting long branches consume the budget
     * before checking an already known immediate loss, fork or stack. */
    score->forcing_loss = score->immediate_loss || score->opponent_forks;
    score->forcing_win = !score->immediate_loss && (score->fork || score->stacked);
    if (!score->forcing_loss && !score->forcing_win) {
        unsigned budget = FORCING_NODE_BUDGET;
        score->forcing_loss = forcing_win(next, -player, -player, FORCING_MAX_PLIES, 0, &budget);
        score->forcing_limited |= budget == 0;
        if (!score->forcing_loss) {
            budget = FORCING_NODE_BUDGET;
            score->forcing_win = forcing_win(next, player, -player, FORCING_MAX_PLIES, 0, &budget);
            score->forcing_limited |= budget == 0;
        }
    }
    if (!score->forcing_loss && !score->forcing_win) {
        unsigned budget = PREPARATION_NODE_BUDGET;
        score->prepared_loss = forcing_win(next, -player, -player, FORCING_MAX_PLIES,
                                          PREPARATION_MAX_MOVES, &budget);
        score->forcing_limited |= budget == 0;
        score->forcing_loss = score->prepared_loss;
        if (!score->forcing_loss) {
            budget = PREPARATION_NODE_BUDGET;
            score->prepared_win = forcing_win(next, player, -player, FORCING_MAX_PLIES,
                                             PREPARATION_MAX_MOVES, &budget);
            score->forcing_limited |= budget == 0;
            score->forcing_win = score->prepared_win;
        }
    }
}

static void evaluate_tactics(const Game *game, unsigned col, unsigned threats, Score *score)
{
    Game next = *game;
    next.cells[score->row][col] = game->turn;
    score->win = wins_at(&next, score->row, col, game->turn);
    score->block = (threats & (1u << col)) != 0;
    if (score->win)
        return;
    score->immediate_loss = winning_columns(&next, -game->turn) != 0;
    unsigned own_threats = winning_columns(&next, game->turn);
    score->fork = multiple_threats(own_threats);
    score->stacked = stacked_threat(&next, game->turn, own_threats);
    if (score->immediate_loss || score->fork || score->stacked)
        return;
    for (unsigned reply = 0; reply < GAME_COLS; reply++) {
        int row = game_drop_row(&next, reply);
        if (row < 0)
            continue;
        Game response = next;
        response.cells[row][reply] = -game->turn;
        if (winning_columns(&response, game->turn))
            continue;
        unsigned enemy_threats = winning_columns(&response, -game->turn);
        gboolean stacked = stacked_threat(&response, -game->turn, enemy_threats);
        score->opponent_stacked += stacked;
        if (multiple_threats(enemy_threats) || stacked)
            score->opponent_forks++;
    }
}

/* Count distinct missing supports outside the window. Empty cells within a
 * vertical window support each other as that sequence is developed. */
static unsigned window_supports(const Game *game, int row, int col, int dr, int dc)
{
    guint64 window = 0, supports = 0;
    for (int step = 0; step < 4; step++)
        window |= G_GUINT64_CONSTANT(1) << ((row + step * dr) * GAME_COLS + col + step * dc);
    for (int step = 0; step < 4; step++) {
        int r = row + step * dr, c = col + step * dc;
        if (game->cells[r][c])
            continue;
        for (int below = r + 1; below < GAME_ROWS; below++)
            if (!game->cells[below][c])
                supports |= G_GUINT64_CONSTANT(1) << (below * GAME_COLS + c);
    }
    supports &= ~window;
    unsigned count = 0;
    while (supports) {
        count++;
        supports &= supports - 1;
    }
    return count;
}

void game_position_evaluate(const Game *game, int player, PositionValue *value)
{
    memset(value, 0, sizeof *value);
    for (int row = 0; row < GAME_ROWS; row++) {
        for (int col = 0; col < GAME_COLS; col++) {
            for (unsigned d = 0; d < G_N_ELEMENTS(directions); d++) {
                int dr = directions[d][0], dc = directions[d][1];
                if (!inside(row + 3 * dr, col + 3 * dc))
                    continue;
                unsigned own = 0, opponent = 0;
                for (int step = 0; step < 4; step++) {
                    int piece = game->cells[row + step * dr][col + step * dc];
                    own += piece == player;
                    opponent += piece == -player;
                }
                if (own && opponent)
                    continue;
                if (!own && !opponent) {
                    value->empty_windows++;
                    continue;
                }
                unsigned supports = window_supports(game, row, col, dr, dc);
                if (own) {
                    value->own_windows[own]++;
                    value->own_potential += window_weights[own] / (1.0 + supports);
                    value->own_ready += supports == 0;
                    value->own_supported += supports > 0;
                } else {
                    value->opponent_windows[opponent]++;
                    value->opponent_potential += window_weights[opponent] / (1.0 + supports);
                    value->opponent_ready += supports == 0;
                    value->opponent_supported += supports > 0;
                }
            }
        }
    }
    value->total = SCORE_POSITIONAL_SCALE *
        (value->own_potential - SCORE_DEFENSE_FACTOR * value->opponent_potential);
}

void game_evaluate(const Game *game, GRand *random, Score scores[GAME_COLS])
{
    memset(scores, 0, sizeof(Score) * GAME_COLS);
    gboolean finished = game_result(game) != 0;
    unsigned threats = finished ? 0 : winning_columns(game, -game->turn);
    for (unsigned col = 0; col < GAME_COLS; col++) {
        Score *score = &scores[col];
        score->row = game_drop_row(game, col);
        if (finished || score->row < 0)
            continue;
        score->legal = TRUE;
        if (game->moves > 0) {
            Game next = *game;
            next.cells[score->row][col] = game->turn;
            game_position_evaluate(&next, game->turn, &score->position);
            evaluate_tactics(game, col, threats, score);
            evaluate_forcing(&next, game->turn, score);
        }
        score->random = g_rand_double_range(random, 0.0, 0.1);
        score->total = SCORE_WIN * score->win + SCORE_BLOCK * score->block +
            SCORE_FORK * (score->fork || score->stacked) -
            (score->opponent_forks ? SCORE_FORK_RISK : 0) -
            SCORE_IMMEDIATE_LOSS * score->immediate_loss +
            SCORE_FORCING_WIN * score->forcing_win - SCORE_FORCING_LOSS * score->forcing_loss +
            score->position.total + score->random;
    }
}

int game_best_move(const Score scores[GAME_COLS])
{
    int best = -1;
    for (int col = 0; col < GAME_COLS; col++)
        if (scores[col].legal && (best < 0 || scores[col].total > scores[best].total))
            best = col;
    return best;
}

static void record_board(GString *text, const Game *game)
{
    g_string_append(text, "```text\n1 2 3 4 5 6 7\n");
    for (unsigned row = 0; row < GAME_ROWS; row++) {
        for (unsigned col = 0; col < GAME_COLS; col++)
            g_string_append_printf(text, "%c%c", game_symbol(game->cells[row][col]),
                                   col == GAME_COLS - 1 ? '\n' : ' ');
    }
    g_string_append(text, "```\n\n");
}

void game_record(GString *history, const Game *before, const Score scores[GAME_COLS],
                 unsigned column, PlayerKind controller)
{
    g_string_append_printf(history, "### Jogada %u — %s (%s)\n\nTabuleiro anterior (V: vermelho; A: amarelo):\n\n",
        before->moves + 1, game_player_name(before->turn), player_kind_name(controller));
    record_board(history, before);
    g_string_append(history,
        "Avaliação global após a queda: pesos 10/100/1000/10000 para 1/2/3/4 peças, "
        "divididos por (1 + apoios ausentes fora da janela). "
        "Posicional = 0,1 × (potencial próprio − 1,25 × adversário). "
        "Na abertura, componentes estratégicos zerados.\n\n"
        "Garfo ou empilhamento próprio: +1e5 uma vez; alguma resposta perigosa: −1e5 uma vez.\n\n"
        "Sequências forçadas: +1e6 por vitória própria e −2e7 por derrota demonstrada; "
        "até 12 meias-jogadas e 1024 nós na busca contínua; extensão com até 2 jogadas "
        "preparatórias e 4096 nós por análise. Preparações criam alvos de vitória ou fornecem "
        "apoio; todas as respostas livres do defensor são verificadas. "
        "Limite atingido não significa segurança.\n\n"
        "| Coluna | Linha de queda | Vazias | Próprias 1/2/3/4 | Adversárias 1/2/3/4 | Próprias sem/com apoio ausente | Adversárias sem/com apoio ausente | Potencial próprio | Potencial adversário | Vitória ×1e9 | Bloqueio ×1e7 | Garfo | Empilhamento | Respostas com garfo/empilhamento | Respostas com empilhamento | Derrota imediata ×−1e8 | Vitória forçada ×1e6 | Derrota forçada ×−2e7 | Vitória com preparação | Derrota com preparação | Limite de nós atingido | Posicional | Aleatório | Total |\n"
        "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |\n");
    for (unsigned col = 0; col < GAME_COLS; col++) {
        const Score *s = &scores[col];
        if (!s->legal)
            continue;
        const PositionValue *v = &s->position;
        char random[G_ASCII_DTOSTR_BUF_SIZE], total[G_ASCII_DTOSTR_BUF_SIZE];
        char positional[G_ASCII_DTOSTR_BUF_SIZE], own[G_ASCII_DTOSTR_BUF_SIZE];
        char opponent[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_dtostr(random, sizeof random, s->random);
        g_ascii_dtostr(total, sizeof total, s->total);
        g_ascii_dtostr(positional, sizeof positional, v->total);
        g_ascii_dtostr(own, sizeof own, v->own_potential);
        g_ascii_dtostr(opponent, sizeof opponent, v->opponent_potential);
        g_string_append_printf(history,
            "| %u | %d | %u | %u/%u/%u/%u | %u/%u/%u/%u | %u/%u | %u/%u | %s | %s | %u | %u | %u | %u | %u | %u | %u | %u | %u | %u | %u | %u | %s | %s | %s |\n",
            col + 1, s->row + 1, v->empty_windows,
            v->own_windows[1], v->own_windows[2], v->own_windows[3], v->own_windows[4],
            v->opponent_windows[1], v->opponent_windows[2], v->opponent_windows[3], v->opponent_windows[4],
            v->own_ready, v->own_supported, v->opponent_ready, v->opponent_supported,
            own, opponent, s->win, s->block, s->fork, s->stacked,
            s->opponent_forks, s->opponent_stacked, s->immediate_loss,
            s->forcing_win, s->forcing_loss, s->prepared_win, s->prepared_loss,
            s->forcing_limited, positional, random, total);
    }
    g_string_append_printf(history, "\nEscolha: coluna **%u**, linha %d.\n\n",
                           column + 1, scores[column].row + 1);
}

char *game_report(const Game *game, const char *history, const char *configuration,
                  GDateTime *finished_at)
{
    int result = game_result(game);
    if (result != 1 && result != -1)
        return NULL;
    char *timestamp = g_date_time_format(finished_at, "%Y-%m-%d %H:%M:%S %:z");
    GString *report = g_string_new("\n## Partida Lig-4 — ");
    g_string_append_printf(report, "%s\n\n%s\n\nVencedor: **%s**.\n\n%s",
                           timestamp, configuration, game_player_name(result), history);
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
