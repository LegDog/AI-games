// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#define main tavli_application_main
#include "../src/main.c"
#undef main
static char *test_directory;
static void pump(unsigned milliseconds)
{
    gint64 end=g_get_monotonic_time()+milliseconds*1000;
    do {while(g_main_context_iteration(NULL,FALSE)){}g_usleep(1000);}while(g_get_monotonic_time()<end);
}
static void G_GNUC_UNUSED settle_worker(App *app)
{
    gint64 end=g_get_monotonic_time()+5000000;while(app->task && g_get_monotonic_time()<end)pump(1);g_assert_null(app->task);
}
static void ui_setup(App *app,gconstpointer data)
{
    (void)data;app_init(app);g_free(app->url);app->url=g_strdup("http://127.0.0.1:1");g_free(app->preferences);app->preferences=g_build_filename(test_directory,"settings.ini",NULL);i18n_set_language(I18N_ENGLISH);
    static unsigned id=0;g_autofree char *name=g_strdup_printf("io.github.aigames.Tavli.Test%u",++id);app->application=GTK_APPLICATION(adw_application_new(name,G_APPLICATION_NON_UNIQUE));GError *error=NULL;
    g_assert_true(g_application_register(G_APPLICATION(app->application),NULL,&error));g_assert_no_error(error);activate(app->application,app);pump(25);
}
static void ui_teardown(App *app,gconstpointer data)
{
    (void)data;app_clear(app);if(app->window)gtk_window_destroy(GTK_WINDOW(app->window));app->window=NULL;g_object_unref(app->application);pump(10);
}
static void humans(App *app)
{
    for(int p=0;p<2;p++){gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controllers[p]),PLAYER_HUMAN);app->kinds[p]=PLAYER_HUMAN;}
}
static void force_turn(App *app,Variant v,unsigned a,unsigned b)
{
    cancel_work(app);app->playing=FALSE;gtk_drop_down_set_selected(GTK_DROP_DOWN(app->variant),v);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->session),SESSION_SINGLE);humans(app);game_init(&app->game,v,SESSION_SINGLE,5);app->game.phase=PRE_ROLL;game_roll(&app->game,a,b);
    app->playing=TRUE;app->paused=app->failed=app->saved=app->save_failed=FALSE;app->draft=app->game;app->drafting=TRUE;app->draft_plan=(TurnPlan){0};app->selected=-1;refresh_choices(app);update(app);
}
