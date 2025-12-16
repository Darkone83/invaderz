// enemy.h
#pragma once

#include <xtl.h>

// Space Invaders-style enemy formation (integer-only).
// Owns:
//  - formation layout + reset
//  - classic left/right march + step-down on edge hit
//  - alive flags, remaining count
//  - per-row score values
//
// Rendering / bullets are done elsewhere.

static const int ENEMY_COLS = 11;
static const int ENEMY_ROWS = 5;
static const int ENEMY_MAX = ENEMY_COLS * ENEMY_ROWS;

struct EnemyState
{
    // Formation origin (top-left of grid)
    int originX;
    int originY;

    // Cell spacing (in pixels)
    int cellW;
    int cellH;

    // March state
    int dir;            // -1 = left, +1 = right
    int stepDown;       // pixels to move down when bouncing at edge
    int stepX;          // pixels per horizontal move step

    // Timing (frames)
    int tick;           // frame counter
    int stepFrames;     // frames per move step (smaller = faster)
    int stepFramesMin;  // clamp min speed

    // Bounds we march within (screen-space)
    int boundLeft;
    int boundRight;

    // Alive flags + remaining count
    BYTE alive[ENEMY_MAX];
    int  remaining;

    // Row score values (row 0 = top)
    int rowScore[ENEMY_ROWS];

    // Cached overall alive bounds (columns)
    int aliveMinCol;
    int aliveMaxCol;
};

void Enemy_Init(EnemyState& e,
    int originX, int originY,
    int cellW, int cellH,
    int boundLeft, int boundRight);

void Enemy_Reset(EnemyState& e);                 // all alive, reset march + speed
void Enemy_Update(EnemyState& e);                // march logic (call once per frame)

// Query
bool Enemy_IsAlive(const EnemyState& e, int row, int col);
int  Enemy_Index(int row, int col);
void Enemy_GetCellPos(const EnemyState& e, int row, int col, int& outX, int& outY);

// Kill / scoring
bool Enemy_Kill(EnemyState& e, int row, int col, int& outPoints); // returns true if enemy was alive

// Utility
bool Enemy_AllDead(const EnemyState& e);

int  Enemy_FindBottomAliveRowInCol(const EnemyState& e, int col);

bool Enemy_PickShooter(const EnemyState& e, DWORD seed,
    int& outRow, int& outCol, int& outX, int& outY);
