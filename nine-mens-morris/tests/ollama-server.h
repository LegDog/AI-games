// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <stdio.h>

/* Real HTTP protocol against a local deterministic server. No installed model
 * or running Ollama is needed. The worker owns its private request context. */
typedef struct {
  SoupServer *server;
  char *url;
  const char *body;
  guint status;
  gboolean delayed;
  JsonObject *request;
  gboolean auto_move;
  GPtrArray *paused;
  unsigned calls, show_calls, generate_calls;
  const char *info_body;
  GPtrArray *requests;
} Fixture;

static void handle(SoupServer *server, SoupServerMessage *message,
                   const char *path, GHashTable *query, gpointer data) {
  (void)server;
  (void)query;
  Fixture *f = data;
  f->calls++;
  if (g_str_equal(path, "/api/generate") || g_str_equal(path, "/api/show")) {
    g_assert_cmpstr(soup_server_message_get_method(message), ==, "POST");
    SoupMessageBody *body = soup_server_message_get_request_body(message);
    g_autoptr(JsonParser) parser = json_parser_new();
    GError *error = NULL;
    g_assert_true(
        json_parser_load_from_data(parser, body->data, body->length, &error));
    g_assert_no_error(error);
    g_clear_pointer(&f->request, json_object_unref);
    f->request =
        json_object_ref(json_node_get_object(json_parser_get_root(parser)));
    g_ptr_array_add(f->requests, json_object_ref(f->request));
    if (g_str_equal(path, "/api/show"))
      f->show_calls++;
    else
      f->generate_calls++;
  } else {
    g_assert_cmpstr(path, ==, "/api/tags");
    g_assert_cmpstr(soup_server_message_get_method(message), ==, "GET");
  }
  g_autofree char *generated = NULL;
  if (f->auto_move && g_str_equal(path, "/api/generate")) {
    if (!json_object_has_member(f->request, "format")) {
      generated = g_strdup("{\"done\":true,\"response\":\"<think>Check "
                           "threats.</think> Choose an allowed move.\"}");
    } else {
      const char *prompt = json_object_get_string_member(f->request, "prompt");
      const char *start = strstr(prompt, "Allowed moves:\n");
      char square[16];
      g_assert_nonnull(start);
      start = strchr(start, '\n') + 1;
      g_assert_cmpint(sscanf(start, "%15s", square), ==, 1);
      generated = g_strdup_printf(
          "{\"done\":true,\"response\":\"{\\\"move\\\":\\\"%s\\\"}\"}", square);
    }
  }
  if (g_str_equal(path, "/api/show")) {
    g_free(generated);
    generated = g_strdup(f->info_body ? f->info_body : "{}");
  }
  const char *response = generated ? generated : f->body;
  soup_server_message_set_status(message, f->status, NULL);
  soup_server_message_set_response(message, "application/json",
                                   SOUP_MEMORY_COPY, response,
                                   strlen(response));
  if (f->delayed) {
    soup_server_message_pause(message);
    g_ptr_array_add(f->paused, g_object_ref(message));
  }
}

static void setup(Fixture *f, gconstpointer data) {
  (void)data;
  f->paused = g_ptr_array_new_with_free_func(g_object_unref);
  f->requests =
      g_ptr_array_new_with_free_func((GDestroyNotify)json_object_unref);
  f->server = soup_server_new(NULL, NULL);
  soup_server_add_handler(f->server, NULL, handle, f, NULL);
  GError *error = NULL;
  if (!soup_server_listen_local(f->server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY,
                                &error)) {
    g_test_skip_printf("Local HTTP server unavailable in this environment: %s",
                       error->message);
    g_clear_error(&error);
    return;
  }
  GSList *uris = soup_server_get_uris(f->server);
  char *url = g_uri_to_string(uris->data);
  f->url = g_strndup(url, strlen(url) - 1); /* Remove trailing slash. */
  g_free(url);
  g_slist_free_full(uris, (GDestroyNotify)g_uri_unref);
  f->status = 200;
}

static void teardown(Fixture *f, gconstpointer data) {
  (void)data;
  for (unsigned i = 0; i < f->paused->len; i++)
    soup_server_message_unpause(g_ptr_array_index(f->paused, i));
  g_ptr_array_unref(f->paused);
  g_ptr_array_unref(f->requests);
  soup_server_disconnect(f->server);
  g_object_unref(f->server);
  g_free(f->url);
  g_clear_pointer(&f->request, json_object_unref);
}
