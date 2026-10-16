#include "minimax.h"
#include <string.h>

static void sequence(Game *game, const unsigned *moves, unsigned count)
{
    game_init(game);
    for (unsigned i = 0; i < count; i++)
        g_assert_true(game_play(game, moves[i]));
}

static double reference_leaf(const Game *game, int root)
{
    
    static const unsigned lines[8][3] = {{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}};
    double value = 0;
    for (unsigned i = 0; i < 8; i++) {
        unsigned own = 0, opponent = 0;
        for (unsigned j = 0; j < 3; j++) {
            unsigned cell = lines[i][j];
            own += game->cells[cell / 3][cell % 3] == root;
            opponent += game->cells[cell / 3][cell % 3] == -root;
        }
        if (!opponent) value += own == 2 ? 10 : own == 1 ? 1 : 0;
        if (!own) value -= opponent == 2 ? 10 : opponent == 1 ? 1 : 0;
    }
    return value;
}

/* Independent, unpruned search: no cache, ordering or production search code. */
static double reference(const Game *game, unsigned depth, int root, unsigned initial_moves)
{
    int result = game_result(game);
    if (result == 2) return 0;
    if (result) return result == root ? 1e9 - (game->moves - initial_moves) : -1e9 + (game->moves - initial_moves);
    if (!depth) return reference_leaf(game, root);
    double best = game->turn == root ? -1e12 : 1e12;
    for (unsigned move = 0; move < GAME_CHOICES; move++) {
        Game next = *game;
        if (!game_play(&next, move)) continue;
        double value = reference(&next, depth - 1, root, initial_moves);
        best = game->turn == root ? MAX(best, value) : MIN(best, value);
    }
    return best;
}

static void test_reference(void)
{
    const unsigned moves[] = {0,4};
    Game game;
    sequence(&game, moves, G_N_ELEMENTS(moves));
    for (unsigned depth = 1; depth <= 4; depth++) {
        for (unsigned flags = 0; flags < 4; flags++) {
            SearchControl control = {.disable_cache = flags & 1, .disable_ordering = flags & 2};
            MinimaxResult result;
            g_assert_true(minimax_evaluate(&game, depth, &control, &result));
            g_assert_cmpuint(result.completed_depth, ==, depth);
            for (unsigned move = 0; move < GAME_CHOICES; move++) {
                Game next = game;
                gboolean legal = game_play(&next, move);
                g_assert_cmpint(result.legal[move], ==, legal);
                if (legal) {
                    double expected = reference(&next, depth - 1, game.turn, game.moves);
                    g_assert_cmpfloat_with_epsilon(result.values[move], expected, .000001);
                }
            }
        }
    }
}

static void test_tactics(void)
{
    const unsigned winning[] = {0,3,1,4};
    Game game;
    sequence(&game, winning, G_N_ELEMENTS(winning));
    MinimaxResult result;
    g_assert_true(minimax_evaluate(&game, 9, NULL, &result));
    g_assert_cmpint(result.best_move, ==, 2);
    g_assert_cmpfloat(result.values[2], ==, 1e9 - 1);
    const unsigned blocking[] = {0,4,1};
    sequence(&game, blocking, G_N_ELEMENTS(blocking));
    g_assert_true(minimax_evaluate(&game, 9, NULL, &result));
    g_assert_cmpint(result.best_move, ==, 2);
}

static void test_opening(void)
{
    Game game;
    game_init(&game);
    GRand *random = g_rand_new_with_seed(73);
    MinimaxResult result;
    g_assert_true(minimax_evaluate(&game, GAME_MAX_DEPTH, NULL, &result));
    g_assert_true(result.opening_random);
    g_assert_cmpuint(result.nodes, ==, 0);
    g_assert_cmpuint(result.completed_depth, ==, 0);
    unsigned seen = 0;
    for (unsigned i = 0; i < 512; i++) {
        int best = minimax_choose(&result, random);
        g_assert_cmpint(best, >=, 0);
        seen |= 1u << best;
    }
    g_assert_cmpuint(seen, ==, (1u << GAME_CHOICES) - 1);
    for (unsigned move = 0; move < GAME_CHOICES; move++) {
        g_assert_true(result.legal[move]);
        g_assert_cmpfloat(result.values[move], ==, 0);
    }
    GString *history = g_string_new(NULL);
    minimax_record(history, &result);
    g_assert_nonnull(strstr(history->str, "Abertura aleatória"));
    g_assert_nonnull(strstr(history->str, "Profundidade solicitada"));
    g_string_free(history, TRUE);
    g_rand_free(random);
}

