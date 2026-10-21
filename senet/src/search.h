// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game.h"
/* depth counts decisions, including bonus turns; chance rolls use exact
 * weights. */
GameMove search_choose(const Game *g, unsigned depth, GRand *rng);
double search_value(const Game *g, int player, unsigned depth);
