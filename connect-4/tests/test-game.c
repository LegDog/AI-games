#include "game.h"
#include <glib/gstdio.h>
#include <signal.h>
#include <string.h>
#include <sys/resource.h>

static const unsigned draw_moves[] = {
    6,6,3,4,5,5,0,4,1,2,3,1,1,1,5,5,6,2,6,3,3,
    2,6,6,0,5,1,3,2,2,5,1,3,4,4,0,2,0,0,0,4,4
};

static void play_sequence(Game *game, const unsigned *moves, unsigned count)
{
    game_init(game);
    for (unsigned i = 0; i < count; i++) {
        g_assert_cmpint(game_result(game), ==, 0);
        g_assert_true(game_play(game, moves[i]));
    }
}

static void test_gravity(void)
{
    Game game;
    game_init(&game);
    g_assert_cmpint(game.turn, ==, 1);
    g_assert_cmpint(game_drop_row(&game, 7), ==, -1);
    g_assert_false(game_play(&game, 7));
    for (unsigned i = 0; i < GAME_ROWS; i++) {
        g_assert_cmpint(game_drop_row(&game, 2), ==, GAME_ROWS - 1 - (int) i);
        int player = game.turn;
        g_assert_true(game_play(&game, 2));
        g_assert_cmpint(game.cells[GAME_ROWS - 1 - i][2], ==, player);
        g_assert_cmpint(game.turn, ==, -player);
    }
    g_assert_cmpint(game_drop_row(&game, 2), ==, -1);
    Game before = game;
    g_assert_false(game_play(&game, 2));
    g_assert_cmpmem(&before, sizeof before, &game, sizeof game);
}

static void test_wins(void)
{
    const unsigned horizontal[] = {0,6,1,6,2,5,3};
    const unsigned vertical[] = {0,1,0,1,0,2,0};
    const unsigned down_right[] = {5,0,0,5,2,1,1,1,5,0,5,5,4,0,4,3,0,0,1,2};
    const unsigned down_left[] = {5,2,6,6,0,1,1,1,4,3,1,3,1,2,3,1,0,4,5,0,0,6,5,0,0,3,3,5,5,5,3,4};
    const unsigned *sequences[] = {horizontal, vertical, down_right, down_left};
    const unsigned lengths[] = {G_N_ELEMENTS(horizontal), G_N_ELEMENTS(vertical),
                               G_N_ELEMENTS(down_right), G_N_ELEMENTS(down_left)};
    for (unsigned i = 0; i < 4; i++) {
        Game game;
        play_sequence(&game, sequences[i], lengths[i]);
        g_assert_cmpint(game_result(&game), ==, i < 2 ? 1 : -1);
        WinningLine lines[GAME_MAX_LINES];
        g_assert_cmpuint(game_winning_lines(&game, lines), ==, 1);
        if (i == 0)
            g_assert_cmpuint(lines[0].row1, ==, lines[0].row2);
        else if (i == 1)
            g_assert_cmpuint(lines[0].col1, ==, lines[0].col2);
        else {
            g_assert_cmpuint(lines[0].row2, >, lines[0].row1);
            if (i == 2)
                g_assert_cmpuint(lines[0].col2, >, lines[0].col1);
            else
                g_assert_cmpuint(lines[0].col2, <, lines[0].col1);
        }
        g_assert_false(game_play(&game, 4));
    }
    /* A final disc can join two runs into five and complete another line. */
    Game game;
    game_init(&game);
    for (unsigned col = 0; col < 5; col++)
        game.cells[5][col] = 1;
    for (unsigned row = 2; row < 5; row++)
        game.cells[row][2] = 1;
    WinningLine lines[GAME_MAX_LINES];
    g_assert_cmpuint(game_winning_lines(&game, lines), ==, 2);
    gboolean found_long = FALSE;
    for (unsigned i = 0; i < 2; i++)
        found_long |= lines[i].row1 == 5 && lines[i].col1 == 0 && lines[i].col2 == 4;
    g_assert_true(found_long);
}