static void test_tie_tolerance(void)
{
    MinimaxResult result = {.best_move = 1};
    result.legal[0] = result.legal[1] = result.legal[2] = TRUE;
    result.values[0] = 1 - 1.5e-9;
    result.values[1] = 1 - .75e-9;
    result.values[2] = 1;
    GRand *random = g_rand_new_with_seed(91);
    unsigned seen = 0;
    for (unsigned i = 0; i < 128; i++) {
        int move = minimax_choose(&result, random);
        g_assert_true(move == 1 || move == 2);
        seen |= 1u << move;
    }
    g_assert_cmpuint(seen, ==, 6);
    g_rand_free(random);
}

typedef struct { gboolean stop; unsigned depth; } Cancel;
static gboolean cancelled(gpointer data) { return ((Cancel *) data)->stop; }
static void progress(unsigned completed, gpointer data)
{
    Cancel *cancel = data;
    cancel->depth = completed;
    cancel->stop = TRUE;
}

static void test_cancel(void)
{
    Game game;
    const unsigned moves[] = {0,4};
    sequence(&game, moves, G_N_ELEMENTS(moves));
    Cancel cancel = {.stop = TRUE};
    SearchControl control = {.cancelled = cancelled, .progress = progress, .data = &cancel};
    MinimaxResult result;
    g_assert_false(minimax_evaluate(&game, GAME_MAX_DEPTH, &control, &result));
    g_assert_true(result.cancelled);
    g_assert_cmpuint(result.completed_depth, ==, 0);
    cancel.stop = FALSE;
    g_assert_false(minimax_evaluate(&game, GAME_MAX_DEPTH, &control, &result));
    g_assert_true(result.cancelled);
    g_assert_cmpuint(result.completed_depth, ==, 1);
    g_assert_cmpuint(cancel.depth, ==, 1);
    GRand *random = g_rand_new_with_seed(1);
    g_assert_cmpint(minimax_choose(&result, random), ==, -1);
    g_rand_free(random);
}

static void test_last_move(void)
{
    const unsigned moves[] = {0,1,2,4,3,5,7,6};
    Game game;
    sequence(&game, moves, G_N_ELEMENTS(moves));
    MinimaxResult result;
    g_assert_true(minimax_evaluate(&game, GAME_MAX_DEPTH, NULL, &result));
    g_assert_cmpuint(result.completed_depth, ==, 1);
    g_assert_true(game_play(&game, result.best_move));
    g_assert_cmpint(game_result(&game), ==, 2);
    g_assert_true(minimax_evaluate(&game, GAME_MAX_DEPTH, NULL, &result));
    g_assert_cmpint(result.best_move, ==, -1);
}

static guint8 oracle[19683], seen[19683];
static unsigned board_key(const Game *game)
{
    unsigned key = 0;
    for (unsigned r = 0; r < 3; r++)
        for (unsigned c = 0; c < 3; c++)
            key = key * 3 + (unsigned) (game->cells[r][c] + 1);
    return key;
}
static int outcome(const Game *game)
{
    unsigned key = board_key(game);
    if (oracle[key]) return (int) oracle[key] - 2;
    int result = game_result(game);
    if (result) return result == 2 ? 0 : result;
    int best = game->turn == 1 ? -2 : 2;
    for (unsigned move = 0; move < 9; move++) {
        Game next = *game;
        if (!game_play(&next, move)) continue;
        int value = outcome(&next);
        best = game->turn == 1 ? MAX(best, value) : MIN(best, value);
    }
    oracle[key] = (guint8) (best + 2);
    return best;
}
static void visit_all(Game game)
{
    unsigned key = board_key(&game);
    if (seen[key] || game_result(&game)) return;
    seen[key] = 1;
    SearchControl control = {.disable_cache = TRUE};
    MinimaxResult result;
    g_assert_true(minimax_evaluate(&game, 9, &control, &result));
    double best = result.values[result.best_move];
    int expected_best = outcome(&game) * game.turn;
    g_assert_cmpint(best > 0 ? 1 : best < 0 ? -1 : 0, ==, expected_best);
    for (unsigned move = 0; move < 9; move++) {
        Game next = game;
        if (!game_play(&next, move)) continue;
        int expected = outcome(&next) * game.turn;
        double value = result.values[move];
        g_assert_cmpint(value > 0 ? 1 : value < 0 ? -1 : 0, ==, expected);
        visit_all(next);
    }
}
static void test_exhaustive(void)
{
    Game game;
    game_init(&game);
    visit_all(game);
}


int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/minimax/reference-cache-ordering", test_reference);
    g_test_add_func("/minimax/tactics", test_tactics);
    g_test_add_func("/minimax/opening", test_opening);
    g_test_add_func("/minimax/tie-tolerance", test_tie_tolerance);
    g_test_add_func("/minimax/cancellation", test_cancel);
    g_test_add_func("/minimax/last-move", test_last_move);
    g_test_add_func("/minimax/all-reachable-positions", test_exhaustive);
    return g_test_run();
}
