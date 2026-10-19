// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include "i18n.h"
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
const char * const game_labels[64]={"a1","b1","c1","d1","e1","f1","g1","h1","a2","b2","c2","d2","e2","f2","g2","h2","a3","b3","c3","d3","e3","f3","g3","h3","a4","b4","c4","d4","e4","f4","g4","h4","a5","b5","c5","d5","e5","f5","g5","h5","a6","b6","c6","d6","e6","f6","g6","h6","a7","b7","c7","d7","e7","f7","g7","h7","a8","b8","c8","d8","e8","f8","g8","h8"};
unsigned game_count(guint64 bits)
{
    unsigned count=0;for(;bits;bits&=bits-1)count++;return count;
}
static guint64 step(guint64 bits,unsigned direction)
{
    const guint64 a=G_GUINT64_CONSTANT(0x0101010101010101),h=G_GUINT64_CONSTANT(0x8080808080808080);
    switch(direction) {
    case 0:return (bits&~a)>>9;case 1:return bits>>8;case 2:return (bits&~h)>>7;
    case 3:return (bits&~a)>>1;case 4:return (bits&~h)<<1;
    case 5:return (bits&~a)<<7;case 6:return bits<<8;default:return (bits&~h)<<9;
    }
}
void game_init(Game *game)
{
    *game=(Game){.black=SQUARE_BIT(28)|SQUARE_BIT(35),.white=SQUARE_BIT(27)|SQUARE_BIT(36),.turn=1};
}
guint64 game_legal_for(const Game *game,int player)
{
    guint64 own=player==1?game->black:game->white,other=player==1?game->white:game->black;
    guint64 empty=~(own|other),legal=0;
    for(unsigned d=0;d<8;d++) {
        guint64 line=step(own,d)&other;
        for(unsigned i=0;i<5;i++)line|=step(line,d)&other;
        legal|=step(line,d)&empty;
    }return legal;
}
guint64 game_legal_moves(const Game *game){return game_legal_for(game,game->turn);}
guint64 game_flips(const Game *game,unsigned square)
{
    if(square>=64 || ((game->black|game->white)&SQUARE_BIT(square)))return 0;
    guint64 own=game->turn==1?game->black:game->white,other=game->turn==1?game->white:game->black,flips=0;
    for(unsigned d=0;d<8;d++) {
        guint64 at=step(SQUARE_BIT(square),d),line=0;
        while(at&other){line|=at;at=step(at,d);}
        if(at&own)flips|=line;
    }return flips;
}
int game_result(const Game *game)
{
    if(game_legal_for(game,1) || game_legal_for(game,-1))return 0;
    unsigned black=game_count(game->black),white=game_count(game->white);
    return black==white?2:black>white?1:-1;
}
gboolean game_play(Game *game,unsigned square)
{
    if(square==GAME_PASS) {
        if(game_legal_moves(game) || !game_legal_for(game,-game->turn))return FALSE;
        game->turn=-game->turn;game->passes++;return TRUE;
    }
    guint64 flips=game_flips(game,square);if(!flips)return FALSE;
    if(game->turn==1){game->black|=flips|SQUARE_BIT(square);game->white&=~flips;}
    else{game->white|=flips|SQUARE_BIT(square);game->black&=~flips;}
    game->moves++;game->turn=-game->turn;return TRUE;
}
int game_point(const char *label)
{
    if(!label || strlen(label)!=2)return -1;
    char col=g_ascii_tolower(label[0]);return col>='a' && col<='h' && label[1]>='1' && label[1]<='8'?(label[1]-'1')*8+col-'a':-1;
}
int game_random_move(guint64 legal,GRand *random)
{
    if(!legal)return GAME_PASS;
    unsigned choice=g_rand_int_range(random,0,game_count(legal));
    for(unsigned p=0;p<64;p++)if(legal&SQUARE_BIT(p)){if(!choice)return p;choice--;}
    return -1;
}
void game_position(const Game *game,int player,PositionValue *value)
{
    static const int weights[64]={
        120,-25,20,5,5,20,-25,120,-25,-45,-5,-5,-5,-5,-45,-25,
        20,-5,15,3,3,15,-5,20,5,-5,3,3,3,3,-5,5,
        5,-5,3,3,3,3,-5,5,20,-5,15,3,3,15,-5,20,
        -25,-45,-5,-5,-5,-5,-45,-25,120,-25,20,5,5,20,-25,120};
    *value=(PositionValue){0};guint64 own=player==1?game->black:game->white,other=player==1?game->white:game->black;
    value->discs=(int)game_count(own)-(int)game_count(other);
    int result=game_result(game);
    if(result){value->total=result==2?0:(result==player?100000:-100000)+100*value->discs;return;}
    for(unsigned p=0;p<64;p++) {if(own&SQUARE_BIT(p))value->positional+=weights[p];else if(other&SQUARE_BIT(p))value->positional-=weights[p];}
    value->mobility=(int)game_count(game_legal_for(game,player))-(int)game_count(game_legal_for(game,-player));
    guint64 frontier=0,empty=~(own|other);for(unsigned d=0;d<8;d++)frontier|=step(empty,d);
    value->frontier=(int)game_count(frontier&other)-(int)game_count(frontier&own);
    value->total=4*value->positional+15*value->mobility+6*value->frontier+(game_count(empty)>20?1:4)*value->discs;
}
int game_positional_move(const Game *game,GRand *random,int values[64])
{
    guint64 legal=game_legal_moves(game),best_moves=0;int best=G_MININT;
    for(unsigned p=0;p<64;p++) {
        values[p]=G_MININT;if(!(legal&SQUARE_BIT(p)))continue;
        Game next=*game;game_play(&next,p);PositionValue value;game_position(&next,game->turn,&value);values[p]=value.total;
        if(value.total>best){best=value.total;best_moves=0;}if(value.total==best)best_moves|=SQUARE_BIT(p);
    }return game_random_move(best_moves,random);
}
const char *player_name(PlayerKind kind)
{
    static const char *const names[]={"Human","Random","Positional","Minimax","Monte Carlo tree search","Ollama"};return kind<PLAYER_COUNT?names[kind]:"Unknown";
}

