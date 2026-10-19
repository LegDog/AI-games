// SPDX-License-Identifier: GPL-3.0-or-later
/* Exercise the actual final placement for both terminal outcomes. */
static int prepare_final_placement(App *app,int outcome)
{
    guint64 full=(G_GUINT64_CONSTANT(1)<<GAME_POINTS)-1;
    for(unsigned m=0;m<GAME_CHOICES;m++) {
        Move move=game_moves[m];
        guint64 empty=(G_GUINT64_CONSTANT(1)<<move.entry)|(G_GUINT64_CONSTANT(1)<<move.destination);
        if(outcome==1) {
            app->game=(Game){.occupied=full^empty,.moves=GAME_MAX_DEPTH-1};
            return m;
        }
        for(unsigned p=0;p<GAME_POINTS;p++)if(!(empty&(G_GUINT64_CONSTANT(1)<<p))) {
            Game before={.occupied=full^empty^(G_GUINT64_CONSTANT(1)<<p),.moves=GAME_MAX_DEPTH-2};
            Game after=before;
            if(game_play(&after,m) && game_result(&after)==2) {app->game=before;return m;}
        }
    }
    g_assert_not_reached();
}
static void end_pause(App *app,gconstpointer data)
{
    (void)data;
    g_assert_false(gtk_check_button_get_active(GTK_CHECK_BUTTON(app->pause_at_end_switch)));
    for(int outcome=1;outcome<=2;outcome++)for(unsigned enabled=0;enabled<=1;enabled++) {
        configure_clicked(NULL,app);
        gtk_check_button_set_active(GTK_CHECK_BUTTON(app->pause_at_end_switch),enabled);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->delay),0);
        begin_clicked(NULL,app);
        g_assert_false(gtk_widget_get_sensitive(app->pause_at_end_switch));
        int move=prepare_final_placement(app,outcome);start_animation(app,move);
        g_assert_cmpint(game_result(&app->game),==,outcome);g_assert_true(app->saved);
        g_assert_cmpint(app->paused,==,enabled);g_assert_cmpint(app->end_dialog!=NULL,==,enabled);
        guint64 occupied=app->game.occupied;unsigned moves=app->game.moves;
        g_autofree char *history_before=NULL,*history_after=NULL;
        g_assert_true(g_file_get_contents(GAME_HISTORY_FILE,&history_before,NULL,NULL));
        pump(30);schedule(app);update(app);pump(10);
        g_assert_cmpuint(app->game.occupied,==,occupied);g_assert_cmpuint(app->game.moves,==,moves);g_assert_null(app->task);
        g_assert_true(g_file_get_contents(GAME_HISTORY_FILE,&history_after,NULL,NULL));g_assert_cmpstr(history_before,==,history_after);
        if(enabled) {
            g_assert_true(gtk_window_get_modal(GTK_WINDOW(app->end_dialog)));
            g_assert_true(gtk_window_get_transient_for(GTK_WINDOW(app->end_dialog))==GTK_WINDOW(app->window));
            if(outcome==1)acknowledge_end_pause(NULL,app);
            else gtk_window_close(GTK_WINDOW(app->end_dialog));
            pump(10);g_assert_null(app->end_dialog);g_assert_false(app->paused);
            g_assert_cmpuint(app->game.occupied,==,occupied);
        }
        undo_clicked(NULL,app);g_assert_cmpuint(app->game.moves,==,moves-1);g_assert_false(app->paused);
    }
    /* Reset/cancel also dismiss an outstanding dialog without stale callbacks. */
    configure_clicked(NULL,app);gtk_check_button_set_active(GTK_CHECK_BUTTON(app->pause_at_end_switch),TRUE);
    begin_clicked(NULL,app);start_animation(app,prepare_final_placement(app,1));g_assert_nonnull(app->end_dialog);
    restart_clicked(NULL,app);g_assert_null(app->end_dialog);g_assert_cmpuint(app->game.moves,==,0);g_assert_false(app->paused);
    g_assert_true(app->pause_at_end);
    start_animation(app,prepare_final_placement(app,2));g_assert_nonnull(app->end_dialog);
    configure_clicked(NULL,app);g_assert_null(app->end_dialog);g_assert_false(app->playing);g_assert_false(app->paused);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(app->language),I18N_PORTUGUESE);
    g_assert_cmpstr(gtk_check_button_get_label(GTK_CHECK_BUTTON(app->pause_at_end_switch)),==,"Pausar ao final (concluído ou bloqueado)");
}