static void test_draw(void)
{
    Game game;
    play_sequence(&game, draw_moves, G_N_ELEMENTS(draw_moves));
    g_assert_cmpint(game_result(&game), ==, 2);
    g_assert_cmpuint(game.moves, ==, 42);
    g_assert_cmpuint(game_winning_lines(&game, NULL), ==, 0);
    GRand *random = g_rand_new_with_seed(3);
    Score scores[GAME_COLS];
    game_evaluate(&game, random, scores);
    g_assert_cmpint(game_best_move(scores), ==, -1);
    g_assert_false(game_play(&game, 0));
    g_rand_free(random);
}

static void test_opening(void)
{
    Game game;
    game_init(&game);
    GRand *random = g_rand_new_with_seed(17);
    unsigned openings = 0;
    for (unsigned trial = 0; trial < 256; trial++) {
        Score scores[GAME_COLS];
        game_evaluate(&game, random, scores);
        for (unsigned col = 0; col < GAME_COLS; col++) {
            g_assert_true(scores[col].legal);
            g_assert_cmpint(scores[col].row, ==, 5);
            g_assert_cmpfloat(scores[col].total, ==, scores[col].random);
            g_assert_cmpfloat(scores[col].position.total, ==, 0);
            g_assert_cmpfloat(scores[col].random, >=, 0);
            g_assert_cmpfloat(scores[col].random, <, .1);
        }
        openings |= 1u << game_best_move(scores);
    }
    g_assert_cmpuint(openings, ==, 127);
    Score tied[GAME_COLS] = {0};
    tied[1].legal = tied[3].legal = TRUE;
    g_assert_cmpint(game_best_move(tied), ==, 1);
    g_rand_free(random);
}

static void test_tactics(void)
{
    Game game;
    Score scores[GAME_COLS];
    GRand *random = g_rand_new_with_seed(23);
    unsigned win[] = {0,6,1,6,2,6};
    play_sequence(&game, win, G_N_ELEMENTS(win));
    game_evaluate(&game, random, scores);
    g_assert_cmpuint(scores[3].win, ==, 1);
    g_assert_cmpuint(scores[6].block, ==, 1);
    g_assert_cmpint(game_best_move(scores), ==, 3); /* victory before defense */
    unsigned block[] = {0,6,1,6,2};
    play_sequence(&game, block, G_N_ELEMENTS(block));
    game_evaluate(&game, random, scores);
    g_assert_cmpint(game_best_move(scores), ==, 3);
    g_assert_cmpuint(scores[3].block, ==, 1);
    g_assert_cmpuint(scores[3].immediate_loss, ==, 0);
    unsigned fork[] = {1,6,2,6};
    play_sequence(&game, fork, G_N_ELEMENTS(fork));
    game_evaluate(&game, random, scores);
    g_assert_cmpuint(scores[3].fork, ==, 1);
    g_assert_cmpint(game_best_move(scores), ==, 3);
    unsigned avoid[] = {1,6,2};
    play_sequence(&game, avoid, G_N_ELEMENTS(avoid));
    game_evaluate(&game, random, scores);
    g_assert_cmpuint(scores[5].opponent_forks, >, 0);
    g_assert_cmpuint(scores[game_best_move(scores)].opponent_forks, ==, 0);
    /* Playing column 4 supports an otherwise unreachable yellow winning cell.
     * A threat in the air is not already an immediate win to block. */
    unsigned support[] = {0,1,2,0,4,1,6,2};
    play_sequence(&game, support, G_N_ELEMENTS(support));
    game_evaluate(&game, random, scores);
    g_assert_cmpuint(scores[3].block, ==, 0);
    g_assert_cmpuint(scores[3].immediate_loss, ==, 1);
    g_assert_cmpuint(scores[game_best_move(scores)].immediate_loss, ==, 0);
    /* Mirror the same gravity trap. */
    for (unsigned i = 0; i < G_N_ELEMENTS(support); i++)
        support[i] = 6 - support[i];
    play_sequence(&game, support, G_N_ELEMENTS(support));
    game_evaluate(&game, random, scores);
    g_assert_cmpuint(scores[3].immediate_loss, ==, 1);
    g_assert_cmpuint(scores[game_best_move(scores)].immediate_loss, ==, 0);
    g_rand_free(random);
}

