// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include <glib/gstdio.h>

static const char * const translations[][4] = {
#include "i18n-catalog.inc"
};

static void test_detection(void)
{
    const struct { const char *names[4]; I18nLanguage expected; } cases[] = {
        {{"pt_BR.UTF-8", NULL}, I18N_PORTUGUESE},
        {{"pt-PT", NULL}, I18N_PORTUGUESE},
        {{"es_MX", NULL}, I18N_SPANISH},
        {{"it_IT@euro", NULL}, I18N_ITALIAN},
        {{"en_US", NULL}, I18N_ENGLISH},
        {{"de_DE", "es_ES", "C", NULL}, I18N_SPANISH},
        {{"fr_FR", "ja_JP", "C", NULL}, I18N_ENGLISH},
        {{"C.UTF-8", NULL}, I18N_ENGLISH},
        {{"POSIX", NULL}, I18N_ENGLISH},
        {{"portuguese", "italian", "es", NULL}, I18N_SPANISH},
        {{"PT_br", NULL}, I18N_PORTUGUESE},
    };
    for (unsigned i = 0; i < G_N_ELEMENTS(cases); i++)
        g_assert_cmpint(i18n_detect(cases[i].names), ==, cases[i].expected);
    g_assert_cmpint(i18n_detect(NULL), ==, I18N_ENGLISH);
    i18n_set_language(I18N_SYSTEM);
    g_assert_cmpint(i18n_selection(), ==, I18N_SYSTEM);
    g_assert_cmpint(i18n_language(), ==, i18n_detect(g_get_language_names()));
    g_test_message("System language: %s", i18n_language_code(i18n_language()));
    i18n_set_language(I18N_ENGLISH);
}

static char *format_signature(const char *text)
{
    g_autoptr(GRegex) pattern = g_regex_new("%[0-9.]*[scudiufg]", 0, 0, NULL);
    GMatchInfo *matches = NULL;
    g_regex_match(pattern, text, 0, &matches);
    GString *signature = g_string_new(NULL);
    while (g_match_info_matches(matches)) {
        g_autofree char *part = g_match_info_fetch(matches, 0);
        g_string_append(signature, part);
        g_match_info_next(matches, NULL);
    }
    g_match_info_free(matches);
    return g_string_free(signature, FALSE);
}

static void test_catalog(void)
{
    for (unsigned row = 0; row < G_N_ELEMENTS(translations); row++) {
        g_autofree char *reference = format_signature(translations[row][0]);
        for (unsigned col = 0; col < 4; col++) {
            const char *text = translations[row][col];
            g_assert_true(g_utf8_validate(text, -1, NULL));
            g_assert_cmpstr(text, !=, "");
            g_autofree char *signature = format_signature(text);
            g_assert_cmpstr(signature, ==, reference);
            i18n_set_language((I18nLanguage) (col + 1));
            g_assert_cmpstr(_(translations[row][0]), ==, text);
            g_assert_cmpstr(_(translations[row][1]), ==, text);
        }
    }
    i18n_set_language(I18N_ENGLISH);
    g_assert_cmpstr(_("Human"), ==, "Human");
    g_assert_cmpstr(_("model-name:tag"), ==, "model-name:tag");
    i18n_set_language(I18N_SPANISH);
    g_assert_cmpstr(_("Start game"), ==, "Iniciar partida");
    i18n_set_language(I18N_ITALIAN);
    g_assert_cmpstr(_("New configuration"), ==, "Nuova configurazione");
    i18n_set_language(I18N_ENGLISH);
}

