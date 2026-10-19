// SPDX-License-Identifier: GPL-3.0-or-later
#include "search.h"
#include <math.h>
#include <string.h>
enum { SIMULATION_ACTIONS = 324, NONE = G_MAXUINT32 };
typedef struct {
  unsigned parent, child, sibling, move, visits;
  guint64 untried[2];
  double reward;
  gboolean initialized;
} Node;
typedef struct {
  const SearchOptions *options;
  const SearchControl *control;
  SearchResult *result;
  gint64 start;
} Control;
SearchOptions search_defaults(void) {
  return (SearchOptions){1000, 5, 64 * 1024 * 1024, 0};
}
const char *search_status_name(SearchStatus s) {
  static const char *names[] = {"complete", "limit-reached", "cancelled",
                                "error"};
  return s <= SEARCH_ERROR ? names[s] : "error";
}
static gboolean stop(Control *c) {
  if (c->control && c->control->cancelled &&
      c->control->cancelled(c->control->data)) {
    c->result->status = SEARCH_CANCELLED;
    return TRUE;
  }
  if ((c->options->seconds > 0 &&
       g_get_monotonic_time() - c->start >= c->options->seconds * 1000000.0) ||
      (c->options->node_limit && c->result->nodes >= c->options->node_limit)) {
    c->result->status = SEARCH_LIMIT;
    return TRUE;
  }
  return FALSE;
}
static Node node(unsigned parent, unsigned move) {
  return (Node){.parent = parent, .child = NONE, .sibling = NONE, .move = move};
}
static gboolean untried(const Node *n) {
  return n->untried[0] || n->untried[1];
}
static unsigned take_move(Node *n, GRand *random) {
  MoveList list = {0};
  for (unsigned m = 0; m < GO_CHOICES; m++)
    if (n->untried[m / 64] & (G_GUINT64_CONSTANT(1) << (m % 64)))
      list.items[list.count++] = m;
  unsigned move = game_random(&list, random);
  n->untried[move / 64] &= ~(G_GUINT64_CONSTANT(1) << (move % 64));
  return move;
}
gboolean mcts_evaluate(const Game *g, const SearchOptions *provided,
                       const SearchControl *control, GRand *random,
                       SearchResult *r) {
  SearchOptions options = provided ? *provided : search_defaults();
  *r = (SearchResult){.status = SEARCH_ERROR, .requested = options.iterations};
  Control c = {&options, control, r, g_get_monotonic_time()};
  Node *tree = NULL;
  Game simulation = {0};
  if (!game_valid(g) || game_finished(g) || !options.iterations ||
      options.iterations > 1000000 || !isfinite(options.seconds) ||
      options.seconds < 0)
    goto finish;
  r->status = SEARCH_COMPLETE;
  if (stop(&c))
    goto finish;
  gsize overhead =
      4 * ((gsize)g->history->len + SIMULATION_ACTIONS + 1) * sizeof(State) +
      sizeof(Game) + sizeof(SearchResult) + 64 * 1024;
  if (options.memory_bytes < overhead + 2 * sizeof(Node)) {
    r->status = SEARCH_LIMIT;
    goto finish;
  }
  unsigned capacity = MIN((gsize)options.iterations + 1,
                          (options.memory_bytes - overhead) / sizeof(Node));
  r->peak_memory = overhead + (gsize)capacity * sizeof(Node);
  tree = g_try_new0(Node, capacity);
  if (!tree) {
    r->status = SEARCH_ERROR;
    goto finish;
  }
  tree[0] = node(NONE, GO_PASS);
  unsigned count = 1;
  game_copy(&simulation, g, SIMULATION_ACTIONS + 1);
  for (unsigned iteration = 0; iteration < options.iterations; iteration++) {
    if (stop(&c))
      break;
    simulation.state = g->state;
    g_array_set_size(simulation.history, g->history->len);
    memcpy(simulation.seen, g->seen, sizeof g->seen);
    unsigned index = 0, actions = 0;
    while (!game_finished(&simulation) && actions < SIMULATION_ACTIONS) {
      if (stop(&c))
        goto finish;
      Node *n = &tree[index];
      if (!n->initialized) {
        MoveList legal = game_legal(&simulation);
        for (unsigned i = 0; i < legal.count; i++)
          n->untried[legal.items[i] / 64] |= G_GUINT64_CONSTANT(1)
                                             << (legal.items[i] % 64);
        n->initialized = TRUE;
      }
      if (untried(n)) {
        if (count == capacity) {
          r->status = SEARCH_LIMIT;
          goto finish;
        }
        unsigned move = take_move(n, random);
        if (!game_play(&simulation, move)) {
          r->status = SEARCH_ERROR;
          goto finish;
        }
        unsigned child = count++;
        tree[child] = node(index, move);
        tree[child].sibling = n->child;
        n->child = child;
        index = child;
        actions++;
        r->nodes++;
        break;
      }
      unsigned chosen = NONE;
      double best = -G_MAXDOUBLE;
      for (unsigned child = n->child; child != NONE;
           child = tree[child].sibling) {
        Node *v = &tree[child];
        double sign = simulation.state.turn == g->state.turn ? 1 : -1;
        double value =
            v->visits
                ? sign * v->reward / v->visits +
                      sqrt(2 * log(MAX(1u, n->visits)) / (double)v->visits)
                : G_MAXDOUBLE;
        if (value > best) {
          best = value;
          chosen = child;
        }
      }
      if (chosen == NONE || !game_play(&simulation, tree[chosen].move)) {
        r->status = SEARCH_ERROR;
        goto finish;
      }
      index = chosen;
      actions++;
      r->nodes++;
    }
    while (!game_finished(&simulation) && actions < SIMULATION_ACTIONS) {
      if (stop(&c))
        goto finish;
      MoveList legal = game_legal(&simulation);
      int move = game_random(&legal, random);
      if (move < 0 || !game_play(&simulation, move)) {
        r->status = SEARCH_ERROR;
        goto finish;
      }
      actions++;
      r->nodes++;
    }
    double reward;
    int winner = game_winner(&simulation);
    if (winner >= 0)
      reward = winner == 2 ? 0 : winner == (int)g->state.turn ? 1 : -1;
    else {
      Score score;
      game_score(&simulation, &score);
      reward = CLAMP(score.margin / (GO_POINTS + g->komi), -1, 1) *
               (g->state.turn == BLACK ? 1 : -1);
      r->cutoffs++;
    }
    for (unsigned at = index; at != NONE; at = tree[at].parent) {
      tree[at].visits++;
      tree[at].reward += reward;
    }
    r->completed++;
    r->tree_nodes = count;
    if (control && control->progress && r->completed % 8 == 0)
      control->progress(r->completed, r->nodes, control->data);
  }
finish:
  if (tree) {
    for (unsigned child = tree[0].child; child != NONE;
         child = tree[child].sibling) {
      unsigned m = tree[child].move;
      r->visits[m] = tree[child].visits;
      r->means[m] =
          tree[child].visits ? tree[child].reward / tree[child].visits : 0;
    }
  }
  game_clear(&simulation);
  g_free(tree);
  r->elapsed_us = g_get_monotonic_time() - c.start;
  return r->completed > 0 &&
         (r->status == SEARCH_COMPLETE || r->status == SEARCH_LIMIT);
}
int search_choose(const SearchResult *r, GRand *random) {
  unsigned visits = 0, ties = 0;
  double mean = -G_MAXDOUBLE;
  int choice = -1;
  for (unsigned m = 0; m < GO_CHOICES; m++)
    if (r->visits[m]) {
      if (r->visits[m] > visits ||
          (r->visits[m] == visits && r->means[m] > mean)) {
        visits = r->visits[m];
        mean = r->means[m];
        choice = m;
        ties = 1;
      } else if (r->visits[m] == visits && r->means[m] == mean &&
                 g_rand_int_range(random, 0, ++ties) == 0)
        choice = m;
    }
  return choice;
}