static void test_positional(void)
{
    Game game;
    game_init(&game);
    PositionValue value;
    game_position_evaluate(&game, 1, &value);
    g_assert_cmpuint(value.empty_windows, ==, 69);
    g_assert_cmpfloat(value.total, ==, 0);
    game_play(&game, 0);
    game_play(&game, 6);
    PositionValue own, opponent;
    game_position_evaluate(&game, 1, &own);
    game_position_evaluate(&game, -1, &opponent);
    g_assert_cmpfloat_with_epsilon(own.own_potential, opponent.opponent_potential, .000001);
    g_assert_cmpfloat_with_epsilon(own.opponent_potential, opponent.own_potential, .000001);
    g_assert_cmpfloat_with_epsilon(own.total, SCORE_POSITIONAL_SCALE *
        (own.own_potential - SCORE_DEFENSE_FACTOR * own.opponent_potential), .000001);
    /* Remote yellow opportunities contribute even when red drops in column 1. */
    GRand *random = g_rand_new_with_seed(3);
    Score scores[GAME_COLS];
    game_evaluate(&game, random, scores);
    g_assert_cmpfloat(scores[0].position.opponent_potential, >, 0);
    Game next = game;
    game_play(&next, 0);
    game_position_evaluate(&next, game.turn, &value);
    g_assert_cmpfloat_with_epsilon(scores[0].position.total, value.total, .000001);
    g_rand_free(random);
}

static void test_accessibility(void)
{
    Game game;
    game_init(&game);
    game_play(&game, 0);
    PositionValue value;
    game_position_evaluate(&game, 1, &value);
    /* One corner disc belongs to a ready horizontal and vertical window,
     * and a diagonal needing six external support cells: 10 + 10 + 10/7. */
    g_assert_cmpuint(value.own_windows[1], ==, 3);
    g_assert_cmpuint(value.own_ready, ==, 2);
    g_assert_cmpuint(value.own_supported, ==, 1);
    g_assert_cmpfloat_with_epsilon(value.own_potential, 20 + 10.0 / 7, .000001);
    game_play(&game, 1);
    game_position_evaluate(&game, 1, &value);
    /* Yellow closes the horizontal window but supplies one diagonal support.
     * The vertical window's internal empty cells are not external supports. */
    g_assert_cmpuint(value.own_windows[1], ==, 2);
    g_assert_cmpuint(value.own_ready, ==, 1);
    g_assert_cmpuint(value.own_supported, ==, 1);
    g_assert_cmpfloat_with_epsilon(value.own_potential, 10 + 10.0 / 6, .000001);
}

static void test_stacked_regression(void)
{
    /* Recorded game 2026-10-16 17:32:12, zero-based columns. */
    const unsigned moves[] = {6,3,6,2,6,6,4,4,4,0,1,2,2,3,1,3,3,2,5,5};
    GRand *random = g_rand_new_with_seed(29);
    for (unsigned mirror = 0; mirror < 2; mirror++) {
        for (int color = -1; color <= 1; color += 2) {
            Game game;
            game_init(&game);
            game.turn = color;
            for (unsigned turn = 0; turn < 18; turn++) {
                Score scores[GAME_COLS];
                unsigned col = mirror ? 6 - moves[turn] : moves[turn];
                if (turn == 12) {
                    for (unsigned trial = 0; trial < 16; trial++) {
                        game_evaluate(&game, random, scores);
                        g_assert_cmpint(game_best_move(scores), ==, 3);
                        g_assert_cmpfloat(scores[3].position.total, >, scores[col].position.total);
                    }
                }
                if (turn == 14 || turn == 16) {
                    game_evaluate(&game, random, scores);
                    g_assert_cmpuint(scores[col].opponent_stacked, >, 0);
                    g_assert_cmpuint(scores[col].opponent_forks, >, 0);
                }
                if (turn == 17) {
                    game_evaluate(&game, random, scores);
                    g_assert_cmpuint(scores[col].stacked, ==, 1);
                    g_assert_cmpuint(scores[col].fork, ==, 0); /* only one playable column */
                    g_assert_cmpint(game_best_move(scores), ==, col);
                    GString *history = g_string_new(NULL);
                    game_record(history, &game, scores, col, PLAYER_POSITIONAL);
                    g_assert_nonnull(strstr(history->str, "Empilhamento"));
                    g_assert_nonnull(strstr(history->str, "Potencial adversário"));
                    g_string_free(history, TRUE);
                }
                g_assert_true(game_play(&game, col));
            }
            Score scores[GAME_COLS];
            game_evaluate(&game, random, scores);
            for (unsigned col = 0; col < GAME_COLS; col++)
                if (scores[col].legal)
                    g_assert_cmpuint(scores[col].immediate_loss, ==, 1);
            unsigned target = mirror ? 1 : 5;
            g_assert_true(game_play(&game, target)); /* forced block supplies support */
            g_assert_cmpint(game_result(&game), ==, 0);
            game_evaluate(&game, random, scores);
            g_assert_cmpuint(scores[target].win, ==, 1);
            g_assert_cmpint(game_best_move(scores), ==, target);
            int attacker = game.turn;
            g_assert_true(game_play(&game, target));
            g_assert_cmpint(game_result(&game), ==, attacker);
        }
    }
    /* A defender with its own immediate win need not play the forced block. */
    Game game;
    play_sequence(&game, moves, 17);
    for (unsigned row = 2; row <= 4; row++)
        game.cells[row][0] = 1;
    game.moves += 3;
    Score scores[GAME_COLS];
    game_evaluate(&game, random, scores);
    g_assert_cmpuint(scores[2].immediate_loss, ==, 1);
    g_assert_cmpuint(scores[2].stacked, ==, 0);
    g_assert_cmpint(game_best_move(scores), !=, 2);
    g_rand_free(random);
}

