// SPDX-License-Identifier: GPL-3.0-or-later
#include "game.h"
static guint64 reference_flips(const Game *game,unsigned square)
{
    if((game->black|game->white)&SQUARE_BIT(square))return 0;
    int cells[8][8];for(unsigned r=0;r<8;r++)for(unsigned c=0;c<8;c++){guint64 b=SQUARE_BIT(r*8+c);cells[r][c]=game->black&b?1:game->white&b?-1:0;}
    guint64 flips=0;
    for(int dr=-1;dr<=1;dr++)for(int dc=-1;dc<=1;dc++)if(dr || dc) {
        int r=(int)square/8+dr,c=(int)square%8+dc;guint64 line=0;
        while(r>=0 && r<8 && c>=0 && c<8 && cells[r][c]==-game->turn){line|=SQUARE_BIT(r*8+c);r+=dr;c+=dc;}
        if(r>=0 && r<8 && c>=0 && c<8 && cells[r][c]==game->turn)flips|=line;
    }return flips;
}
static void opening(void)
{
    Game game;game_init(&game);guint64 expected=SQUARE_BIT(19)|SQUARE_BIT(26)|SQUARE_BIT(37)|SQUARE_BIT(44);
    g_assert_cmpuint(game_legal_moves(&game),==,expected);g_assert_false(game_play(&game,GAME_PASS));g_assert_false(game_play(&game,27));
    g_assert_true(game_play(&game,19));g_assert_cmpuint(game_count(game.black),==,4);g_assert_cmpuint(game_count(game.white),==,1);
    g_assert_cmpuint(game.black&SQUARE_BIT(27),!=,0);g_assert_cmpint(game.turn,==,-1);
    g_assert_cmpint(game_point("H8"),==,63);g_assert_cmpint(game_point("a1"),==,0);g_assert_cmpint(game_point("a0"),==,-1);g_assert_cmpint(game_point("a10"),==,-1);
}
static void directions(void)
{
    Game game={.turn=1};guint64 expected=0;
    for(int dr=-1;dr<=1;dr++)for(int dc=-1;dc<=1;dc++)if(dr || dc){expected|=SQUARE_BIT((3+dr)*8+3+dc);game.black|=SQUARE_BIT((3+2*dr)*8+3+2*dc);}
    game.white=expected;g_assert_cmpuint(game_flips(&game,27),==,expected);g_assert_true(game_play(&game,27));g_assert_cmpuint(game.white,==,0);g_assert_cmpint(game_result(&game),==,1);
    game=(Game){.turn=1,.black=SQUARE_BIT(2)|SQUARE_BIT(4),.white=SQUARE_BIT(1)|SQUARE_BIT(3)};
    g_assert_cmpuint(game_flips(&game,0),==,SQUARE_BIT(1));g_assert_true(game_play(&game,0));g_assert_cmpuint(game.white,==,SQUARE_BIT(3));
    game=(Game){.turn=1,.black=SQUARE_BIT(7),.white=SQUARE_BIT(8)};g_assert_cmpuint(game_flips(&game,9),==,0);
}
static void passes(void)
{
    Game game={.black=SQUARE_BIT(1),.white=SQUARE_BIT(0),.turn=1};
    g_assert_cmpuint(game_legal_moves(&game),==,0);g_assert_cmpint(game_result(&game),==,0);
    g_assert_true(game_play(&game,GAME_PASS));g_assert_cmpuint(game.passes,==,1);g_assert_cmpint(game.turn,==,-1);g_assert_false(game_play(&game,GAME_PASS));
    g_assert_true(game_play(&game,2));g_assert_cmpint(game_result(&game),==,-1);g_assert_false(game_play(&game,GAME_PASS));
    game=(Game){.black=SQUARE_BIT(0),.white=SQUARE_BIT(63),.turn=1};g_assert_cmpint(game_result(&game),==,2);
    game=(Game){.black=G_GUINT64_CONSTANT(0xffffffff),.white=G_GUINT64_CONSTANT(0xffffffff00000000),.turn=1};g_assert_cmpint(game_result(&game),==,2);
}
static void random_reference(void)
{
    g_autoptr(GRand) random=g_rand_new_with_seed(0);guint64 seen=0;
    for(unsigned trial=0;trial<35;trial++) {
        Game game;game_init(&game);unsigned actions=0;
        while(!game_result(&game)) {
            guint64 legal=0;
            for(unsigned p=0;p<64;p++){guint64 flips=reference_flips(&game,p);g_assert_cmpuint(game_flips(&game,p),==,flips);if(flips)legal|=SQUARE_BIT(p);}
            g_assert_cmpuint(game_legal_moves(&game),==,legal);seen|=legal;
            PositionValue black,white;game_position(&game,1,&black);game_position(&game,-1,&white);g_assert_cmpint(black.total,==,-white.total);
            g_assert_true(game_play(&game,game_random_move(legal,random)));g_assert_cmpuint(game.black&game.white,==,0);
            g_assert_cmpuint(game_count(game.black|game.white),==,game.moves+4);g_assert_cmpuint(++actions,<=,GAME_MAX_ACTIONS);
        }
        g_assert_cmpuint(game_legal_for(&game,1)|game_legal_for(&game,-1),==,0);
    }
    g_assert_cmpuint(seen&SQUARE_BIT(63),!=,0);
}
int main(int argc,char **argv){g_test_init(&argc,&argv,NULL);g_test_add_func("/game/opening",opening);g_test_add_func("/game/eight-directions",directions);g_test_add_func("/game/forced-pass-end",passes);g_test_add_func("/game/independent-replay",random_reference);return g_test_run();}
