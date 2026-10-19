// SPDX-License-Identifier: GPL-3.0-or-later
/* Exercise the private logger without requiring HTTP sockets, using the same
 * source inclusion approach as the application's UI lifecycle tests. */
#include "../src/ollama.c"
#include <glib/gstdio.h>

static char *log_path;

static void test_payloads(void)
{
    g_autoptr(SoupMessage) message = soup_message_new("POST", OLLAMA_DEFAULT_URL "/api/generate");
    soup_message_headers_append(soup_message_get_request_headers(message), "Content-Type", "application/json");
    const char *request = "{\"model\":\"cogito:3b\",\"prompt\":\"board\\nfull prompt\",\"stream\":false}";
    const char *response = "{\"done\":true,\"thinking\":\"Verificar coluna 4.\\nBloquear ameaça.\",\"response\":\"<think>análise</think>{\\\"move\\\":4}\"}";
    g_autoptr(GBytes) sent = g_bytes_new_static(request, strlen(request));
    g_autoptr(GBytes) received_body = g_bytes_new_static(response, strlen(response));
    gint64 start = g_get_monotonic_time();
    log_exchange("paired-id", "request", "POST", OLLAMA_DEFAULT_URL "/api/generate", message, sent, start, NULL);
    log_exchange("paired-id", "response", "POST", OLLAMA_DEFAULT_URL "/api/generate", message, received_body, start, NULL);
    log_exchange("failed-id", "timeout", "GET", OLLAMA_DEFAULT_URL "/api/tags", message, NULL, start, "Prazo esgotado");
    const guint8 binary[] = {0xff, 0, 0xfe};
    g_autoptr(GBytes) invalid_body = g_bytes_new_static(binary, sizeof binary);
    log_exchange("binary-id", "response", "POST", OLLAMA_DEFAULT_URL, message, invalid_body, start, NULL);
    g_autofree char *contents = NULL;
    GError *error = NULL;
    g_assert_true(g_file_get_contents(log_path, &contents, NULL, &error));
    g_assert_no_error(error);
    g_auto(GStrv) lines = g_strsplit(contents, "\n", -1);
    g_assert_cmpuint(g_strv_length(lines), ==, 5);
    for (unsigned i = 0; i < 4; i++) {
        g_autoptr(JsonParser) parser = json_parser_new();
        g_assert_true(json_parser_load_from_data(parser, lines[i], -1, &error));
        g_assert_no_error(error);
        JsonObject *entry = json_node_get_object(json_parser_get_root(parser));
        g_assert_cmpstr(json_object_get_string_member(entry, "game"), ==,
            GAME_ID);
        g_assert_cmpint(json_object_get_int_member(entry, "pid"), ==, getpid());
        g_assert_cmpint(json_object_get_int_member(entry, "elapsed_us"), >=, 0);
        g_autoptr(GDateTime) date = g_date_time_new_from_iso8601(json_object_get_string_member(entry, "timestamp"), NULL);
        g_assert_nonnull(date);
        if (i < 2) {
            g_assert_cmpstr(json_object_get_string_member(entry, "id"), ==, "paired-id");
            g_assert_cmpstr(json_object_get_string_member(entry, "body"), ==, i == 0 ? request : response);
        }
        if (i == 1) {
            g_autoptr(JsonParser) body_parser = json_parser_new();
            g_assert_true(json_parser_load_from_data(body_parser, json_object_get_string_member(entry, "body"), -1, &error));
            JsonObject *body = json_node_get_object(json_parser_get_root(body_parser));
            g_assert_cmpstr(json_object_get_string_member(body, "thinking"), ==, "Verificar coluna 4.\nBloquear ameaça.");
            g_assert_nonnull(strstr(json_object_get_string_member(body, "response"), "<think>análise</think>"));
        }
        if (i == 2)
            g_assert_cmpstr(json_object_get_string_member(entry, "error"), ==, "Prazo esgotado");
        if (i == 3) {
            gsize size;
            g_autofree guchar *decoded = g_base64_decode(json_object_get_string_member(entry, "body_base64"), &size);
            g_assert_cmpmem(decoded, size, binary, sizeof binary);
        }
    }
    g_autofree char *error_contents = NULL;
    g_assert_true(g_file_get_contents(g_getenv("OLLAMA_ERROR_LOG_FILE"), &error_contents, NULL, &error));
    g_assert_no_error(error);
    g_auto(GStrv) error_lines = g_strsplit(error_contents, "\n", -1);
    g_assert_cmpuint(g_strv_length(error_lines), ==, 2);
    g_assert_cmpint(g_remove(g_getenv("OLLAMA_ERROR_LOG_FILE")), ==, 0);
    g_assert_cmpint(g_remove(log_path), ==, 0);
}

