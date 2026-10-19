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
const char *const game_labels[64]={"a1","b1","c1","d1","e1","f1","g1","a2","b2","c2","d2","e2","f2","g2","a3","b3","c3","d3","e3","f3","g3","a4","b4","c4","d4","e4","f4","g4","a5","b5","c5","d5","e5","f5","g5","a6","b6","c6","d6","e6","f6","g6","a7","b7","c7","d7","e7","f7","g7","-","-","-","-","-","-","-","-","-","-","-","-","-","-","swap"};

unsigned game_count(guint64 bits){unsigned n=0;for(;bits;bits&=bits-1)n++;return n;}
gboolean game_init_variant(Game *game,unsigned size,gboolean swap)
{
    if(size!=3 && size!=5 && size!=7)return FALSE;
    *game=(Game){.turn=1,.size=size,.swap_enabled=swap};return TRUE;
}
void game_init(Game *game){game_init_variant(game,5,TRUE);}
guint64 game_board_mask(const Game *game)
{
    guint64 mask=0;for(unsigned r=0;r<game->size;r++)for(unsigned c=0;c<game->size;c++)mask|=SQUARE_BIT(r*7+c);return mask;
}
guint64 game_neighbors(const Game *game,unsigned p)
{
    if(p>=49 || p/7>=game->size || p%7>=game->size)return 0;
    static const int dr[]={-1,-1,0,0,1,1},dc[]={0,1,-1,1,-1,0};guint64 mask=0;
    for(unsigned i=0;i<6;i++){int r=(int)(p/7)+dr[i],c=(int)(p%7)+dc[i];if(r>=0 && c>=0 && r<(int)game->size && c<(int)game->size)mask|=SQUARE_BIT(r*7+c);}
    return mask;
}
guint64 game_winning_path(const Game *game,int player)
{
    guint64 own=player==1?game->red:game->blue,seen=0;unsigned queue[49],head=0,tail=0;int parent[49];
    for(unsigned i=0;i<game->size;i++){
        unsigned p=player==1?i:i*7;if(own&SQUARE_BIT(p)){queue[tail++]=p;seen|=SQUARE_BIT(p);parent[p]=-1;}
    }
    while(head<tail){unsigned p=queue[head++];
        if((player==1?p/7:p%7)==game->size-1){guint64 path=0;for(int at=p;at>=0;at=parent[at])path|=SQUARE_BIT(at);return path;}
        guint64 next=game_neighbors(game,p)&own&~seen;
        for(unsigned q=0;q<49;q++)if(next&SQUARE_BIT(q)){seen|=SQUARE_BIT(q);parent[q]=p;queue[tail++]=q;}
    }return 0;
}
int game_result(const Game *game){return game_winning_path(game,1)?1:game_winning_path(game,-1)?-1:0;}
guint64 game_legal_moves(const Game *game)
{
    if(game_result(game))return 0;
    guint64 legal=game_board_mask(game)&~(game->red|game->blue);
    if(game->swap_enabled && game->actions==1 && game->turn==-1)legal|=SQUARE_BIT(GAME_SWAP);
    return legal;
}
gboolean game_play(Game *game,unsigned p)
{
    if(p>=64 || !(game_legal_moves(game)&SQUARE_BIT(p)))return FALSE;
    if(p==GAME_SWAP){
        /* Reflect and recolor the opening instead of exchanging UI player seats.
         * This is the isomorphic pie-rule position with fixed Red/Blue goals. */
        unsigned first=0;while(!(game->red&SQUARE_BIT(first)))first++;
        game->red=0;game->blue=SQUARE_BIT((first%7)*7+first/7);game->swapped=TRUE;
    }else{if(game->turn==1)game->red|=SQUARE_BIT(p);else game->blue|=SQUARE_BIT(p);game->moves++;}
    game->actions++;game->turn=-game->turn;return TRUE;
}
int game_point(const char *label)
{
    if(!label)return -1;
    if(g_ascii_strcasecmp(label,"swap")==0)return GAME_SWAP;
    if(strlen(label)!=2)return -1;
    char col=g_ascii_tolower(label[0]);return col>='a' && col<='g' && label[1]>='1' && label[1]<='7'?(label[1]-'1')*7+col-'a':-1;
}
int game_random_move(guint64 legal,GRand *random)
{
    if(!legal)return -1;
    unsigned n=g_rand_int_range(random,0,game_count(legal));
    for(unsigned p=0;p<64;p++)if(legal&SQUARE_BIT(p)){if(!n)return p;n--;}
    return -1;
}
/* Dijkstra: own stones cost zero, empty cells one, opposing stones impassable. */
int game_distance(const Game *game,int player)
{
    int distance[49];guint64 done=0,board=game_board_mask(game),own=player==1?game->red:game->blue,other=player==1?game->blue:game->red;
    const int infinity=1000;
    for(unsigned p=0;p<49;p++)distance[p]=infinity;
    for(unsigned i=0;i<game->size;i++){unsigned p=player==1?i:i*7;if(!(other&SQUARE_BIT(p)))distance[p]=(own&SQUARE_BIT(p))?0:1;}
    for(unsigned step=0;step<game->size*game->size;step++){
        int p=-1,best=infinity;
        for(unsigned q=0;q<49;q++)if((board&SQUARE_BIT(q)) && !(done&SQUARE_BIT(q)) && distance[q]<best){p=q;best=distance[q];}
        if(p<0)return game->size*game->size+1;
        if((player==1?(unsigned)p/7:(unsigned)p%7)==game->size-1)return best;
        done|=SQUARE_BIT(p);guint64 neighbors=game_neighbors(game,p)&~other&~done;
        for(unsigned q=0;q<49;q++)if(neighbors&SQUARE_BIT(q))distance[q]=MIN(distance[q],best+((own&SQUARE_BIT(q))?0:1));
    }return game->size*game->size+1;
}
void game_position(const Game *game,int player,PositionValue *value)
{
    *value=(PositionValue){.own_distance=game_distance(game,player),.other_distance=game_distance(game,-player)};
    int result=game_result(game);value->total=result?(result==player?100000:-100000):100*(value->other_distance-value->own_distance);
}
int game_positional_move(const Game *game,GRand *random,int values[64])
{
    guint64 legal=game_legal_moves(game),choices=0;int best=G_MININT;
    for(unsigned p=0;p<64;p++){
        values[p]=G_MININT;if(!(legal&SQUARE_BIT(p)))continue;
        Game next=*game;game_play(&next,p);PositionValue v;game_position(&next,game->turn,&v);values[p]=v.total;
        if(v.total>best){best=v.total;choices=0;}if(v.total==best)choices|=SQUARE_BIT(p);
    }return game_random_move(choices,random);
}
const char *player_name(PlayerKind kind)
{
    static const char *const names[]={"Human","Random","Connection heuristic","Minimax","Monte Carlo tree search","Ollama"};return kind<PLAYER_COUNT?names[kind]:"Unknown";
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
