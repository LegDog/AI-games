// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>

typedef enum {
  I18N_SYSTEM,
  I18N_ENGLISH,
  I18N_PORTUGUESE,
  I18N_SPANISH,
  I18N_ITALIAN,
  I18N_LANGUAGE_COUNT
} I18nLanguage;

/* Embedded catalogs support live switching without changing process locales
 * or environment variables while computation/HTTP workers are running. */
const char *i18n_translate(const char *message) G_GNUC_FORMAT(1);
#define _(message) i18n_translate(message)
I18nLanguage i18n_detect(const char *const *languages);
void i18n_set_language(I18nLanguage selection);
I18nLanguage i18n_language(void);
I18nLanguage i18n_selection(void);
const char *i18n_language_code(I18nLanguage language);
gboolean i18n_load(const char *path, GError **error);
/* Read the legacy preference only when the new path is absent. */
gboolean i18n_load_with_legacy(const char *path, const char *legacy_path,
                               GError **error);
gboolean i18n_save(const char *path, GError **error);
