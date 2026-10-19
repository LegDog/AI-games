// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ollama-events.h"
#include <adwaita.h>
typedef const char *(*OllamaTranslate)(const char *text);
/* Install once after controller widgets exist; the main window owns the viewer.
 */
GtkWidget *ollama_view_install(GtkWindow *parent, GtkWidget *first,
                               GtkWidget *second, unsigned ollama_kind,
                               OllamaTranslate translate);
void ollama_view_begin(GtkWindow *parent);
void ollama_view_clear(GtkWindow *parent);
void ollama_view_invalidate(GtkWindow *parent);
void ollama_view_undo(GtkWindow *parent);
void ollama_view_bind(GtkWindow *parent, GCancellable *operation,
                      const char *actor, unsigned turn, const char *model);
void ollama_view_retranslate(GtkWindow *parent);