static gpointer append_thread(gpointer data)
{
    for (unsigned i = 0; i < 16; i++) {
        g_autofree char *id = g_strdup_printf("writer-%u-%u", GPOINTER_TO_UINT(data), i);
        log_exchange(id, "error", "GET", OLLAMA_DEFAULT_URL "/api/tags", NULL, NULL,
                     g_get_monotonic_time(), "Erro de conexão");
    }
    return NULL;
}

static void test_concurrent_append(void)
{
    GThread *first = g_thread_new("log-first", append_thread, GUINT_TO_POINTER(1));
    GThread *second = g_thread_new("log-second", append_thread, GUINT_TO_POINTER(2));
    g_thread_join(first);
    g_thread_join(second);
    g_autofree char *contents = NULL;
    GError *error = NULL;
    g_assert_true(g_file_get_contents(log_path, &contents, NULL, &error));
    g_assert_no_error(error);
    g_auto(GStrv) lines = g_strsplit(contents, "\n", -1);
    g_assert_cmpuint(g_strv_length(lines), ==, 33);
    g_autoptr(GHashTable) ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (unsigned i = 0; i < 32; i++) {
        g_autoptr(JsonParser) parser = json_parser_new();
        g_assert_true(json_parser_load_from_data(parser, lines[i], -1, &error));
        g_assert_no_error(error);
        JsonObject *entry = json_node_get_object(json_parser_get_root(parser));
        g_assert_true(g_hash_table_add(ids, g_strdup(json_object_get_string_member(entry, "id"))));
    }
    g_assert_cmpuint(g_hash_table_size(ids), ==, 32);
    g_autofree char *error_contents = NULL;
    g_assert_true(g_file_get_contents(g_getenv("OLLAMA_ERROR_LOG_FILE"), &error_contents, NULL, &error));
    g_assert_no_error(error);
    g_auto(GStrv) error_lines = g_strsplit(error_contents, "\n", -1);
    g_assert_cmpuint(g_strv_length(error_lines), ==, 33);
    g_assert_cmpint(g_remove(g_getenv("OLLAMA_ERROR_LOG_FILE")), ==, 0);
    g_assert_cmpint(g_remove(log_path), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    GError *error = NULL;
    g_autofree char *directory = g_dir_make_tmp("tavli-log-XXXXXX", &error);
    g_assert_no_error(error);
    log_path = g_build_filename(directory, OLLAMA_DEFAULT_LOG_FILE, NULL);
    g_autofree char *error_path = g_build_filename(directory, OLLAMA_DEFAULT_ERROR_LOG_FILE, NULL);
    g_setenv("OLLAMA_LOG_FILE", log_path, TRUE);
    g_setenv("OLLAMA_ERROR_LOG_FILE", error_path, TRUE);
    g_test_add_func("/ollama-log/payloads", test_payloads);
    g_test_add_func("/ollama-log/concurrent", test_concurrent_append);
    int status = g_test_run();
    g_remove(log_path); g_remove(error_path); g_rmdir(directory); g_free(log_path);
    return status;
}