static void test_forcing_chain_regression(void)
{
    /* Seed 0, opening 7/4, previous evaluator: yellow won at move 22.
     * Red's move 11 in column 2 permits a continuous chain of checks.
     * At move 13 every remaining choice is already losing to such a chain. */
    const unsigned moves[] = {6,3,6,2,6,6,1,5,4,4,1,5,5,3,2,3,3,4,5,2,1,2};
    GRand *random = g_rand_new_with_seed(43);
    for (unsigned mirror = 0; mirror < 2; mirror++) {
        for (int color = -1; color <= 1; color += 2) {
            Game game;
            game_init(&game);
            game.turn = color;
            for (unsigned i = 0; i < 10; i++)
                g_assert_true(game_play(&game, mirror ? 6 - moves[i] : moves[i]));
            unsigned losing = mirror ? 5 : 1;
            Score scores[GAME_COLS];
            for (unsigned trial = 0; trial < 16; trial++) {
                game_evaluate(&game, random, scores);
                g_assert_cmpuint(scores[losing].immediate_loss, ==, 0);
                g_assert_cmpuint(scores[losing].opponent_forks, ==, 0);
                g_assert_cmpuint(scores[losing].forcing_loss, ==, 1);
                int best = game_best_move(scores);
                g_assert_cmpint(best, !=, losing);
                g_assert_cmpuint(scores[best].forcing_loss, ==, 0);
                g_assert_cmpuint(scores[losing].forcing_limited, ==, 0);
            }
            GString *history = g_string_new(NULL);
            game_record(history, &game, scores, (unsigned) game_best_move(scores), PLAYER_POSITIONAL);
            g_assert_nonnull(strstr(history->str, "Derrota forçada ×−2e7"));
            g_assert_nonnull(strstr(history->str, "Limite de nós atingido"));
            g_string_free(history, TRUE);
            g_assert_true(game_play(&game, losing));
            game_evaluate(&game, random, scores);
            unsigned attack = mirror ? 1 : 5;
            g_assert_cmpuint(scores[attack].forcing_win, ==, 1);
            g_assert_true(game_play(&game, attack));
            game_evaluate(&game, random, scores);
            for (unsigned col = 0; col < GAME_COLS; col++)
                if (scores[col].legal)
                    g_assert_cmpuint(scores[col].forcing_loss, ==, 1);
        }
    }
    g_rand_free(random);
}

