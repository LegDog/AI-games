#include "minimax.h"
#include <string.h>

#define MATE 1000000000.0
#define INF 1000000000000.0
#define TABLE_SIZE (1u << 18)
enum { EXACT = 1, LOWER, UPPER };

typedef struct {
    guint64 first, second;
    double score;
    int depth, turn, best, bound;
} Entry;
G_STATIC_ASSERT(sizeof(Entry) * TABLE_SIZE <= 16u * 1024u * 1024u);

typedef struct {
    const SearchControl *control;
    Entry *table;
    int root;
    unsigned root_moves;
    guint64 nodes;
    gboolean cancelled;
} Search;

static int landing(const Game *game, unsigned move)
{
    return move < GAME_CHOICES && !game->cells[move / 3][move % 3] ? (int) (move / 3) : -1;
}

static Game apply(const Game *game, unsigned move)
{
    Game next = *game;
    next.cells[move / 3][move % 3] = game->turn;
    next.moves++;
    next.turn = -next.turn;
    return next;
}

static double leaf(const Game *game, int player)
{
    static const unsigned lines[8][3] = {{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
    double value = 0;
    for (unsigned i = 0; i < 8; i++) {
        unsigned own = 0, opponent = 0;
        for (unsigned j = 0; j < 3; j++) {
            unsigned cell = lines[i][j];
            own += game->cells[cell / 3][cell % 3] == player;
            opponent += game->cells[cell / 3][cell % 3] == -player;
        }
        if (!opponent) value += own == 2 ? 10 : own == 1 ? 1 : 0;
        if (!own) value -= opponent == 2 ? 10 : opponent == 1 ? 1 : 0;
    }
    return value;
}

static gboolean stopped(Search *search)
{
    if (search->control && search->control->cancelled &&
        search->control->cancelled(search->control->data))
        search->cancelled = TRUE;
    return search->cancelled;
}

static Entry *lookup(Search *search, const Game *game, guint64 *first, guint64 *second)
{
    *first = *second = 0;
    if (!search->table)
        return NULL;
    for (unsigned row = 0; row < GAME_ROWS; row++) {
        for (unsigned col = 0; col < GAME_COLS; col++) {
            guint64 bit = G_GUINT64_CONSTANT(1) << (row * GAME_COLS + col);
            if (game->cells[row][col] == 1) *first |= bit;
            if (game->cells[row][col] == -1) *second |= bit;
        }
    }
    guint64 hash = *first * G_GUINT64_CONSTANT(0x9e3779b97f4a7c15) ^
                   *second * G_GUINT64_CONSTANT(0xc2b2ae3d27d4eb4f) ^ (guint64) (game->turn + 1);
    hash ^= hash >> 32;
    return &search->table[hash & (TABLE_SIZE - 1)];
}

static unsigned ordered_moves(const Game *game, int preferred, gboolean unordered,
                              unsigned moves[GAME_CHOICES])
{
    unsigned count = 0;
    int priorities[GAME_CHOICES];
    for (unsigned move = 0; move < GAME_CHOICES; move++) {
        if (landing(game, move) < 0)
            continue;
        int priority = 0;
        if (!unordered) {
            Game next = apply(game, move);
            if (game_result(&next) == game->turn)
                priority += 100000;
            Game opponent = *game;
            opponent.turn = -game->turn;
            next = apply(&opponent, move);
            if (game_result(&next) == -game->turn)
                priority += 10000;
            priority += 10 - ABS((int) (move / 3) - 1) - ABS((int) (move % 3) - 1);
            if ((int) move == preferred)
                priority += 1000000;
        }
        unsigned i = count++;
        while (i && priorities[i - 1] < priority) {
            moves[i] = moves[i - 1];
            priorities[i] = priorities[i - 1];
            i--;
        }
        moves[i] = move;
        priorities[i] = priority;
    }
    return count;
}

static double visit(Search *search, const Game *game, unsigned depth, double alpha, double beta)
{
    search->nodes++;
    if (stopped(search))
        return 0;
    int result = game_result(game);
    unsigned ply = game->moves - search->root_moves;
    if (result == 2)
        return 0;
    if (result)
        return result == search->root ? MATE - ply : -MATE + ply;
    if (!depth)
        return leaf(game, search->root);

    double original_alpha = alpha, original_beta = beta;
    guint64 first, second;
    Entry *entry = lookup(search, game, &first, &second);
    int preferred = -1;
    if (entry && entry->bound && entry->first == first && entry->second == second &&
        entry->turn == game->turn) {
        preferred = entry->best;
        if (entry->depth >= (int) depth) {
            if (entry->bound == EXACT)
                return entry->score;
            if (entry->bound == LOWER) alpha = MAX(alpha, entry->score);
            if (entry->bound == UPPER) beta = MIN(beta, entry->score);
            if (alpha >= beta)
                return entry->score;
        }
    }
    unsigned moves[GAME_CHOICES];
    unsigned count = ordered_moves(game, preferred, search->control && search->control->disable_ordering, moves);
    gboolean maximizing = game->turn == search->root;
    double best = maximizing ? -INF : INF;
    int best_move = -1;
    for (unsigned i = 0; i < count; i++) {
        Game next = apply(game, moves[i]);
        double value = visit(search, &next, depth - 1, alpha, beta);
        if (search->cancelled)
            return 0;
        if (best_move < 0 || (maximizing ? value > best : value < best)) {
            best = value;
            best_move = moves[i];
        }
        if (maximizing) alpha = MAX(alpha, best);
        else beta = MIN(beta, best);
        if (alpha >= beta)
            break;
    }
    if (entry) {
        int bound = best <= original_alpha ? UPPER : best >= original_beta ? LOWER : EXACT;
        *entry = (Entry) {first, second, best, (int) depth, game->turn, best_move, bound};
    }
    return best;
}

gboolean minimax_evaluate(const Game *game, unsigned depth, const SearchControl *control,
                         MinimaxResult *result)
{
    memset(result, 0, sizeof *result);
    result->best_move = -1;
    result->requested_depth = CLAMP(depth, 1, GAME_MAX_DEPTH);
    gint64 started = g_get_monotonic_time();
    Search search = {.control = control, .root = game->turn, .root_moves = game->moves};
    if (game_result(game))
        return TRUE;
    for (unsigned move = 0; move < GAME_CHOICES; move++)
        result->legal[move] = landing(game, move) >= 0;
    if (stopped(&search))
        goto done;
    if (game->moves == 0) {
        result->opening_random = TRUE;
        result->best_move = 0;
        goto done;
    }
    if (!control || !control->disable_cache)
        search.table = g_try_new0(Entry, TABLE_SIZE);
    unsigned effective_depth = MIN(result->requested_depth, GAME_ROWS * GAME_COLS - game->moves);
    int preferred = -1;
    for (unsigned iteration = 1; iteration <= effective_depth; iteration++) {
        unsigned moves[GAME_CHOICES];
        double values[GAME_CHOICES] = {0};
        unsigned count = ordered_moves(game, preferred, control && control->disable_ordering, moves);
        double best = -INF;
        int best_move = -1;
        for (unsigned i = 0; i < count; i++) {
            Game next = apply(game, moves[i]);
            /* Independent full windows keep every displayed root value exact. */
            double value = visit(&search, &next, iteration - 1, -INF, INF);
            if (search.cancelled)
                goto done;
            values[moves[i]] = value;
            if (best_move < 0 || value > best) {
                best = value;
                best_move = moves[i];
            }
        }
        for (unsigned move = 0; move < GAME_CHOICES; move++) {
            if (result->legal[move] && ABS(values[move] - best) <= 1e-9) {
                best_move = (int) move;
                break;
            }
        }
        memcpy(result->values, values, sizeof values);
        result->best_move = best_move;
        result->completed_depth = iteration;
        preferred = best_move;
        if (control && control->progress)
            control->progress(iteration, control->data);
        if (stopped(&search))
            goto done;
    }
done:
    result->cancelled = search.cancelled;
    result->nodes = search.nodes;
    result->elapsed_us = g_get_monotonic_time() - started;
    g_free(search.table);
    return !result->cancelled;
}

int minimax_choose(const MinimaxResult *result, GRand *random)
{
    if (result->cancelled || result->best_move < 0)
        return -1;
    double best = -INF;
    for (unsigned move = 0; move < GAME_CHOICES; move++)
        if (result->legal[move]) best = MAX(best, result->values[move]);
    unsigned count = 0;
    int chosen = -1;
    for (unsigned move = 0; move < GAME_CHOICES; move++) {
        if (result->legal[move] && ABS(result->values[move] - best) <= 1e-9) {
            count++;
            if (g_rand_int_range(random, 0, count) == 0)
                chosen = move;
        }
    }
    return chosen;
}

void minimax_record(GString *history, const MinimaxResult *result)
{
    g_string_append_printf(history,
        "#### Minimax\n\nProfundidade solicitada: %u; concluída: %u; nós: %" G_GUINT64_FORMAT
        "; duração: %.3f ms.\n\n",
        result->requested_depth, result->completed_depth, result->nodes, result->elapsed_us / 1000.0);
    if (result->opening_random)
        g_string_append(history, "Abertura aleatória: todas as alternativas têm valor estratégico zero.\n\n");
    g_string_append(history, "| Alternativa | Valor minimax |\n| --- | --- |\n");
    for (unsigned move = 0; move < GAME_CHOICES; move++) {
        if (!result->legal[move]) continue;
        char value[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_dtostr(value, sizeof value, result->values[move]);
        g_string_append_printf(history, "| %u | %s |\n", 7 - (move / 3) * 3 + move % 3, value);
    }
    g_string_append_printf(history, "\nEscolha minimax: **%u**.\n\n", 7 - ((unsigned) result->best_move / 3) * 3 + (unsigned) result->best_move % 3);
}
