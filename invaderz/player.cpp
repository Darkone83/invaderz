// player.cpp
#include "player.h"

static __forceinline int Clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static __forceinline WORD EdgePressed(WORD now, WORD prev, WORD bit)
{
    return (WORD)((now & bit) && !(prev & bit));
}

void Player_Init(PlayerState& p,
    int startX, int startY,
    int w, int h,
    int speed,
    int boundLeft, int boundRight,
    int fireCooldownFrames)
{
    p.x = startX;
    p.y = startY;

    p.w = w;
    p.h = h;

    p.speed = speed;

    p.boundLeft = boundLeft;
    p.boundRight = boundRight;

    p.fireCooldown = (fireCooldownFrames < 0) ? 0 : fireCooldownFrames;
    p.fireTimer = 0;

    p.prevButtons = 0;
}

void Player_Reset(PlayerState& p, int startX, int startY)
{
    p.x = startX;
    p.y = startY;
    p.fireTimer = 0;
    p.prevButtons = 0;
}

bool Player_Update(PlayerState& p)
{
    WORD now = GetButtons();

    // Movement (digital)
    int dx = 0;
    if (now & BTN_DPAD_LEFT)  dx -= p.speed;
    if (now & BTN_DPAD_RIGHT) dx += p.speed;

    p.x += dx;

    // Clamp to bounds (treat p.x as left edge; boundRight is screen max X)
    int minX = p.boundLeft;
    int maxX = p.boundRight - (p.w - 1);
    p.x = Clamp(p.x, minX, maxX);

    // Fire timer
    if (p.fireTimer > 0)
        p.fireTimer--;

    // Fire edge: A (primary). Also allow B if you want.
    bool fire = false;
    if (p.fireTimer == 0)
    {
        if (EdgePressed(now, p.prevButtons, BTN_A) || EdgePressed(now, p.prevButtons, BTN_B))
        {
            fire = true;
            p.fireTimer = p.fireCooldown;
        }
    }

    p.prevButtons = now;
    return fire;
}

void Player_GetCenter(const PlayerState& p, int& outCx, int& outCy)
{
    outCx = p.x + (p.w / 2);
    outCy = p.y + (p.h / 2);
}