static void test_preferences(void)
{
    GError *error = NULL;
    g_autofree char *directory = g_dir_make_tmp("ai-games-i18n-XXXXXX", &error);
    g_assert_no_error(error);
    g_autofree char *parent = g_build_filename(directory, "nested", NULL);
    g_autofree char *path = g_build_filename(parent, "settings.ini", NULL);
    i18n_set_language(I18N_SPANISH);
    g_assert_true(i18n_save(path, &error));
    g_assert_no_error(error);
    i18n_set_language(I18N_ITALIAN);
    g_assert_true(i18n_load(path, &error));
    g_assert_no_error(error);
    g_assert_cmpint(i18n_selection(), ==, I18N_SPANISH);
    g_assert_cmpint(i18n_language(), ==, I18N_SPANISH);
    g_assert_true(g_file_set_contents(path, "[Interface]\nLanguage=unsupported\n", -1, &error));
    g_assert_no_error(error);
    g_assert_true(i18n_load(path, &error));
    g_assert_no_error(error);
    g_assert_cmpint(i18n_selection(), ==, I18N_SYSTEM);
    g_assert_true(g_file_set_contents(path, "broken ini", -1, &error));
    g_assert_no_error(error);
    g_assert_false(i18n_load(path, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_assert_cmpint(i18n_selection(), ==, I18N_SYSTEM);
    g_remove(path);
    g_assert_true(i18n_load(path, &error));
    g_assert_no_error(error);
    g_assert_cmpint(i18n_language(), ==, i18n_detect(g_get_language_names()));
    g_assert_false(i18n_save(parent, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_rmdir(parent);
    g_rmdir(directory);
    i18n_set_language(I18N_ENGLISH);
}

static void test_legacy_preferences(void)
{
    GError *error = NULL;
    g_autofree char *directory = g_dir_make_tmp("ai-games-legacy-XXXXXX", &error);
    g_assert_no_error(error);
    g_autofree char *path = g_build_filename(directory, "new.ini", NULL);
    g_autofree char *legacy = g_build_filename(directory, "old.ini", NULL);
    i18n_set_language(I18N_PORTUGUESE);
    g_assert_true(i18n_save(legacy, &error));
    g_assert_true(i18n_load_with_legacy(path, legacy, &error));
    g_assert_no_error(error);
    g_assert_cmpint(i18n_selection(), ==, I18N_PORTUGUESE);
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    i18n_set_language(I18N_ITALIAN);
    g_assert_true(i18n_save(path, &error));
    g_assert_true(i18n_load_with_legacy(path, legacy, &error));
    g_assert_no_error(error);
    g_assert_cmpint(i18n_selection(), ==, I18N_ITALIAN);
    g_assert_true(g_file_set_contents(path, "broken", -1, &error));
    g_assert_false(i18n_load_with_legacy(path, legacy, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_assert_cmpint(i18n_selection(), ==, I18N_SYSTEM);
    g_remove(path);
    g_remove(legacy);
    g_assert_true(i18n_load_with_legacy(path, legacy, &error));
    g_assert_no_error(error);
    g_assert_cmpint(i18n_selection(), ==, I18N_SYSTEM);
    g_rmdir(directory);
    i18n_set_language(I18N_ENGLISH);
}

static gpointer read_translations(gpointer data)
{
    (void) data;
    for (unsigned i = 0; i < 1000; i++) {
        const char *text = _("Start game");
        g_assert_true(g_str_equal(text, "Start game") || g_str_equal(text, "Iniciar partida") ||
                      g_str_equal(text, "Inizia partita"));
    }
    return NULL;
}

static void test_concurrent_switch(void)
{
    i18n_set_language(I18N_ENGLISH);
    GThread *thread = g_thread_new("translation-reader", read_translations, NULL);
    for (unsigned i = 0; i < 1000; i++)
        i18n_set_language((I18nLanguage) (1 + i % 4));
    g_thread_join(thread);
    i18n_set_language(I18N_ENGLISH);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/i18n/system-default", test_detection);
    g_test_add_func("/i18n/catalog-format-placeholders", test_catalog);
    g_test_add_func("/i18n/persistent-preference", test_preferences);
    g_test_add_func("/i18n/legacy-preference", test_legacy_preferences);
    g_test_add_func("/i18n/concurrent-switch", test_concurrent_switch);
    return g_test_run();
}
