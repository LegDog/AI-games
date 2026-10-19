// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation.h"
#include <math.h>
void animation_start(Animation *a, gint64 now, double seconds) {
  *a = (Animation){.started = now,
                   .duration = (gint64)(MAX(0, seconds) * 1000000),
                   .active = TRUE};
}
void animation_pause(Animation *a, gint64 now) {
  if (a->active && !a->paused) {
    a->paused_at = now;
    a->paused = TRUE;
  }
}
void animation_resume(Animation *a, gint64 now) {
  if (a->active && a->paused) {
    a->started += now - a->paused_at;
    a->paused = FALSE;
  }
}
AnimationFrame animation_frame(const Animation *a, gint64 now) {
  double progress =
      a->duration
          ? CLAMP((double)((a->paused ? a->paused_at : now) - a->started) /
                      a->duration,
                  0, 1)
          : 1;
  AnimationFrame frame = {.opacity = 1, .finished = progress >= 1};
  if (progress < .5)
    frame.opacity = .25 + .75 * (1 + cos(progress * 12 * G_PI)) / 2;
  else
    frame.position = (progress - .5) * 2;
  return frame;
}
