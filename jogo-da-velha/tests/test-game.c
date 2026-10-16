#include "game.h"
#include <glib/gstdio.h>
#include <signal.h>
#include <sys/resource.h>

static void test_rules(void)
{
    const unsigned lines[8][3] = {
        {0,1,2}, {3,4,5}, {6,7,8}, {0,3,6},
        {1,4,7}, {2,5,8}, {0,4,8}, {2,4,6}
    };
    for (unsigned i = 0; i < 8; i++) {
        for (int player = -1; player <= 1; player += 2) {
            Game game;
            game_init(&game);
            for (unsigned j = 0; j < 3; j++) {
                unsigned cell = lines[i][j];
                game.cells[cell / 3][cell % 3] = player;
            }
            g_assert_cmpint(game_result(&game), ==, player);
            const unsigned *winning = game_winning_line(&game);
            g_assert_nonnull(winning);
            for (unsigned j = 0; j < 3; j++)
                g_assert_cmpuint(winning[j], ==, lines[i][j]);
            g_assert_false(game_play(&game, (lines[i][0] + 1) % 9));
        }
    }
    Game game;
    game_init(&game);
    g_assert_null(game_winning_line(&game));
    g_assert_cmpint(game.turn, ==, 1);
    g_assert_false(game_play(&game, 9));
    g_assert_true(game_play(&game, 0));
    g_assert_false(game_play(&game, 0));
    g_assert_cmpint(game.turn, ==, -1);
    g_assert_cmpuint(game.moves, ==, 1);
    unsigned rest[] = {1, 2, 4, 3, 5, 7, 6, 8};
    for (unsigned i = 0; i < G_N_ELEMENTS(rest); i++)
        g_assert_true(game_play(&game, rest[i]));
    g_assert_cmpint(game_result(&game), ==, 2);
    g_assert_null(game_winning_line(&game));
    g_assert_false(game_play(&game, 0));
}

static void test_scores(void)
{
    GRand *random = g_rand_new_with_seed(1234);
    Game game = {.cells = {{1,1,0}, {-1,-1,0}, {0,0,0}}, .turn = 1, .moves = 4};
    Score scores[9];
    game_evaluate(&game, random, scores);
    g_assert_cmpint(game_best_move(scores), ==, 2);
    g_assert_cmpuint(scores[2].wins, ==, 1);
    g_assert_cmpuint(scores[5].blocks, ==, 1);
    g_assert_cmpfloat(scores[2].total, >, scores[5].total);
    g_assert_cmpuint(scores[6].own_lines, ==, 0); /* mixed diagonal/column */
    g_assert_cmpuint(scores[6].opponent_lines, ==, 1);
    g_assert_false(scores[0].empty);
    game.turn = -1;
    game_evaluate(&game, random, scores);
    g_assert_cmpint(game_best_move(scores), ==, 5);
    g_assert_cmpuint(scores[2].blocks, ==, 1);

    game_init(&game);
    game.cells[0][0] = 1;
    game.cells[0][2] = 1;
    game_evaluate(&game, random, scores);
    g_assert_cmpuint(scores[4].own_lines, ==, 2);
    g_assert_cmpfloat(scores[4].total, >=, 20);
    g_assert_cmpuint(scores[1].wins, ==, 1);
    for (unsigned i = 0; i < 9; i++) {
        if (scores[i].empty) {
            g_assert_cmpfloat(scores[i].random, >=, 0);
            g_assert_cmpfloat(scores[i].random, <, .1);
        }
    }
    game_init(&game);
    game_evaluate(&game, random, scores);
    for (unsigned i = 0; i < 9; i++)
        g_assert_cmpfloat(scores[i].total, ==, scores[i].random);
    Score tied[9] = {0};
    g_assert_cmpint(game_best_move(tied), ==, -1);
    tied[3].empty = tied[5].empty = TRUE;
    tied[3].total = tied[5].total = 100;
    g_assert_cmpint(game_best_move(tied), ==, 3);
    g_rand_free(random);
}

/* Traverse every reachable position: wins always outrank blocks, blocks always
 * outrank quiet moves, and the strategy never selects an occupied square. */
