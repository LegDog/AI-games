// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n.h"
#include <glib/gstdio.h>
#include <errno.h>
#include <string.h>

static const char * const catalog[][4] = {
#include "i18n-catalog.inc"
};
static const char * const codes[] = {"system", "en", "pt", "es", "it"};
static gint selected_language = I18N_SYSTEM;
static gint actual_language = I18N_ENGLISH;

const char *i18n_translate(const char *message)
{
    if (!message) return "";
    unsigned language = (unsigned) i18n_language() - 1;
    for (unsigned row = 0; row < G_N_ELEMENTS(catalog); row++) {
        /* Also accept saved diagnostic text in any supported language, so
         * static errors already displayed can follow a live language change. */
        for (unsigned col = 0; col < 4; col++)
            if (g_str_equal(message, catalog[row][col])) return catalog[row][language];
    }
    return message; /* Names, external errors and unknown messages stay intact. */
}

I18nLanguage i18n_detect(const char * const *languages)
{
    if (languages) {
        for (unsigned i = 0; languages[i]; i++) {
            const char *name = languages[i];
            if (g_str_equal(name, "C") || g_str_has_prefix(name, "C.") || g_str_equal(name, "POSIX"))
                return I18N_ENGLISH;
            for (unsigned language = I18N_ENGLISH; language < I18N_LANGUAGE_COUNT; language++) {
                if (strlen(name) >= 2 && g_ascii_strncasecmp(name, codes[language], 2) == 0 &&
                    (name[2] == '\0' || strchr("_-@.", name[2])))
                    return (I18nLanguage) language;
            }
        }
    }
    return I18N_ENGLISH;
}

void i18n_set_language(I18nLanguage selection)
{
    if (selection < I18N_SYSTEM || selection >= I18N_LANGUAGE_COUNT) selection = I18N_SYSTEM;
    I18nLanguage actual = selection == I18N_SYSTEM ? i18n_detect(g_get_language_names()) : selection;
    g_atomic_int_set(&selected_language, selection);
    g_atomic_int_set(&actual_language, actual);
}

I18nLanguage i18n_language(void) { return (I18nLanguage) g_atomic_int_get(&actual_language); }
I18nLanguage i18n_selection(void) { return (I18nLanguage) g_atomic_int_get(&selected_language); }

const char *i18n_language_code(I18nLanguage language)
{
    return language >= I18N_SYSTEM && language < I18N_LANGUAGE_COUNT ? codes[language] : "system";
}

gboolean i18n_load(const char *path, GError **error)
{
    i18n_set_language(I18N_SYSTEM);
    if (!path) return TRUE;
    g_autoptr(GKeyFile) file = g_key_file_new();
    GError *failure = NULL;
    if (!g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, &failure)) {
        if (g_error_matches(failure, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
            g_clear_error(&failure);
            return TRUE;
        }
        g_propagate_error(error, failure);
        return FALSE;
    }
    g_autofree char *code = g_key_file_get_string(file, "Interface", "Language", NULL);
    for (unsigned i = 0; i < G_N_ELEMENTS(codes); i++) {
        if (g_strcmp0(code, codes[i]) == 0) {
            i18n_set_language((I18nLanguage) i);
            break;
        }
    }
    return TRUE;
}

gboolean i18n_load_with_legacy(const char *path, const char *legacy_path, GError **error)
{
    if (path && legacy_path) {
        struct stat info;
        /* Do not mask permission errors or malformed new preferences with an
         * older setting. Fall back only when the new file is absent. */
        if (g_stat(path, &info) < 0 && errno == ENOENT)
            return i18n_load(legacy_path, error);
    }
    return i18n_load(path, error);
}

gboolean i18n_save(const char *path, GError **error)
{
    if (!path) return TRUE;
    g_autofree char *directory = g_path_get_dirname(path);
    if (g_mkdir_with_parents(directory, 0700) < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "%s: %s", directory, g_strerror(errno));
        return FALSE;
    }
    g_autoptr(GKeyFile) file = g_key_file_new();
    g_key_file_set_string(file, "Interface", "Language", i18n_language_code(i18n_selection()));
    gsize size;
    g_autofree char *contents = g_key_file_to_data(file, &size, NULL);
    return g_file_set_contents(path, contents, size, error);
}