gboolean game_append_report(const char *path, const char *report, GError **error)
{
    int fd = g_open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
    if (fd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    _("Could not open %s: %s"), path, g_strerror(errno));
        return FALSE;
    }
    /* Serialize separate app instances, and roll back an incomplete append so
     * the pending report can be retried without duplicating its prefix. */
    int saved_errno = 0;
    while (flock(fd, LOCK_EX) < 0) {
        if (errno != EINTR) {
            saved_errno = errno;
            break;
        }
    }
    struct stat info;
    gboolean can_rollback = FALSE;
    if (!saved_errno) {
        if (fstat(fd, &info) < 0)
            saved_errno = errno;
        else if (!S_ISREG(info.st_mode))
            saved_errno = EINVAL;
        else
            can_rollback = TRUE;
    }
    gsize remaining = strlen(report);
    const char *cursor = report;
    while (!saved_errno && remaining) {
        ssize_t written = write(fd, cursor, remaining);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            saved_errno = written == 0 ? EIO : errno;
        else {
            cursor += written;
            remaining -= written;
        }
    }
    if (!saved_errno && fsync(fd) < 0)
        saved_errno = errno;
    gboolean rollback_failed = FALSE;
    if (saved_errno && can_rollback)
        rollback_failed = ftruncate(fd, info.st_size) < 0;
    /* Closing releases the advisory lock. Data has already been synchronized. */
    close(fd);
    if (saved_errno)
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                    _("Could not write %s: %s%s"), path, g_strerror(saved_errno),
                    rollback_failed ? _(". Could not remove the partial write; check the file before retrying") : "");
    return saved_errno == 0;
}
