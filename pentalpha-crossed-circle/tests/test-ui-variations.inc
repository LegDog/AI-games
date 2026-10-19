// SPDX-License-Identifier: GPL-3.0-or-later
static void graph_variations(App *app,gconstpointer data)
{
    (void)data;
    for(PlayerKind kind=PLAYER_CONNECTIVITY;kind<=PLAYER_GF2;kind++) {
        configure_clicked(NULL,app);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller),kind);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),.1);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(app->pause_at_end_switch),TRUE);
        begin_clicked(NULL,app);pause_clicked(NULL,app);settle_worker(app);
        g_assert_true(app->ready);g_assert_cmpuint(app->game.moves,==,0);
        g_assert_true(app->has_proof);g_assert_cmpuint(app->proven_max,==,34);g_assert_cmpuint(app->proof.component_count,==,2);
        if(forest_controller(kind))g_assert_cmpuint(app->forest.length,==,34);
        if(kind==PLAYER_GF2)g_assert_cmpint(app->forest.analysis.matrix_rank,==,34);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language),I18N_PORTUGUESE);
        g_assert_cmpuint(gtk_drop_down_get_selected(GTK_DROP_DOWN(app->controller)),==,kind);
        g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(app->status)),"máximo comprovado 34"));
        gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language),I18N_ENGLISH);
        pause_clicked(NULL,app);g_assert_true(app->animation.active);pause_clicked(NULL,app);
        AnimationFrame before=animation_frame(&app->animation,G_MAXINT64);pump(30);
        g_assert_cmpfloat(animation_frame(&app->animation,G_MAXINT64).position,==,before.position);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0);pause_clicked(NULL,app);
        gint64 end=g_get_monotonic_time()+5000000;
        while(!game_result(&app->game) && g_get_monotonic_time()<end)pump(2);
        g_assert_cmpuint(app->game.moves,==,34);g_assert_cmpint(game_result(&app->game),==,2);
        g_assert_true(app->saved);g_assert_nonnull(app->end_dialog);g_assert_true(app->paused);
        g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(app->status)),"Proven optimum reached"));
        acknowledge_end_pause(NULL,app);g_assert_null(app->end_dialog);pump(20);g_assert_cmpuint(app->game.moves,==,34);
        g_autofree char *history=NULL;g_assert_true(g_file_get_contents(GAME_HISTORY_FILE,&history,NULL,NULL));
        g_assert_nonnull(strstr(history,"Initial proven maximum: 34"));g_assert_nonnull(strstr(history,"optimum reached: yes"));
        restart_clicked(NULL,app);pause_clicked(NULL,app);settle_worker(app);
        g_assert_cmpuint(app->game.moves,==,0);g_assert_true(app->ready);
        configure_clicked(NULL,app);g_assert_cmpuint(app->forest.length,==,0);g_assert_false(app->has_proof);
        begin_clicked(NULL,app);configure_clicked(NULL,app);settle_worker(app);
        g_assert_cmpuint(app->game.moves,==,0);g_assert_cmpuint(app->forest.length,==,0);g_assert_false(app->has_proof);
    }
    configure_clicked(NULL,app);gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller),PLAYER_SEARCH_SYMMETRY);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->depth),2);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->memory_limit),16);
    begin_clicked(NULL,app);pause_clicked(NULL,app);settle_worker(app);
    g_assert_cmpuint(app->last_search.completed_depth,==,2);g_assert_cmpuint(app->last_symmetry.sibling_prunes,>,0);
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(app->status)),"Sibling prunes:"));
    configure_clicked(NULL,app);gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->time_limit),1e-12);
    begin_clicked(NULL,app);settle_worker(app);g_assert_true(app->failed);g_assert_cmpuint(app->game.moves,==,0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->time_limit),60);retry_clicked(NULL,app);pause_clicked(NULL,app);settle_worker(app);
    g_assert_false(app->failed);g_assert_true(app->ready);configure_clicked(NULL,app);
    /* Existing controllers never receive the new graph advice. */
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->controller),PLAYER_SEARCH);
    begin_clicked(NULL,app);pause_clicked(NULL,app);settle_worker(app);g_assert_false(app->has_proof);
}
