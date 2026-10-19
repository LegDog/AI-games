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
const char *const game_labels[GAME_POINTS]={"a1","b1","c1","d1","e1","f1","a2","b2","c2","d2","e2","f2","a3","b3","c3","d3","e3","f3","a4","b4","c4","d4","e4","f4","a5","b5","c5","d5","e5","f5","a6","b6","c6","d6","e6","f6"};
void game_init(Game *g){*g=(Game){.turn=1};}
unsigned game_count(guint64 bits){unsigned n=0;for(;bits;bits&=bits-1)n++;return n;}
/* All 32 length-five windows, including both windows of a six-cell line. */
static const guint64 winning_windows[32]={
    G_GUINT64_CONSTANT(0x00000001f), G_GUINT64_CONSTANT(0x00000003e), G_GUINT64_CONSTANT(0x0000007c0), G_GUINT64_CONSTANT(0x000000f80), G_GUINT64_CONSTANT(0x00001f000), G_GUINT64_CONSTANT(0x00003e000), G_GUINT64_CONSTANT(0x0007c0000), G_GUINT64_CONSTANT(0x000f80000), G_GUINT64_CONSTANT(0x01f000000), G_GUINT64_CONSTANT(0x03e000000), G_GUINT64_CONSTANT(0x7c0000000), G_GUINT64_CONSTANT(0xf80000000),
    G_GUINT64_CONSTANT(0x001041041), G_GUINT64_CONSTANT(0x002082082), G_GUINT64_CONSTANT(0x004104104), G_GUINT64_CONSTANT(0x008208208), G_GUINT64_CONSTANT(0x010410410), G_GUINT64_CONSTANT(0x020820820), G_GUINT64_CONSTANT(0x041041040), G_GUINT64_CONSTANT(0x082082080), G_GUINT64_CONSTANT(0x104104100), G_GUINT64_CONSTANT(0x208208200), G_GUINT64_CONSTANT(0x410410400), G_GUINT64_CONSTANT(0x820820800),
    G_GUINT64_CONSTANT(0x010204081), G_GUINT64_CONSTANT(0x020408102), G_GUINT64_CONSTANT(0x408102040), G_GUINT64_CONSTANT(0x810204080),
    G_GUINT64_CONSTANT(0x001084210), G_GUINT64_CONSTANT(0x002108420), G_GUINT64_CONSTANT(0x042108400), G_GUINT64_CONSTANT(0x084210800),
};
guint64 game_winning_path(const Game *g,int player)
{
    guint64 bits=player==1?g->black:g->white,wins=0;
    for(unsigned i=0;i<G_N_ELEMENTS(winning_windows);i++)if((bits&winning_windows[i])==winning_windows[i])wins|=winning_windows[i];
    return wins;
}
GameResult game_result(const Game *g)
{
    gboolean black=game_winning_path(g,1)!=0,white=game_winning_path(g,-1)!=0;
    if(black && white)return GAME_DRAW;
    if(black)return GAME_BLACK;
    if(white)return GAME_WHITE;
    return (g->black|g->white)==GAME_BOARD_MASK?GAME_DRAW:GAME_ONGOING;
}
int game_rotated_cell(int cell,unsigned q,unsigned direction)
{
    if(cell<0 || cell>=36 || q>=4 || direction>1)return -1;
    int r=cell/6,c=cell%6,qr=(q/2)*3,qc=(q%2)*3;
    if(r<qr || r>=qr+3 || c<qc || c>=qc+3)return cell;
    int y=r-qr,x=c-qc;
    return direction?(qr+2-x)*6+qc+y:(qr+x)*6+qc+2-y;
}
guint64 game_rotate(guint64 bits,unsigned q,unsigned direction)
{
    if(q>=4 || direction>1)return bits;
    guint64 rotated=0;for(unsigned p=0;p<36;p++)if(bits&SQUARE_BIT(p))rotated|=SQUARE_BIT(game_rotated_cell(p,q,direction));return rotated;
}
gboolean game_move_equal(GameMove a,GameMove b){return a.cell==b.cell && a.quadrant==b.quadrant && a.direction==b.direction;}
static gboolean placement_win(const Game *g,int cell)
{
    Game next=*g;if(g->turn==1)next.black|=SQUARE_BIT(cell);else next.white|=SQUARE_BIT(cell);
    return game_winning_path(&next,g->turn)!=0;
}
void game_legal_moves(const Game *g,GameMoves *legal)
{
    legal->count=0;if(game_result(g))return;
    for(int cell=0;cell<36;cell++)if(!((g->black|g->white)&SQUARE_BIT(cell))){
        if(placement_win(g,cell))legal->moves[legal->count++]=(GameMove){cell,-1,0};
        else for(int q=0;q<4;q++)for(int d=0;d<2;d++)legal->moves[legal->count++]=(GameMove){cell,q,d};
    }
}
gboolean game_move_legal(const Game *g,GameMove m)
{
    if(m.cell<0 || m.cell>=36 || ((g->black|g->white)&SQUARE_BIT(m.cell)) || game_result(g))return FALSE;
    if(placement_win(g,m.cell))return m.quadrant==-1 && m.direction==0;
    return m.quadrant>=0 && m.quadrant<4 && m.direction>=0 && m.direction<2;
}
void game_apply_legal(Game *g,GameMove m)
{
    if(g->turn==1)g->black|=SQUARE_BIT(m.cell);else g->white|=SQUARE_BIT(m.cell);
    if(m.quadrant>=0){g->black=game_rotate(g->black,m.quadrant,m.direction);g->white=game_rotate(g->white,m.quadrant,m.direction);}
    g->actions++;g->turn=-g->turn;
}
gboolean game_play(Game *g,GameMove m){if(!game_move_legal(g,m))return FALSE;game_apply_legal(g,m);return TRUE;}
int game_point(const char *s){return s && strlen(s)==2 && s[0]>='a' && s[0]<='f' && s[1]>='1' && s[1]<='6'?(s[1]-'1')*6+s[0]-'a':-1;}
void game_move_label(GameMove m,char label[16])
{
    if(m.cell<0 || m.cell>=36){g_strlcpy(label,"invalid",16);return;}
    if(m.quadrant==-1)g_snprintf(label,16,"%s:win",game_labels[m.cell]);
    else g_snprintf(label,16,"%s:%c:%s",game_labels[m.cell],'A'+m.quadrant,m.direction?"ccw":"cw");
}
gboolean game_parse_move(const char *label,GameMove *m)
{
    if(!label || strlen(label)<6)return FALSE;
    char cell[3]={label[0],label[1],0};int p=game_point(cell);if(p<0 || label[2]!=':')return FALSE;
    if(g_str_equal(label+3,"win")){*m=(GameMove){p,-1,0};return TRUE;}
    if(strlen(label)<7 || label[3]<'A' || label[3]>'D' || label[4]!=':')return FALSE;
    int d=g_str_equal(label+5,"cw")?0:g_str_equal(label+5,"ccw")?1:-1;if(d<0)return FALSE;
    *m=(GameMove){p,label[3]-'A',d};return TRUE;
}
int game_random_move(const GameMoves *legal,GRand *random){return legal->count?g_rand_int_range(random,0,legal->count):-1;}
void game_position(const Game *g,int player,PositionValue *v)
{
    *v=(PositionValue){0};guint64 own=player==1?g->black:g->white,other=player==1?g->white:g->black;
    const int weight[]={0,1,10,100,1000,0};
    for(unsigned i=0;i<G_N_ELEMENTS(winning_windows);i++){
        if(!(other&winning_windows[i]))v->own_score+=weight[game_count(own&winning_windows[i])];
        if(!(own&winning_windows[i]))v->other_score+=weight[game_count(other&winning_windows[i])];
    }
    GameResult result=game_result(g);v->total=result==GAME_DRAW?0:result?(result==player?GAME_WIN_SCORE:-GAME_WIN_SCORE):v->own_score-v->other_score;
}
int game_positional_move(const Game *g,GRand *random,int values[GAME_CHOICES])
{
    GameMoves legal;game_legal_moves(g,&legal);int best=G_MININT,chosen=-1;unsigned ties=0;
    for(unsigned i=0;i<GAME_CHOICES;i++)values[i]=G_MININT;
    for(unsigned i=0;i<legal.count;i++){
        Game next=*g;game_apply_legal(&next,legal.moves[i]);PositionValue v;game_position(&next,g->turn,&v);values[i]=v.total;
        if(v.total>best){best=v.total;chosen=i;ties=1;}else if(v.total==best && g_rand_int_range(random,0,++ties)==0)chosen=i;
    }return chosen;
}
const char *player_name(PlayerKind kind)
{
    static const char *const names[]={"Human","Random","Heuristic","Minimax (plain)","Minimax (alpha-beta)","Ollama"};return (unsigned)kind<PLAYER_COUNT?names[kind]:"Unknown";
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
