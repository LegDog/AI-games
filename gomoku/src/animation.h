// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <glib.h>
typedef struct {
  gint64 started, paused_at, duration;
  gboolean active, paused;
} Animation;
typedef struct {
  double position, opacity;
  gboolean finished;
} AnimationFrame;
void animation_start(Animation *animation, gint64 now, double seconds);
void animation_pause(Animation *animation, gint64 now);
void animation_resume(Animation *animation, gint64 now);
AnimationFrame animation_frame(const Animation *animation, gint64 now);
