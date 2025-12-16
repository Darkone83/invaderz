// enemy.cpp
#include "enemy.h"

int Enemy_Index(int row, int col)
{
    return row * ENEMY_COLS + col;
}

static void RecomputeAliveCols(EnemyState& e)
{
    int minC = ENEMY_COLS;
    int maxC = -1;

    for (int r = 0; r < ENEMY_ROWS; ++r)
    {
        for (int c = 0; c < ENEMY_COLS; ++c)
        {
            int i = Enemy_Index(r, c);
            if (e.alive[i])
            {
                if (c < minC) minC = c;
                if (c > maxC) maxC = c;
            }
        }
    }

    if (maxC < 0)
    {
        // none alive
        e.aliveMinCol = 0;
        e.aliveMaxCol = 0;
    }
    else
    {
        e.aliveMinCol = minC;
        e.aliveMaxCol = maxC;
    }
}

void Enemy_Init(EnemyState& e,
    int originX, int originY,
    int cellW, int cellH,
    int boundLeft, int boundRight)
{
    e.originX = originX;
    e.originY = originY;

    e.cellW = cellW;
    e.cellH = cellH;

    e.boundLeft = boundLeft;
    e.boundRight = boundRight;

    // Default march params (tune later)
    e.dir = +1;
    e.stepDown = 12;
    e.stepX = 6;

    e.tick = 0;
    e.stepFrames = 30;
    e.stepFramesMin = 6;

    // Classic-ish per-row points (top -> bottom)
    e.rowScore[0] = 40;
    e.rowScore[1] = 20;
    e.rowScore[2] = 20;
    e.rowScore[3] = 10;
    e.rowScore[4] = 10;

    Enemy_Reset(e);
}

void Enemy_Reset(EnemyState& e)
{
    for (int i = 0; i < ENEMY_MAX; ++i)
        e.alive[i] = 1;

    e.remaining = ENEMY_MAX;

    e.dir = +1;
    e.tick = 0;

    // reset speed
    e.stepFrames = 30;

    // (leave origin as configured)
    RecomputeAliveCols(e);
}

bool Enemy_IsAlive(const EnemyState& e, int row, int col)
{
    if (row < 0 || row >= ENEMY_ROWS) return false;
    if (col < 0 || col >= ENEMY_COLS) return false;
    return (e.alive[Enemy_Index(row, col)] != 0);
}

void Enemy_GetCellPos(const EnemyState& e, int row, int col, int& outX, int& outY)
{
    outX = e.originX + col * e.cellW;
    outY = e.originY + row * e.cellH;
}

static void Speedup(EnemyState& e)
{
    // Simple speedup as enemies die: shrink stepFrames but keep a floor.
    if (e.stepFrames > e.stepFramesMin)
        e.stepFrames--;
    if (e.stepFrames < e.stepFramesMin)
        e.stepFrames = e.stepFramesMin;
}

bool Enemy_Kill(EnemyState& e, int row, int col, int& outPoints)
{
    outPoints = 0;

    if (row < 0 || row >= ENEMY_ROWS) return false;
    if (col < 0 || col >= ENEMY_COLS) return false;

    int idx = Enemy_Index(row, col);
    if (!e.alive[idx])
        return false;

    e.alive[idx] = 0;
    e.remaining--;
    if (e.remaining < 0) e.remaining = 0;

    outPoints = e.rowScore[row];

    // update alive bounds + speed up
    RecomputeAliveCols(e);
    Speedup(e);

    return true;
}

bool Enemy_AllDead(const EnemyState& e)
{
    return (e.remaining <= 0);
}

void Enemy_Update(EnemyState& e)
{
    if (e.remaining <= 0)
        return;

    e.tick++;
    if (e.tick < e.stepFrames)
        return;

    e.tick = 0;

    // Compute pixel extents of alive formation.
    // IMPORTANT: right extent should include the width of the last alive column.
    // leftX: originX + minCol*cellW
    // rightX: originX + (maxCol+1)*cellW
    int leftX = e.originX + e.aliveMinCol * e.cellW;
    int rightX = e.originX + (e.aliveMaxCol + 1) * e.cellW;

    // If next horizontal step would exceed bounds, bounce + step down.
    int nextLeft = leftX + e.dir * e.stepX;
    int nextRight = rightX + e.dir * e.stepX;

    if (nextLeft <= e.boundLeft || nextRight >= e.boundRight)
    {
        e.dir = -e.dir;
        e.originY += e.stepDown;

        // Optional: small speedup on each drop for that classic ramp
        Speedup(e);
    }
    else
    {
        e.originX += e.dir * e.stepX;
    }
}

// -----------------------------------------------------------------------------
// NEW shooter helpers
// -----------------------------------------------------------------------------
int Enemy_FindBottomAliveRowInCol(const EnemyState& e, int col)
{
    if (col < 0 || col >= ENEMY_COLS) return -1;

    for (int r = ENEMY_ROWS - 1; r >= 0; --r)
    {
        if (e.alive[Enemy_Index(r, col)] != 0)
            return r;
    }
    return -1;
}

bool Enemy_PickShooter(const EnemyState& e, DWORD seed,
    int& outRow, int& outCol, int& outX, int& outY)
{
    outRow = -1;
    outCol = -1;
    outX = 0;
    outY = 0;

    if (e.remaining <= 0)
        return false;

    // Pick a starting column from the seed, then probe all columns (wrap)
    int startCol = (int)(seed % (DWORD)ENEMY_COLS);

    for (int k = 0; k < ENEMY_COLS; ++k)
    {
        int c = startCol + k;
        if (c >= ENEMY_COLS) c -= ENEMY_COLS;

        int r = Enemy_FindBottomAliveRowInCol(e, c);
        if (r >= 0)
        {
            outRow = r;
            outCol = c;
            Enemy_GetCellPos(e, r, c, outX, outY);
            return true;
        }
    }

    return false;
}
