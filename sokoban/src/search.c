// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <math.h>
#include <string.h>
typedef struct {
  Position pos;
  guint32 parent, depth, estimate, tie;
  guint16 region, move;
} Node;
typedef struct {
  Node *nodes;
  guint32 *heap, *table;
  unsigned count, heaps, capacity;
  const Game *initial;
  PlayerKind kind;
  SearchLimits limits;
  const SearchControl *control;
  SearchResult result;
  gint64 start;
  Analysis *analysis;
  GRand *random;
} Search;
SearchLimits search_defaults(void) {
  return (SearchLimits){60, 256 * 1024 * 1024, 0, TRUE};
}
const char *search_status_name(SearchStatus status) {
  static const char *names[] = {"solved", "unsolvable", "limit-reached",
                                "cancelled", "error"};
  return status <= SEARCH_ERROR ? names[status] : "error";
}
void search_result_clear(SearchResult *r) {
  g_clear_pointer(&r->path, g_array_unref);
}
static gboolean stopped(Search *s) {
  if (s->control && s->control->cancelled &&
      s->control->cancelled(s->control->data)) {
    s->result.status = SEARCH_CANCELLED;
    return TRUE;
  }
  if ((s->limits.seconds > 0 &&
       g_get_monotonic_time() - s->start >= s->limits.seconds * 1000000.0) ||
      (s->limits.node_limit && s->result.nodes >= s->limits.node_limit)) {
    s->result.status = SEARCH_LIMIT;
    return TRUE;
  }
  return FALSE;
}
static guint32 position_hash(const Position *p, unsigned boxes,
                             unsigned region) {
  guint32 hash = 2166136261u;
  for (unsigned i = 0; i < boxes; i++)
    hash = (hash ^ p->boxes[i]) * 16777619u;
  return (hash ^ region) * 16777619u;
}
static unsigned region_of(const Game *g) {
  gboolean reach[MAX_CELLS];
  game_reachable(g, reach, NULL, NULL);
  for (unsigned c = 0; c < MAX_CELLS; c++)
    if (reach[c])
      return c;
  return 0;
}
static unsigned bucket(Search *s, const Position *p, unsigned region) {
  unsigned mask = s->capacity * 2 - 1,
           index = position_hash(p, s->initial->level->boxes, region) & mask;
  while (s->table[index]) {
    Node *n = &s->nodes[s->table[index] - 1];
    if (n->region == region &&
        !memcmp(n->pos.boxes, p->boxes,
                s->initial->level->boxes * sizeof(guint16)))
      break;
    index = (index + 1) & mask;
  }
  return index;
}
static gboolean before(Search *s, unsigned a, unsigned b) {
  Node *x = &s->nodes[a], *y = &s->nodes[b];
  guint64 fx = x->estimate + (s->kind == PLAYER_ASTAR ? (guint64)x->depth : 0),
          fy = y->estimate + (s->kind == PLAYER_ASTAR ? (guint64)y->depth : 0);
  if (fx != fy)
    return fx < fy;
  if (x->estimate != y->estimate)
    return x->estimate < y->estimate;
  return x->tie != y->tie ? x->tie < y->tie : a < b;
}
static void enqueue(Search *s, unsigned node) {
  unsigned at = s->heaps++;
  while (at) {
    unsigned parent = (at - 1) / 2;
    if (!before(s, node, s->heap[parent]))
      break;
    s->heap[at] = s->heap[parent];
    at = parent;
  }
  s->heap[at] = node;
}
static unsigned dequeue(Search *s) {
  unsigned result = s->heap[0], last = s->heap[--s->heaps], at = 0;
  while (at * 2 + 1 < s->heaps) {
    unsigned child = at * 2 + 1;
    if (child + 1 < s->heaps && before(s, s->heap[child + 1], s->heap[child]))
      child++;
    if (!before(s, s->heap[child], last))
      break;
    s->heap[at] = s->heap[child];
    at = child;
  }
  if (s->heaps)
    s->heap[at] = last;
  return result;
}
SearchResult search_solve(const Game *g, PlayerKind kind,
                          const SearchLimits *limits,
                          const SearchControl *control, guint32 seed) {
  Search s = {.initial = g,
              .kind = kind,
              .limits = limits ? *limits : search_defaults(),
              .control = control,
              .start = g_get_monotonic_time(),
              .result = {.status = SEARCH_ERROR}};
  if (!game_valid(g) || (kind != PLAYER_ASTAR && kind != PLAYER_GREEDY) ||
      !isfinite(s.limits.seconds) || s.limits.seconds < 0 ||
      !s.limits.memory_bytes)
    goto finish;
  if (stopped(&s))
    goto finish;
  if (game_solved(g)) {
    s.result.status = SEARCH_SOLVED;
    s.result.optimal = kind == PLAYER_ASTAR;
    s.result.path = g_array_new(FALSE, FALSE, sizeof(guint16));
    goto finish;
  }
  /* Fixed-capacity arenas make memory exhaustion explicit and reproducible.
   * Reserve space for preprocessing, scratch stacks, allocator overhead and a
   * complete reconstructed path, in addition to node/heap/hash storage. */
  const gsize reserve = 192 * 1024,
              per = sizeof(Node) + sizeof(guint32) * 3 + sizeof(guint16);
  if (s.limits.memory_bytes < reserve + per * 8) {
    s.result.status = SEARCH_LIMIT;
    goto finish;
  }
  gsize available = (s.limits.memory_bytes - reserve) / per;
  s.capacity = 8;
  while (s.capacity <= G_MAXUINT32 / 4 && (gsize)s.capacity * 2 <= available)
    s.capacity *= 2;
  if (s.limits.node_limit)
    while (s.capacity > 8 && s.capacity / 2 >= s.limits.node_limit)
      s.capacity /= 2;
  s.result.peak_memory = reserve + (gsize)s.capacity * per;
  s.nodes = g_try_new0(Node, s.capacity);
  s.heap = g_try_new(guint32, s.capacity);
  s.table = g_try_new0(guint32, s.capacity * 2);
  s.analysis = g_try_new(Analysis, 1);
  if (!s.nodes || !s.heap || !s.table || !s.analysis)
    goto finish;
  s.random = g_rand_new_with_seed(seed);
  game_analyze(g->level, s.analysis);
  if (stopped(&s))
    goto finish;
  if (s.limits.pruning && game_deadlocked(g, s.analysis)) {
    s.result.status = SEARCH_UNSOLVABLE;
    goto finish;
  }
  gboolean impossible;
  unsigned estimate = game_estimate(g, s.analysis, &impossible),
           region = region_of(g);
  s.nodes[0] = (Node){.pos = g->pos,
                      .parent = G_MAXUINT32,
                      .estimate = estimate,
                      .region = region,
                      .tie = g_rand_int(s.random)};
  s.count = 1;
  s.table[bucket(&s, &g->pos, region)] = 1;
  enqueue(&s, 0);
  s.result.status = SEARCH_UNSOLVABLE;
  while (s.heaps) {
    if (stopped(&s))
      goto finish;
    unsigned current = dequeue(&s);
    Node n = s.nodes[current];
    if (s.table[bucket(&s, &n.pos, n.region)] != current + 1)
      continue;
    Game state = {.level = g->level, .pos = n.pos};
    s.result.nodes++;
    if (game_solved(&state)) {
      s.result.path = g_array_sized_new(FALSE, FALSE, sizeof(guint16), n.depth);
      g_array_set_size(s.result.path, n.depth);
      unsigned index = current;
      for (unsigned i = n.depth; i; i--) {
        g_array_index(s.result.path, guint16, i - 1) = s.nodes[index].move;
        index = s.nodes[index].parent;
      }
      s.result.status = SEARCH_SOLVED;
      s.result.optimal = kind == PLAYER_ASTAR;
      goto finish;
    }
    if (s.control && s.control->progress && s.result.nodes % 128 == 0)
      s.control->progress(s.result.nodes, g_get_monotonic_time() - s.start,
                          s.result.peak_memory, s.control->data);
    MoveList moves = game_pushes(&state);
    for (unsigned i = 0; i < moves.count; i++) {
      if (stopped(&s))
        goto finish;
      unsigned move = moves.items[i];
      Game next = state;
      next.pos.player = game_neighbor(g->level, move / 4, (move % 4) ^ 1);
      if (!game_step(&next, move % 4, NULL)) {
        s.result.status = SEARCH_ERROR;
        goto finish;
      }
      if (s.limits.pruning && game_deadlocked(&next, s.analysis))
        continue;
      unsigned reg = region_of(&next), slot = bucket(&s, &next.pos, reg),
               depth = n.depth + 1;
      if (s.table[slot] && s.nodes[s.table[slot] - 1].depth <= depth)
        continue;
      if (s.count == s.capacity) {
        s.result.status = SEARCH_LIMIT;
        goto finish;
      }
      unsigned h = game_estimate(&next, s.analysis, &impossible),
               index = s.count++;
      s.nodes[index] = (Node){.pos = next.pos,
                              .parent = current,
                              .depth = depth,
                              .estimate = h,
                              .region = reg,
                              .move = move,
                              .tie = g_rand_int(s.random)};
      s.table[slot] = index + 1;
      enqueue(&s, index);
    }
  }
finish:
  if (s.result.status == SEARCH_SOLVED && s.result.path) {
    Game replay = *g;
    for (unsigned i = 0; i < s.result.path->len; i++) {
      if (s.limits.seconds > 0 &&
          g_get_monotonic_time() - s.start >= s.limits.seconds * 1000000.0) {
        s.result.status = SEARCH_LIMIT;
        break;
      }
      if (control && control->cancelled && control->cancelled(control->data)) {
        s.result.status = SEARCH_CANCELLED;
        break;
      }
      GByteArray *walk =
          game_push_path(&replay, g_array_index(s.result.path, guint16, i));
      if (!walk) {
        s.result.status = SEARCH_ERROR;
        break;
      }
      for (unsigned j = 0; j < walk->len; j++)
        if (!game_step(&replay, walk->data[j], NULL))
          s.result.status = SEARCH_ERROR;
      g_byte_array_unref(walk);
      if (s.result.status != SEARCH_SOLVED)
        break;
    }
    if (s.result.status == SEARCH_SOLVED && !game_solved(&replay))
      s.result.status = SEARCH_ERROR;
    if (s.result.status != SEARCH_SOLVED) {
      search_result_clear(&s.result);
      s.result.optimal = FALSE;
    }
  }
  s.result.elapsed_us = g_get_monotonic_time() - s.start;
  g_free(s.nodes);
  g_free(s.heap);
  g_free(s.table);
  g_free(s.analysis);
  if (s.random)
    g_rand_free(s.random);
  return s.result;
}
