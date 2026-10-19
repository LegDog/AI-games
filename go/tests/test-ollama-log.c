// SPDX-License-Identifier: GPL-3.0-or-later
#include "ollama.h"
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
static void logs(void) {
  char *directory = g_dir_make_tmp("go-logs-XXXXXX", NULL);
  char *full = g_build_filename(directory, "full.log", NULL),
       *failure = g_build_filename(directory, "errors.log", NULL);
  g_setenv("OLLAMA_LOG_FILE", full, TRUE);
  g_setenv("OLLAMA_ERROR_LOG_FILE", failure, TRUE);
  Game g;
  game_init(&g, 7.5);
  OllamaResult result;
  GError *error = NULL;
  g_assert_false(ollama_choose_with_options(&g, "mock", "not a URI", 100, NULL,
                                            NULL, &result, &error));
  g_assert_nonnull(error);
  g_clear_error(&error);
  char *text = NULL;
  g_assert_true(g_file_get_contents(full, &text, NULL, NULL));
  g_assert_nonnull(strstr(text, "go"));
  g_assert_nonnull(strstr(text, "operation_id"));
  g_free(text);
  g_assert_true(g_file_get_contents(failure, &text, NULL, NULL));
  g_assert_nonnull(strstr(text, "error"));
  g_free(text);
  g_remove(full);
  g_remove(failure);
  g_rmdir(directory);
  g_free(directory);
  g_free(full);
  g_free(failure);
  game_clear(&g);
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/ollama/logs", logs);
  return g_test_run();
}