static void test_preparation_regression(void)
{
    /* Minimax depth 6 vs positional, 2026-10-16 18:18:02 and 18:17:50.
     * The losing choices precede two quiet attacking preparations. Waiting
     * until the first immediate threat makes the losses unavoidable. */
    const unsigned short25[] = {0,3,4,3,3,4,4,0,3,3,4,4,0,1,1,1,0};
    const unsigned short29[] = {0,3,4,3,3,4,4,0,3,3,4,4,6,6,6,6,6,0,0,1,1};
    const struct {
        const unsigned *moves;
        unsigned count, losing;
    } cases[] = {
        {short25, G_N_ELEMENTS(short25), 4},
        {short29, G_N_ELEMENTS(short29), 1},
    };
    GRand *random = g_rand_new_with_seed(81);
    for (unsigned k = 0; k < G_N_ELEMENTS(cases); k++) {
        for (unsigned mirror = 0; mirror < 2; mirror++) {
            for (int color = -1; color <= 1; color += 2) {
                Game game;
                game_init(&game);
                game.turn = color;
                for (unsigned i = 0; i < cases[k].count; i++)
                    g_assert_true(game_play(&game, mirror ? 6 - cases[k].moves[i] : cases[k].moves[i]));
                unsigned losing = mirror ? 6 - cases[k].losing : cases[k].losing;
                Score scores[GAME_COLS];
                for (unsigned trial = 0; trial < 8; trial++) {
                    game_evaluate(&game, random, scores);
                    g_assert_cmpuint(scores[losing].immediate_loss, ==, 0);
                    g_assert_cmpuint(scores[losing].opponent_forks, ==, 0);
                    g_assert_cmpuint(scores[losing].prepared_loss, ==, 1);
                    g_assert_cmpuint(scores[losing].forcing_loss, ==, 1);
                    g_assert_cmpuint(scores[losing].forcing_limited, ==, 0);
                    int best = game_best_move(scores);
                    g_assert_cmpint(best, !=, losing);
                    g_assert_cmpuint(scores[best].forcing_loss, ==, 0);
                }
                GString *history = g_string_new(NULL);
                game_record(history, &game, scores, game_best_move(scores), PLAYER_POSITIONAL);
                g_assert_nonnull(strstr(history->str, "Derrota com preparação"));
                g_string_free(history, TRUE);
                g_assert_true(game_play(&game, losing));
                game_evaluate(&game, random, scores);
                unsigned attack = mirror ? 5 : 1;
                g_assert_cmpuint(scores[attack].prepared_win, ==, 1);
                g_assert_cmpuint(scores[attack].forcing_win, ==, 1);
            }
        }
    }
    g_rand_free(random);
}

/* Independent scan of all four-cell windows for generated legal games. */
static int reference_result(const Game *game)
{
    const int dr[] = {0,1,1,1}, dc[] = {1,0,1,-1};
    for (int r = 0; r < 6; r++) {
        for (int c = 0; c < 7; c++) {
            for (unsigned d = 0; d < 4; d++) {
                int end_r = r + 3 * dr[d], end_c = c + 3 * dc[d];
                if (end_r >= 6 || end_c < 0 || end_c >= 7)
                    continue;
                int sum = 0;
                for (int i = 0; i < 4; i++)
                    sum += game->cells[r + i * dr[d]][c + i * dc[d]];
                if (sum == 4 || sum == -4)
                    return sum / 4;
            }
        }
    }
    return game->moves == 42 ? 2 : 0;
}