static void visit(Game game, GRand *random)
{
    if (game_result(&game))
        return;
    Score scores[9];
    game_evaluate(&game, random, scores);
    int best = game_best_move(scores);
    g_assert_cmpint(best, >=, 0);
    g_assert_cmpint(game.cells[best / 3][best % 3], ==, 0);
    gboolean safe_available = FALSE;
    for (unsigned i = 0; i < 9; i++)
        safe_available |= scores[i].empty && !scores[i].immediate_loss && !scores[i].opponent_forks;
    if (safe_available) {
        g_assert_cmpuint(scores[best].immediate_loss, ==, 0);
        g_assert_cmpuint(scores[best].opponent_forks, ==, 0);
    }
    for (unsigned i = 0; i < 9; i++) {
        if (!scores[i].empty)
            continue;
        if (scores[i].wins)
            g_assert_cmpuint(scores[best].wins, >, 0);
        if (scores[i].blocks && !scores[best].wins)
            g_assert_cmpuint(scores[best].blocks, >, 0);
        Game next = game;
        g_assert_true(game_play(&next, i));
        visit(next, random);
    }
}

static void test_all_positions(void)
{
    Game game;
    game_init(&game);
    GRand *random = g_rand_new_with_seed(42);
    visit(game, random);
    g_rand_free(random);
}

static void test_center_bonus(void)
{
    GRand *random = g_rand_new_with_seed(13);
    Game game;
    Score scores[9];
    game_init(&game);
    game_evaluate(&game, random, scores);
    g_assert_cmpuint(scores[4].center_bonus, ==, 0);
    for (unsigned opening = 0; opening < 9; opening++) {
        game_init(&game);
        g_assert_true(game_play(&game, opening));
        game_evaluate(&game, random, scores);
        if (opening != 4) {
            g_assert_cmpint(game_best_move(scores), ==, 4);
            g_assert_cmpuint(scores[4].center_bonus, ==, 5);
        } else {
            g_assert_false(scores[4].empty);
            g_assert_cmpuint(scores[4].center_bonus, ==, 0);
        }
        for (unsigned cell = 0; cell < 9; cell++)
            if (cell != 4)
                g_assert_cmpuint(scores[cell].center_bonus, ==, 0);
    }
    /* Bonus remains available later, but cannot override an immediate block. */
    game_init(&game);
    game_play(&game, 0);
    game_play(&game, 8);
    game_play(&game, 1);
    game_evaluate(&game, random, scores);
    g_assert_cmpuint(scores[4].center_bonus, ==, 5);
    g_assert_cmpint(game_best_move(scores), ==, 2);
    GString *history = g_string_new(NULL);
    game_record(history, &game, scores, 2, PLAYER_POSITIONAL);
    g_assert_nonnull(strstr(history->str, "Bônus centro"));
    g_assert_nonnull(strstr(history->str, "| 5 | 2 | 2 | 0 | 0 | 0 | 1 | 5 |"));
    g_string_free(history, TRUE);
    g_rand_free(random);
}

static void test_open_lines(void)
{
    Game game;
    GRand *random = g_rand_new_with_seed(19);
    Score scores[9];
    game_init(&game);
    unsigned openings = 0;
    for (unsigned trial = 0; trial < 256; trial++) {
        game_evaluate(&game, random, scores);
        for (unsigned cell = 0; cell < 9; cell++) {
            g_assert_cmpuint(scores[cell].open_lines, ==, 0);
            g_assert_cmpuint(scores[cell].center_bonus, ==, 0);
            g_assert_cmpfloat(scores[cell].total, ==, scores[cell].random);
        }
        openings |= 1u << game_best_move(scores);
    }
    g_assert_cmpuint(openings, ==, (1u << 9) - 1);
    /* Regression from 2026-10-16 16:17: against a center opening, corners
     * keep two lines available while sides keep only one, regardless of RNG. */
    game_play(&game, 4);
    for (unsigned trial = 0; trial < 64; trial++) {
        game_evaluate(&game, random, scores);
        int best = game_best_move(scores);
        g_assert_true(best == 0 || best == 2 || best == 6 || best == 8);
        for (unsigned cell = 0; cell < 9; cell++) {
            if (cell != 4)
                g_assert_cmpuint(scores[cell].open_lines, ==, cell % 2 == 0 ? 2 : 1);
        }
        g_assert_cmpuint(scores[4].open_lines, ==, 0);
    }
    /* Mixed lines are excluded: X in the center blocks the diagonals,
     * and its bottom-edge piece also closes the bottom row to O. */
    game_play(&game, 0);
    game_play(&game, 7);
    game_evaluate(&game, random, scores);
    g_assert_cmpuint(scores[2].open_lines, ==, 2); /* row contains O, column empty */
    g_assert_cmpuint(scores[6].open_lines, ==, 1); /* column contains O */
    GString *history = g_string_new(NULL);
    game_record(history, &game, scores, (unsigned) game_best_move(scores), PLAYER_POSITIONAL);
    g_assert_nonnull(strstr(history->str, "Linhas abertas ×0,2"));
    g_string_free(history, TRUE);
    g_rand_free(random);
}

