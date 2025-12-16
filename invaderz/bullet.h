// bullet.h
#pragma once

#include <xtl.h>

// Simple pooled bullet system (integer-only).
// Use for player bullets and/or enemy bullets (owner flag).
//
// Coordinate system: top-left screen pixels.
// Bullets are AABB rectangles (x,y,w,h).

enum BulletOwner
{
    BULLET_PLAYER = 0,
    BULLET_ENEMY = 1
};

struct Bullet
{
    int x, y;
    int vx, vy;     // pixels per frame
    int w, h;
    BYTE active;
    BYTE owner;     // BulletOwner
};

static const int BULLET_MAX = 16;

struct BulletPool
{
    Bullet b[BULLET_MAX];
};

void Bullet_Init(BulletPool& p);

// Spawns a bullet. Returns true on success (free slot found).
bool Bullet_Spawn(BulletPool& p,
    int x, int y,
    int vx, int vy,
    int w, int h,
    BulletOwner owner);

// Updates all active bullets and kills off-screen.
// screenW/screenH are in pixels (e.g. 640/480)
void Bullet_Update(BulletPool& p, int screenW, int screenH);

// Utility
void Bullet_KillAll(BulletPool& p);