static void test_generated_games(void)
{
    GRand *random = g_rand_new_with_seed(101);
    for (unsigned trial = 0; trial < 300; trial++) {
        Game game;
        game_init(&game);
        while (!game_result(&game)) {
            Score scores[GAME_COLS];
            game_evaluate(&game, random, scores);
            int best = game_best_move(scores);
            g_assert_cmpint(best, >=, 0);
            gboolean can_win = FALSE, safe = FALSE, fork_safe = FALSE, forcing_safe = FALSE;
            for (unsigned col = 0; col < GAME_COLS; col++) {
                g_assert_cmpint(scores[col].legal, ==, game_drop_row(&game, col) >= 0);
                can_win |= scores[col].win;
                safe |= scores[col].legal && !scores[col].immediate_loss;
                fork_safe |= scores[col].legal && !scores[col].immediate_loss && !scores[col].opponent_forks;
                forcing_safe |= scores[col].legal && !scores[col].forcing_loss;
                g_assert_false(scores[col].forcing_win && scores[col].forcing_loss);
            }
            if (can_win)
                g_assert_cmpuint(scores[best].win, ==, 1);
            if (safe)
                g_assert_cmpuint(scores[best].immediate_loss, ==, 0);
            if (fork_safe)
                g_assert_cmpuint(scores[best].opponent_forks, ==, 0);
            if (forcing_safe)
                g_assert_cmpuint(scores[best].forcing_loss, ==, 0);
            unsigned col = best;
            if (trial % 2) {
                do { col = g_rand_int_range(random, 0, GAME_COLS); } while (!scores[col].legal);
            }
            int row = game_drop_row(&game, col), player = game.turn;
            g_assert_true(game_play(&game, col));
            g_assert_cmpint(game.cells[row][col], ==, player);
            g_assert_cmpint(game_result(&game), ==, reference_result(&game));
        }
        g_assert_cmpuint(game.moves, <=, 42);
    }
    g_rand_free(random);
}

static void test_history(void)
{
    Game game;
    game_init(&game);
    GRand *random = g_rand_new_with_seed(9);
    GString *history = g_string_new(NULL);
    unsigned moves[] = {0,6,1,6,2,5,3};
    for (unsigned i = 0; i < G_N_ELEMENTS(moves); i++) {
        Score scores[GAME_COLS];
        game_evaluate(&game, random, scores);
        game_record(history, &game, scores, moves[i], i % 2 == 0 ? PLAYER_HUMAN : PLAYER_POSITIONAL);
        game_play(&game, moves[i]);
    }
    g_assert_nonnull(strstr(history->str, "Jogada 1 — Vermelho (Humano)"));
    g_assert_nonnull(strstr(history->str, "Jogada 2 — Amarelo (Posicional)"));
    g_assert_nonnull(strstr(history->str, "Escolha: coluna **4**, linha 6"));
    g_assert_nonnull(strstr(history->str, "Respostas com garfo"));
    GDateTime *date = g_date_time_new_from_iso8601("2026-10-16T17:30:00-03:00", NULL);
    char *report = game_report(&game, history->str, "Vermelho: humano; Amarelo: automático.", date);
    g_assert_nonnull(strstr(report, "2026-10-16 17:30:00 -03:00"));
    g_assert_nonnull(strstr(report, "Vencedor: **Vermelho**"));
    GError *error = NULL;
    char *directory = g_dir_make_tmp("connect-4-test-XXXXXX", &error);
    g_assert_no_error(error);
    char *path = g_build_filename(directory, "histórico-connect-4.md", NULL);
    for (unsigned i = 0; i < 2; i++) {
        g_assert_true(game_append_report(path, report, &error));
        g_assert_no_error(error);
    }
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
    game_init(&game);
    g_assert_null(game_report(&game, "", "", date));
    play_sequence(&game, draw_moves, G_N_ELEMENTS(draw_moves));
    g_assert_null(game_report(&game, "", "", date));
    g_remove(path);
    g_rmdir(directory);
    g_free(saved);
    g_free(path);
    g_free(directory);
    g_free(report);
    g_date_time_unref(date);
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
    char *directory = g_dir_make_tmp("connect-4-partial-XXXXXX", &error);
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
    g_test_add_func("/connect4/gravity", test_gravity);
    g_test_add_func("/connect4/wins", test_wins);
    g_test_add_func("/connect4/draw", test_draw);
    g_test_add_func("/connect4/opening", test_opening);
    g_test_add_func("/connect4/tactics", test_tactics);
    g_test_add_func("/connect4/positional", test_positional);
    g_test_add_func("/connect4/accessibility", test_accessibility);
    g_test_add_func("/connect4/stacked-regression", test_stacked_regression);
    g_test_add_func("/connect4/forcing-chain-regression", test_forcing_chain_regression);
    g_test_add_func("/connect4/preparation-regression", test_preparation_regression);
    g_test_add_func("/connect4/generated-games", test_generated_games);
    g_test_add_func("/connect4/history", test_history);
    g_test_add_func("/connect4/partial-write-retry", test_partial_write);
    return g_test_run();
}
