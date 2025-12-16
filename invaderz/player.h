// player.h
#pragma once

#include <xtl.h>
#include "input.h"

// Simple Space Invaders player controller (integer-only).
// Owns position + fire cooldown. Rendering is done elsewhere.
//
// Notes:
// - Uses GetButtons() / BTN_* from input.h
// - Provide bounds in screen pixels (0..639 typically)

struct PlayerState
{
    int x;              // top-left
    int y;              // top-left

    int w;
    int h;

    int speed;          // pixels per frame

    int boundLeft;
    int boundRight;     // inclusive right edge of screen (e.g. 639)

    // Fire control
    int  fireCooldown;  // frames between shots
    int  fireTimer;     // counts down to 0

    WORD prevButtons;   // for edge detection (A / B)
};

void Player_Init(PlayerState& p,
    int startX, int startY,
    int w, int h,
    int speed,
    int boundLeft, int boundRight,
    int fireCooldownFrames);

void Player_Reset(PlayerState& p, int startX, int startY);

// Update position + fire timing.
// Returns true if a shot should be spawned this frame.
bool Player_Update(PlayerState& p);

// Utility
void Player_GetCenter(const PlayerState& p, int& outCx, int& outCy);
