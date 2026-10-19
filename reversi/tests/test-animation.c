// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation.h"
static void phases(void)
{
    Animation a; animation_start(&a, 1000000, 2);
    AnimationFrame f = animation_frame(&a, 1000000);
    g_assert_cmpfloat(f.position, ==, 0); g_assert_false(f.finished);
    f = animation_frame(&a, 1250000); g_assert_cmpfloat(f.opacity, <, 1); g_assert_cmpfloat(f.position, ==, 0);
    f = animation_frame(&a, 2000000); g_assert_cmpfloat(f.position, ==, 0); g_assert_cmpfloat(f.opacity, ==, 1);
    f = animation_frame(&a, 2500000); g_assert_cmpfloat(f.position, ==, .5);
    f = animation_frame(&a, 3000000); g_assert_cmpfloat(f.position, ==, 1); g_assert_true(f.finished);
    animation_start(&a, 0, 0); g_assert_true(animation_frame(&a, 0).finished);
}
static void pause_resume(void)
{
    for (unsigned phase = 0; phase < 2; phase++) {
        Animation a; animation_start(&a, 0, 2);
        gint64 pause = phase ? 1500000 : 250000;
        animation_pause(&a, pause);
        AnimationFrame before = animation_frame(&a, pause), after = animation_frame(&a, pause + 10000000);
        g_assert_cmpfloat(before.position, ==, after.position); g_assert_cmpfloat(before.opacity, ==, after.opacity);
        animation_resume(&a, pause + 10000000);
        after = animation_frame(&a, pause + 10000000);
        g_assert_cmpfloat(before.position, ==, after.position); g_assert_cmpfloat(before.opacity, ==, after.opacity);
        g_assert_true(animation_frame(&a, 12000000).finished);
    }
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/animation/phases", phases); g_test_add_func("/animation/pause", pause_resume);
    return g_test_run();
}
