// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"
#include <glib/gstdio.h>
/* Independent array flood fill; no engine neighbor/path functions. */
static gboolean reference(const Game *game,int player)
{
    gboolean visited[7][7]={{0}};int qr[49],qc[49],head=0,tail=0;
    guint64 own=player==1?game->red:game->blue;
    for(unsigned i=0;i<game->size;i++){unsigned r=player==1?0:i,c=player==1?i:0;if(own&SQUARE_BIT(r*7+c)){visited[r][c]=TRUE;qr[tail]=r;qc[tail++]=c;}}
    while(head<tail){int r=qr[head],c=qc[head++];if((player==1?r:c)==(int)game->size-1)return TRUE;
        for(int dr=-1;dr<=1;dr++)for(int dc=-1;dc<=1;dc++){
            if((!dr && !dc) || dr==dc)continue;
            int nr=r+dr,nc=c+dc;if(nr<0 || nc<0 || nr>=(int)game->size || nc>=(int)game->size || visited[nr][nc] || !(own&SQUARE_BIT(nr*7+nc)))continue;
            visited[nr][nc]=TRUE;qr[tail]=nr;qc[tail++]=nc;
        }
    }return FALSE;
}
static void rules(void)
{
    Game g;game_init(&g);g_assert_cmpuint(game_count(game_legal_moves(&g)),==,25);
    g_assert_false(game_play(&g,GAME_SWAP));g_assert_false(game_play(&g,49));g_assert_false(game_play(&g,64));
    g_assert_true(game_play(&g,game_point("b3")));g_assert_true(game_legal_moves(&g)&SQUARE_BIT(GAME_SWAP));
    g_assert_true(game_play(&g,GAME_SWAP));g_assert_cmpuint(g.red,==,0);g_assert_cmpuint(g.blue,==,SQUARE_BIT(game_point("c2")));
    g_assert_cmpint(g.turn,==,1);g_assert_cmpuint(g.moves,==,1);g_assert_cmpuint(g.actions,==,2);g_assert_false(game_play(&g,GAME_SWAP));
    g_assert_true(game_init_variant(&g,3,FALSE));g_assert_true(game_play(&g,0));g_assert_false(game_play(&g,GAME_SWAP));g_assert_false(game_play(&g,0));g_assert_false(game_play(&g,3));
    Game saved=g;g_assert_false(game_init_variant(&g,4,TRUE));g_assert_cmpmem(&saved,sizeof saved,&g,sizeof g);
    for(unsigned p=0;p<49;p++)g_assert_cmpint(game_point(game_labels[p]),==,(int)p);
    g_assert_cmpint(game_point("swap"),==,63);g_assert_cmpint(game_point("h1"),==,-1);
    g_assert_cmpint(game_point(NULL),==,-1);g_assert_cmpint(game_random_move(0,NULL),==,-1);
    g_assert_cmpuint(game_count(game_neighbors(&g,8)),==,6);
    g_assert_false(game_neighbors(&g,0)&SQUARE_BIT(8));g_assert_true(game_neighbors(&g,1)&SQUARE_BIT(7));
}
static void full_boards(void)
{
    for(unsigned bits=0;bits<512;bits++){
        Game g;game_init_variant(&g,3,FALSE);
        for(unsigned p=0;p<9;p++){guint64 b=SQUARE_BIT((p/3)*7+p%3);if(bits&(1u<<p))g.red|=b;else g.blue|=b;}
        gboolean red=reference(&g,1),blue=reference(&g,-1);g_assert_true(red!=blue);
        g_assert_cmpint(game_result(&g),==,red?1:-1);g_assert_cmpuint(game_legal_moves(&g),==,0);
        guint64 path=game_winning_path(&g,red?1:-1);g_assert_cmpuint(game_count(path),>=,3);
        Game chain=g;chain.red=red?path:0;chain.blue=blue?path:0;g_assert_true(reference(&chain,red?1:-1));
    }
}
static void randomized(void)
{
    GRand *rng=g_rand_new_with_seed(874);
    for(unsigned size=3;size<=7;size+=2)for(unsigned run=0;run<80;run++){
        Game g;game_init_variant(&g,size,run%2);
        while(!game_result(&g)){
            g_assert_cmpuint(g.actions,<,size*size+1);int p=game_random_move(game_legal_moves(&g),rng);g_assert_true(game_play(&g,p));
            g_assert_cmpint(game_result(&g),==,reference(&g,1)?1:reference(&g,-1)?-1:0);
            PositionValue a,b;game_position(&g,1,&a);game_position(&g,-1,&b);g_assert_cmpint(a.total,==,-b.total);
        }
        g_assert_false(game_play(&g,0));g_assert_cmpuint(game_legal_moves(&g),==,0);
    }g_rand_free(rng);
}
static void distances(void)
{
    Game g;game_init_variant(&g,3,FALSE);g_assert_cmpint(game_distance(&g,1),==,3);g_assert_cmpint(game_distance(&g,-1),==,3);
    g.red=SQUARE_BIT(0)|SQUARE_BIT(7);g.blue=SQUARE_BIT(1)|SQUARE_BIT(2);g.moves=4;int values[64];GRand *rng=g_rand_new_with_seed(4);
    g_assert_cmpint(game_positional_move(&g,rng,values),==,14);g_assert_cmpint(values[14],==,100000);g_rand_free(rng);
}
static void reports(void)
{
    GError *error=NULL;char *dir=g_dir_make_tmp("hex-report-XXXXXX",&error);g_assert_no_error(error);char *file=g_build_filename(dir,"history.md",NULL);
    g_assert_true(game_append_report(file,"one\n",&error));g_assert_true(game_append_report(file,"two\n",&error));g_assert_no_error(error);
    char *contents=NULL;g_file_get_contents(file,&contents,NULL,&error);g_assert_no_error(error);g_assert_cmpstr(contents,==,"one\ntwo\n");g_free(contents);
    g_assert_false(game_append_report(dir,"no",&error));g_assert_nonnull(error);g_clear_error(&error);g_remove(file);g_rmdir(dir);g_free(file);g_free(dir);
}
int main(int argc,char **argv){g_test_init(&argc,&argv,NULL);g_test_add_func("/hex/rules-swap-coordinates",rules);g_test_add_func("/hex/exhaustive-no-draw-paths",full_boards);g_test_add_func("/hex/independent-random-replay",randomized);g_test_add_func("/hex/connection-distances",distances);g_test_add_func("/hex/reports",reports);return g_test_run();}
