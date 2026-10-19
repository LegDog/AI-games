// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <string.h>
typedef struct { guint64 key; guint32 node; } Entry;
typedef struct { guint64 occupied; gint64 heuristic; guint32 parent; guint8 move, depth; } Node;
typedef struct {
    Game root;
    const SearchControl *control;
    SearchLimits limits;
    SearchResult result;
    gint64 start, last_progress;
    guint32 seed;
    Entry *table;
    gsize capacity, used;
    Node *nodes;
    guint32 *heap;
    guint32 count, heap_count, node_capacity;
    unsigned path[MAX_JUMPS];
} Search;
SearchLimits search_defaults(void) { return (SearchLimits) {60, 256u*1024*1024, 0}; }
const char *search_status_name(SearchStatus status)
{
    static const char * const names[] = {"solved", "unsolvable", "limit-reached", "cancelled", "error"}; return names[status];
}
static guint64 mix(guint64 value)
{
    value ^= value >> 30; value *= G_GUINT64_CONSTANT(0xbf58476d1ce4e5b9);
    value ^= value >> 27; value *= G_GUINT64_CONSTANT(0x94d049bb133111eb);
    return value ^ (value >> 31);
}
static gboolean stopped(Search *s)
{
    if (s->control && s->control->cancelled && s->control->cancelled(s->control->data)) s->result.status = SEARCH_CANCELLED;
    if (s->result.status != SEARCH_UNSOLVABLE) return TRUE;
    if (s->limits.node_limit && s->result.nodes >= s->limits.node_limit) s->result.status = SEARCH_LIMIT;
    if ((s->result.nodes & 1023) == 0 || s->result.nodes < 2) {
        gint64 now = g_get_monotonic_time();
        if (s->limits.seconds > 0 && now-s->start >= s->limits.seconds*1000000) s->result.status = SEARCH_LIMIT;
        if (s->control && s->control->progress && now-s->last_progress >= 100000) {
            s->control->progress(s->result.nodes, now-s->start, s->result.peak_memory, s->control->data); s->last_progress = now;
        }
    }
    return s->result.status != SEARCH_UNSOLVABLE;
}
static Entry *lookup(Search *s, guint64 key)
{
    gsize index = mix(key) & (s->capacity-1);
    while (s->table[index].key && s->table[index].key != key+1) index = (index+1) & (s->capacity-1);
    return &s->table[index];
}
static gboolean reserve_entry(Search *s)
{
    if (s->used >= s->capacity*7/10) { s->result.status = SEARCH_LIMIT; return FALSE; }
    s->used++; return TRUE;
}
typedef struct { unsigned move; gint64 score; guint64 tie; } Candidate;
static unsigned ordered(Search *s, const Game *game, Candidate candidates[MAX_MOVES])
{
    MoveList legal = game_legal_moves(game);
    for (unsigned i = 0; i < legal.count; i++) {
        unsigned m = legal.items[i];
        Candidate candidate = {m, game_heuristic(game, m), mix(game->occupied ^ ((guint64) m << 40) ^ s->seed)};
        unsigned j = i;
        while (j && (candidates[j-1].score < candidate.score ||
            (candidates[j-1].score == candidate.score && candidates[j-1].tie > candidate.tie))) {
            candidates[j] = candidates[j-1]; j--;
        }
        candidates[j] = candidate;
    }
    return legal.count;
}
static gboolean dfs(Search *s, Game *game, unsigned depth)
{
    if (stopped(s)) return FALSE;
    s->result.nodes++;
    GameResult terminal = game_result(game);
    if (terminal == GAME_SOLVED) {
        s->result.length = depth; memcpy(s->result.path, s->path, depth*sizeof(unsigned)); s->result.status = SEARCH_SOLVED; return TRUE;
    }
    if (terminal != GAME_ONGOING) return FALSE;
    guint64 key = game_canonical(game);
    if (lookup(s, key)->key) return FALSE;
    Candidate candidates[MAX_MOVES]; unsigned count = ordered(s, game, candidates);
    for (unsigned i = 0; i < count; i++) {
        s->path[depth] = candidates[i].move;
        game_play(game, candidates[i].move);
        gboolean solved = dfs(s, game, depth+1);
        game_undo(game);
        if (solved) return TRUE;
        if (s->result.status != SEARCH_UNSOLVABLE) return FALSE;
    }
    if (!reserve_entry(s)) return FALSE;
    lookup(s, key)->key = key+1;
    return FALSE;
}
/* All successful paths have equal f=g+(pegs-1). Prefer greater g, then the
 * documented positional heuristic, then a seeded state tie break. */