static void test_history(void)
{
    Game game;
    game_init(&game);
    GString *history = g_string_new(NULL);
    GRand *random = g_rand_new_with_seed(7);
    unsigned moves[] = {0,3,1,4,2};
    for (unsigned i = 0; i < G_N_ELEMENTS(moves); i++) {
        Score scores[9];
        game_evaluate(&game, random, scores);
        game_record(history, &game, scores, moves[i], i % 2 == 0 ? PLAYER_HUMAN : PLAYER_POSITIONAL);
        game_play(&game, moves[i]);
    }
    g_assert_nonnull(strstr(history->str, "Jogada 1 — X (Humano)"));
    g_assert_nonnull(strstr(history->str, "Jogada 2 — O (Posicional)"));
    g_assert_nonnull(strstr(history->str, "Escolha: tecla **9**"));
    GDateTime *date = g_date_time_new_from_iso8601("2026-10-16T14:30:00-03:00", NULL);
    char *report = game_report(&game, history->str, "X: humano; O: automático.", date);
    g_assert_nonnull(report);
    g_assert_nonnull(strstr(report, "2026-10-16 14:30:00 -03:00"));
    g_assert_nonnull(strstr(report, "Vencedor: **X**"));
    g_assert_nonnull(strstr(report, "X X X"));
    GError *error = NULL;
    char *directory = g_dir_make_tmp("jogo-da-velha-test-XXXXXX", &error);
    g_assert_no_error(error);
    char *path = g_build_filename(directory, "histórico.md", NULL);
    g_assert_true(game_append_report(path, report, &error));
    g_assert_no_error(error);
    g_assert_true(game_append_report(path, report, &error));
    g_assert_no_error(error);
    char *saved = NULL;
    gsize size;
    g_assert_true(g_file_get_contents(path, &saved, &size, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(size, ==, 2 * strlen(report));
    g_assert_cmpmem(saved, strlen(report), report, strlen(report));
    g_assert_cmpmem(saved + strlen(report), strlen(report), report, strlen(report));
    g_assert_false(game_append_report(directory, report, &error));
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_ISDIR);
    g_clear_error(&error);
    g_remove(path);
    g_rmdir(directory);
    g_free(saved);
    g_free(path);
    g_free(directory);
    g_free(report);
    game_init(&game);
    g_assert_null(game_report(&game, "", "", date));
    unsigned draw[] = {0,1,2,4,3,5,7,6,8};
    for (unsigned i = 0; i < 9; i++)
        game_play(&game, draw[i]);
    g_assert_null(game_report(&game, history->str, "", date));
    g_string_truncate(history, 0);
    g_assert_cmpuint(history->len, ==, 0);
    g_string_free(history, TRUE);
    g_rand_free(random);
    g_date_time_unref(date);
}

static unsigned transform_cell(unsigned cell, unsigned rotation, gboolean reflect)
{
    unsigned row = cell / 3, col = cell % 3;
    if (reflect)
        col = 2 - col;
    for (unsigned i = 0; i < rotation; i++) {
        unsigned previous_row = row;
        row = col;
        col = 2 - previous_row;
    }
    return row * 3 + col;
}

static void test_forks(void)
{
    GRand *random = g_rand_new_with_seed(77);
    /* Regression from 2026-10-16 16:12: opposite corners against a center.
     * Cover both symbols, all rotations/reflections and random tie breakers. */
    for (int player = -1; player <= 1; player += 2) {
        for (unsigned rotation = 0; rotation < 4; rotation++) {
            for (unsigned reflect = 0; reflect < 2; reflect++) {
                Game game = {.turn = player, .moves = 3};
                unsigned a = transform_cell(0, rotation, reflect);
                unsigned b = transform_cell(8, rotation, reflect);
                game.cells[a / 3][a % 3] = -player;
                game.cells[b / 3][b % 3] = -player;
                game.cells[1][1] = player;
                for (unsigned trial = 0; trial < 16; trial++) {
                    Score scores[9];
                    game_evaluate(&game, random, scores);
                    int best = game_best_move(scores);
                    g_assert_true(best == 1 || best == 3 || best == 5 || best == 7);
                    g_assert_cmpuint(scores[best].opponent_forks, ==, 0);
                    for (unsigned cell = 0; cell < 9; cell += 2)
                        if (scores[cell].empty)
                            g_assert_cmpuint(scores[cell].opponent_forks, >, 0);
                }
            }
        }
    }
    Game game;
    game_init(&game);
    game_play(&game, 0);
    game_play(&game, 1);
    game_play(&game, 4);
    game_play(&game, 8);
    Score scores[9];
    game_evaluate(&game, random, scores);
    g_assert_cmpuint(scores[6].fork, ==, 1);
    g_assert_cmpuint(scores[game_best_move(scores)].fork, ==, 1);
    /* Replaying the losing corner: blocking the anti-diagonal also creates
     * a fork. On the following turn no single block can remove both threats. */
    game_init(&game);
    unsigned moves[] = {0, 4, 8, 2};
    for (unsigned i = 0; i < G_N_ELEMENTS(moves); i++)
        game_play(&game, moves[i]);
    game_evaluate(&game, random, scores);
    g_assert_cmpuint(scores[6].fork, ==, 1);
    g_assert_cmpuint(scores[6].blocks, ==, 1);
    g_assert_cmpint(game_best_move(scores), ==, 6);
    game_play(&game, 6);
    game_evaluate(&game, random, scores);
    for (unsigned cell = 0; cell < 9; cell++)
        if (scores[cell].empty)
            g_assert_cmpuint(scores[cell].immediate_loss, ==, 1);
    GString *history = g_string_new(NULL);
    game_record(history, &game, scores, (unsigned) game_best_move(scores), PLAYER_POSITIONAL);
    g_assert_nonnull(strstr(history->str, "Garfo ×50"));
    g_assert_nonnull(strstr(history->str, "Respostas com garfo"));
    g_string_free(history, TRUE);
    g_rand_free(random);
}

static void test_partial_write(void)
{
    if (!g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 5000000, 0);
        g_test_trap_assert_passed();
        return;
    }
    GError *error = NULL;
    char *directory = g_dir_make_tmp("jogo-da-velha-partial-XXXXXX", &error);
    g_assert_no_error(error);
    char *path = g_build_filename(directory, "histórico.md", NULL);
    g_assert_true(g_file_set_contents(path, "existing history\n", -1, &error));
    g_assert_no_error(error);
    struct rlimit original, limited;
    g_assert_cmpint(getrlimit(RLIMIT_FSIZE, &original), ==, 0);
    limited = original;
    limited.rlim_cur = 64;
    signal(SIGXFSZ, SIG_IGN);
    g_assert_cmpint(setrlimit(RLIMIT_FSIZE, &limited), ==, 0);
    char *report = g_strnfill(4096, 'A');
    g_assert_false(game_append_report(path, report, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_assert_cmpint(setrlimit(RLIMIT_FSIZE, &original), ==, 0);
    char *saved = NULL;
    g_assert_true(g_file_get_contents(path, &saved, NULL, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(saved, ==, "existing history\n");
    g_free(saved);
    g_assert_true(game_append_report(path, report, &error));
    g_assert_no_error(error);
    g_assert_true(g_file_get_contents(path, &saved, NULL, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(strlen(saved), ==, strlen("existing history\n") + 4096);
    g_free(saved);
    g_free(report);
    g_remove(path);
    g_rmdir(directory);
    g_free(path);
    g_free(directory);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/game/rules", test_rules);
    g_test_add_func("/game/scoring", test_scores);
    g_test_add_func("/game/center-bonus", test_center_bonus);
    g_test_add_func("/game/open-lines", test_open_lines);
    g_test_add_func("/game/forks", test_forks);
    g_test_add_func("/game/all-reachable-positions", test_all_positions);
    g_test_add_func("/game/history", test_history);
    g_test_add_func("/game/partial-write-retry", test_partial_write);
    return g_test_run();
}
