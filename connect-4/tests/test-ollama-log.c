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
            GAME_CHOICES == 7 ? "connect-4" : "jogo-da-velha");
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
    g_assert_cmpint(g_remove(log_path), ==, 0);
}

static void test_reasoning_policy(void)
{
    const char *inputs[] = {
        "{\"thinking\":{\"values\":[false,true],\"default\":false}}",
        "{\"thinking\":{\"values\":[\"low\",\"medium\",\"high\"],\"default\":\"medium\"}}",
        "{\"thinking\":{\"values\":[\"fast\",\"slow\"],\"default\":\"slow\"}}",
        "{\"thinking\":{\"values\":[false],\"default\":false}}", "{}",
        "{\"thinking\":null}", "{\"thinking\":{\"values\":42}}",
        "{\"thinking\":{\"values\":[null,42,false],\"default\":true}}"
    };
    for (unsigned i = 0; i < G_N_ELEMENTS(inputs); i++) {
        g_autoptr(JsonObject) info = parse_object(inputs[i], NULL);
        g_autoptr(JsonNode) control = thinking_control(info);
        if (i == 0) {
            g_assert_nonnull(control);
            g_assert_cmpuint(json_node_get_value_type(control), ==, G_TYPE_BOOLEAN);
            g_assert_true(json_node_get_boolean(control));
        } else if (i == 1 || i == 2) {
            g_assert_nonnull(control);
            g_assert_cmpstr(json_node_get_string(control), ==, i == 1 ? "high" : "slow");
        } else g_assert_null(control);
    }
    g_assert_true(cogito_v1("cogito:3b"));
    g_assert_true(cogito_v1("namespace/cogito:8b"));
    g_assert_true(cogito_v1("cogito"));
    g_assert_false(cogito_v1("cogito-2.1:latest"));
    g_assert_false(cogito_v1("qwen3:8b"));
    GError *error = NULL;
    g_assert_cmpuint(remaining_time(g_get_monotonic_time() - 2000000, 100, &error), ==, 0);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT);
    g_clear_error(&error);
    g_assert_cmpuint(remaining_time(g_get_monotonic_time(), 100, &error), >, 0);
    g_assert_no_error(error);
    g_autoptr(JsonObject) schema = move_schema((1u << 0) | (1u << 2));
    JsonObject *prop = json_object_get_object_member(json_object_get_object_member(schema, "properties"), "move");
    JsonArray *allowed = json_object_get_array_member(prop, "enum");
    g_assert_cmpuint(json_array_get_length(allowed), ==, 2);
    g_assert_cmpint(json_array_get_int_element(allowed, 0), ==, ollama_move_number(0));
    g_assert_cmpint(json_array_get_int_element(allowed, 1), ==, ollama_move_number(2));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    GError *error = NULL;
    char *directory = g_dir_make_tmp("ollama-log-test-XXXXXX", &error);
    g_assert_no_error(error);
    log_path = g_build_filename(directory, "ollama.log", NULL);
    g_setenv("OLLAMA_LOG_FILE", log_path, TRUE);
    g_test_add_func("/ollama-policy/reasoning-deadline-schema", test_reasoning_policy);
    g_test_add_func("/ollama-log/full-payload-thinking-errors", test_payloads);
    g_test_add_func("/ollama-log/concurrent-append", test_concurrent_append);
    int result = g_test_run();
    g_remove(log_path);
    g_rmdir(directory);
    g_free(log_path);
    g_free(directory);
    return result;
}