static gboolean precedes(Search *s, guint32 a, guint32 b)
{
    Node *x = &s->nodes[a], *y = &s->nodes[b];
    unsigned fx = x->depth + game_count(x->occupied)-1, fy = y->depth + game_count(y->occupied)-1;
    if (fx != fy) return fx < fy;
    if (x->depth != y->depth) return x->depth > y->depth;
    if (x->heuristic != y->heuristic) return x->heuristic > y->heuristic;
    return mix(x->occupied ^ s->seed) < mix(y->occupied ^ s->seed);
}
static void push(Search *s, guint32 node)
{
    unsigned i = s->heap_count++;
    while (i && precedes(s, node, s->heap[(i-1)/2])) { s->heap[i] = s->heap[(i-1)/2]; i = (i-1)/2; }
    s->heap[i] = node;
}
static guint32 pop(Search *s)
{
    guint32 result = s->heap[0], tail = s->heap[--s->heap_count]; unsigned i = 0;
    while (i*2+1 < s->heap_count) {
        unsigned child = i*2+1;
        if (child+1 < s->heap_count && precedes(s, s->heap[child+1], s->heap[child])) child++;
        if (!precedes(s, s->heap[child], tail)) break;
        s->heap[i] = s->heap[child]; i = child;
    }
    if (s->heap_count) s->heap[i] = tail;
    return result;
}
static void astar(Search *s)
{
    Game current = s->root;
    s->nodes[0] = (Node) {.occupied = current.occupied, .parent = G_MAXUINT32, .heuristic = game_position_heuristic(&current)};
    s->count = 1; Entry *first = lookup(s, game_canonical(&current)); first->key = game_canonical(&current)+1; s->used = 1; push(s, 0);
    while (s->heap_count && !stopped(s)) {
        guint32 index = pop(s); Node node = s->nodes[index];
        current = s->root; current.occupied = node.occupied;
        s->result.nodes++;
        GameResult terminal = game_result(&current);
        if (terminal == GAME_SOLVED) {
            s->result.length = node.depth;
            for (unsigned i = node.depth; i; i--) { s->result.path[i-1] = s->nodes[index].move; index = s->nodes[index].parent; }
            s->result.status = SEARCH_SOLVED; return;
        }
        if (terminal != GAME_ONGOING) continue;
        MoveList legal = game_legal_moves(&current);
        for (unsigned i = 0; i < legal.count; i++) {
            Game child = current; game_play(&child, legal.items[i]);
            guint64 key = game_canonical(&child); Entry *entry = lookup(s, key);
            if (entry->key) continue;
            if (s->count >= s->node_capacity || !reserve_entry(s)) { s->result.status = SEARCH_LIMIT; return; }
            guint32 next = s->count++;
            s->nodes[next] = (Node) {.occupied = child.occupied, .heuristic = game_position_heuristic(&child),
                .parent = index, .move = legal.items[i], .depth = node.depth+1};
            entry->key = key+1; entry->node = next; push(s, next);
        }
    }
}
static gboolean allocate(Search *s, gboolean a_star)
{
    gsize overhead = sizeof(Search) + MAX_JUMPS*(sizeof(Game)+MAX_MOVES*sizeof(Candidate)) + 4096;
    if (s->limits.memory_bytes <= overhead) { s->result.status = SEARCH_LIMIT; return FALSE; }
    gsize budget = s->limits.memory_bytes-overhead;
    gsize per_item = a_star ? sizeof(Node)+sizeof(guint32)+2*sizeof(Entry) : sizeof(Entry);
    gsize capacity = 1;
    while (capacity <= G_MAXUINT32/2 && capacity*2 <= budget/per_item) capacity *= 2;
    if (capacity < 16) { s->result.status = SEARCH_LIMIT; return FALSE; }
    s->capacity = a_star ? capacity*2 : capacity;
    s->table = g_try_malloc0_n(s->capacity, sizeof(Entry));
    s->result.peak_memory = overhead+s->capacity*sizeof(Entry);
    if (a_star) {
        s->node_capacity = capacity;
        s->nodes = g_try_malloc_n(capacity, sizeof(Node)); s->heap = g_try_malloc_n(capacity, sizeof(guint32));
        s->result.peak_memory += capacity*(sizeof(Node)+sizeof(guint32));
    }
    if (!s->table || (a_star && (!s->nodes || !s->heap))) { s->result.status = SEARCH_ERROR; return FALSE; }
    return TRUE;
}
SearchResult search_solve(const Game *game, PlayerKind algorithm, const SearchLimits *limits, const SearchControl *control, guint32 seed)
{
    Search s = {.root = *game, .control = control, .limits = limits ? *limits : search_defaults(), .seed = seed,
        .result = {.status = SEARCH_UNSOLVABLE}, .start = g_get_monotonic_time()};
    if (algorithm != PLAYER_BACKTRACKING && algorithm != PLAYER_ASTAR) s.result.status = SEARCH_ERROR;
    if (!stopped(&s) && allocate(&s, algorithm == PLAYER_ASTAR)) {
        if (algorithm == PLAYER_ASTAR) astar(&s);
        else { Game board = *game; dfs(&s, &board, 0); }
    }
    s.result.elapsed_us = g_get_monotonic_time()-s.start;
    if (control && control->progress) control->progress(s.result.nodes, s.result.elapsed_us, s.result.peak_memory, control->data);
    g_free(s.table); g_free(s.nodes); g_free(s.heap);
    return s.result;
}
SearchResult search_certify(const Game *game, const SearchLimits *limits, const SearchControl *control, guint32 seed, MoveList *certified)
{
    *certified = (MoveList) {0};
    SearchLimits remaining = limits ? *limits : search_defaults();
    SearchResult total = {.status = SEARCH_UNSOLVABLE}; gint64 start = g_get_monotonic_time();
    MoveList legal = game_legal_moves(game);
    for (unsigned i = 0; i < legal.count; i++) {
        if (remaining.seconds > 0) {
            remaining.seconds = (limits ? limits->seconds : 60) - (g_get_monotonic_time()-start)/1000000.0;
            if (remaining.seconds <= 0) { total.status = SEARCH_LIMIT; break; }
        }
        if (limits && limits->node_limit) {
            if (total.nodes >= limits->node_limit) { total.status = SEARCH_LIMIT; break; }
            remaining.node_limit = limits->node_limit-total.nodes;
        }
        Game child = *game; game_play(&child, legal.items[i]);
        SearchResult result = search_solve(&child, PLAYER_BACKTRACKING, &remaining, control, seed+i);
        total.nodes += result.nodes; total.peak_memory = MAX(total.peak_memory, result.peak_memory);
        if (result.status == SEARCH_SOLVED) certified->items[certified->count++] = legal.items[i];
        else if (result.status != SEARCH_UNSOLVABLE) { total.status = result.status; break; }
    }
    if (certified->count && total.status == SEARCH_UNSOLVABLE) total.status = SEARCH_SOLVED;
    total.elapsed_us = g_get_monotonic_time()-start;
    return total;
}
