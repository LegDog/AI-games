// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game.h"
typedef struct { double x, y; } BoardPoint;
typedef struct { BoardPoint start, control, end; double parameters[GAME_PATH_POINTS]; } BoardPath;
extern const BoardPoint board_points[GAME_POINTS];
extern const BoardPath board_paths[GAME_PATHS];
BoardPoint board_evaluate(unsigned path, double parameter);
/* Arc-length progress along a directed legal trajectory, including its middle. */
BoardPoint board_move_position(unsigned move, double progress);
