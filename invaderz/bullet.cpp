// bullet.cpp
#include "bullet.h"

void Bullet_Init(BulletPool& p)
{
    for (int i = 0; i < BULLET_MAX; ++i)
    {
        p.b[i].active = 0;
        p.b[i].owner = BULLET_PLAYER;
        p.b[i].x = p.b[i].y = 0;
        p.b[i].vx = p.b[i].vy = 0;
        p.b[i].w = p.b[i].h = 1;
    }
}

void Bullet_KillAll(BulletPool& p)
{
    for (int i = 0; i < BULLET_MAX; ++i)
        p.b[i].active = 0;
}

static __forceinline int Offscreen(const Bullet& b, int screenW, int screenH)
{
    // AABB offscreen test
    if (b.x + b.w <= 0) return 1;
    if (b.y + b.h <= 0) return 1;
    if (b.x >= screenW) return 1;
    if (b.y >= screenH) return 1;
    return 0;
}

bool Bullet_Spawn(BulletPool& p,
    int x, int y,
    int vx, int vy,
    int w, int h,
    BulletOwner owner)
{
    for (int i = 0; i < BULLET_MAX; ++i)
    {
        if (!p.b[i].active)
        {
            Bullet& b = p.b[i];
            b.active = 1;
            b.owner = (BYTE)owner;

            b.x = x;
            b.y = y;

            b.vx = vx;
            b.vy = vy;

            b.w = (w <= 0) ? 1 : w;
            b.h = (h <= 0) ? 1 : h;
            return true;
        }
    }
    return false;
}

void Bullet_Update(BulletPool& p, int screenW, int screenH)
{
    for (int i = 0; i < BULLET_MAX; ++i)
    {
        Bullet& b = p.b[i];
        if (!b.active)
            continue;

        b.x += b.vx;
        b.y += b.vy;

        if (Offscreen(b, screenW, screenH))
            b.active = 0;
    }
}
