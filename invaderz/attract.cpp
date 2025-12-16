#include "attract.h"

#include <xtl.h>
#include <xgraphics.h>
#include <string.h>
#include <stdlib.h>

#include "input.h"
#include "font.h"
#include "title.h"            // Title_IsSecret()
#include "sprites.h"
#include "sprites_classic.h"
#include "sprites_secret.h"
#include "SpriteAnimator.h"
#include "score.h"

// Device provided by main.cpp
extern LPDIRECT3DDEVICE8 g_pDevice;

static const int SCREEN_W = 640;
static const int SCREEN_H = 480;

// ----------------------------------------------------------------------------
// 2D helpers
// ----------------------------------------------------------------------------
struct V2D
{
    float x, y, z, rhw;
    DWORD color;
};

struct V2DTex
{
    float x, y, z, rhw;
    DWORD color;
    float u, v;
};

#define FVF_2D    (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)
#define FVF_2DTEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

// ----------------------------------------------------------------------------
// DDS loader (A8R8G8B8, pow2, swizzled) for clouds overlay
// ----------------------------------------------------------------------------
#pragma pack(push, 1)
struct DDS_PIXELFORMAT
{
    DWORD size;
    DWORD flags;
    DWORD fourCC;
    DWORD rgbBitCount;
    DWORD rMask;
    DWORD gMask;
    DWORD bMask;
    DWORD aMask;
};

struct DDS_HEADER
{
    DWORD           size;
    DWORD           flags;
    DWORD           height;
    DWORD           width;
    DWORD           pitchOrLinearSize;
    DWORD           depth;
    DWORD           mipMapCount;
    DWORD           reserved1[11];
    DDS_PIXELFORMAT ddspf;
    DWORD           caps;
    DWORD           caps2;
    DWORD           caps3;
    DWORD           caps4;
    DWORD           reserved2;
};
#pragma pack(pop)

static __forceinline int IsPow2(int v) { return (v > 0) && ((v & (v - 1)) == 0); }

static LPDIRECT3DTEXTURE8 LoadTextureFromDDS_Rect(const char* path, int& outW, int& outH)
{
    outW = 0; outH = 0;
    if (!g_pDevice || !path || !path[0]) return NULL;

    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;

    DWORD bytesRead = 0;

    DWORD magic = 0;
    if (!ReadFile(hFile, &magic, sizeof(DWORD), &bytesRead, NULL) ||
        bytesRead != sizeof(DWORD) ||
        magic != 0x20534444) // "DDS "
    {
        CloseHandle(hFile);
        return NULL;
    }

    DDS_HEADER hdr;
    if (!ReadFile(hFile, &hdr, sizeof(DDS_HEADER), &bytesRead, NULL) || bytesRead != sizeof(DDS_HEADER))
    {
        CloseHandle(hFile);
        return NULL;
    }

    if (hdr.size != 124 || hdr.ddspf.size != 32)
    {
        CloseHandle(hFile);
        return NULL;
    }

    const DWORD DDPF_FOURCC = 0x4;
    const DWORD DDPF_RGB = 0x40;
    const DWORD DDPF_ALPHAPIXELS = 0x1;

    if (hdr.ddspf.flags & DDPF_FOURCC)
    {
        CloseHandle(hFile);
        return NULL;
    }

    if (hdr.ddspf.rgbBitCount != 32 ||
        (hdr.ddspf.flags & (DDPF_RGB | DDPF_ALPHAPIXELS)) != (DDPF_RGB | DDPF_ALPHAPIXELS) ||
        hdr.ddspf.rMask != 0x00FF0000 ||
        hdr.ddspf.gMask != 0x0000FF00 ||
        hdr.ddspf.bMask != 0x000000FF ||
        hdr.ddspf.aMask != 0xFF000000)
    {
        CloseHandle(hFile);
        return NULL;
    }

    int w = (int)hdr.width;
    int h = (int)hdr.height;

    if (!IsPow2(w) || !IsPow2(h))
    {
        CloseHandle(hFile);
        return NULL;
    }

    DWORD pixelBytes = (DWORD)(w * h * 4);
    BYTE* pixels = (BYTE*)malloc(pixelBytes);
    if (!pixels)
    {
        CloseHandle(hFile);
        return NULL;
    }

    if (!ReadFile(hFile, pixels, pixelBytes, &bytesRead, NULL) || bytesRead != pixelBytes)
    {
        free(pixels);
        CloseHandle(hFile);
        return NULL;
    }

    CloseHandle(hFile);

    LPDIRECT3DTEXTURE8 tex = NULL;
    if (FAILED(g_pDevice->CreateTexture((UINT)w, (UINT)h, 1, 0, D3DFMT_A8R8G8B8, 0, &tex)))
    {
        free(pixels);
        return NULL;
    }

    D3DLOCKED_RECT lr;
    if (FAILED(tex->LockRect(0, &lr, NULL, 0)))
    {
        tex->Release();
        free(pixels);
        return NULL;
    }

    XGSwizzleRect(pixels, w * 4, NULL, lr.pBits, w, h, NULL, 4);
    tex->UnlockRect(0);

    free(pixels);

    outW = w;
    outH = h;
    return tex;
}

// ----------------------------------------------------------------------------
// Local state
// ----------------------------------------------------------------------------
static bool s_running = false;
static bool s_secret = false;

static WORD s_prevButtons = 0;
static int  s_frame = 0;

static int  s_demoFramesLeft = 0; // counts down; when 0 -> exit
static const int kDemoSeconds = 45;
static const int kFPS = 60;

// Starfield
static DWORD s_rng = 0x13579BDFu;
static __forceinline DWORD RngNext()
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return s_rng;
}
static __forceinline int RngRange(int lo, int hi)
{
    DWORD r = RngNext();
    int span = (hi - lo) + 1;
    if (span <= 0) return lo;
    return lo + (int)(r % (DWORD)span);
}

static const int STAR_COUNT = 96;
static int s_starX[STAR_COUNT];
static int s_starY[STAR_COUNT];
static int s_starSpd[STAR_COUNT];

// Sprite rendering (matching game.cpp)
static const SpritePack4* s_pack = &g_packClassic;
static bool s_secretMode = false;
static const int SPR_SCALE = 2;

// Animation support - one animator per invader type
static SpriteAnimator s_animInvaderA;
static SpriteAnimator s_animInvaderB;
static SpriteAnimator s_animInvaderC;

// Dual-layer clouds overlay (matching game.cpp)
static const char* kCloudsDDS = "D:\\tex\\cloud_256.dds";
static LPDIRECT3DTEXTURE8 s_clouds = NULL;
static int s_cloudsW = 0;
static int s_cloudsH = 0;
static float s_cloudU0 = 0.0f;
static float s_cloudV0 = 0.0f;
static float s_cloudU1 = 0.5f;
static float s_cloudV1 = 0.25f;

// Demo entities (very simple)
static int s_playerX = SCREEN_W / 2;
static const int s_playerY = SCREEN_H - 42;
static int s_playerDir = 1;      // -1/+1
static int s_playerCooldown = 0;

static bool s_bulletActive = false;
static int  s_bulletX = 0;
static int  s_bulletY = 0;

static const int EN_COLS = 11;
static const int EN_ROWS = 5;

struct Enemy
{
    bool alive;
    int x, y;
    int type; // 0=bottom, 1=mid, 2=top
};

static Enemy s_en[EN_ROWS][EN_COLS];
static int s_enDir = 1;
static int s_enStepTimer = 0;
static int s_enStepDelay = 18;
static int s_enDropPending = 0;

// Enemy bullets
static const int ENEMY_BUL_MAX = 8;
static bool s_ebActive[ENEMY_BUL_MAX];
static int s_ebX[ENEMY_BUL_MAX];
static int s_ebY[ENEMY_BUL_MAX];
static int s_ebW = 3;
static int s_ebH = 8;
static int s_enemyFireTimer = 0;

static int s_fakeScore = 0;

// Dodging AI state
static bool s_dodging = false;
static int s_dodgeTargetX = 0;
static int s_dodgeTimer = 0;

// ----------------------------------------------------------------------------
// Render state helpers
// ----------------------------------------------------------------------------
static void Prepare2D_NoTex()
{
    if (!g_pDevice) return;

    g_pDevice->SetTexture(0, NULL);

    g_pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    g_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);

    g_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

    g_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
    g_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    g_pDevice->SetVertexShader(FVF_2D);
}

static void Prepare2D_Tex()
{
    if (!g_pDevice) return;

    g_pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    g_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);

    g_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    g_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    g_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_pDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);

    g_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    g_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    g_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

    g_pDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    g_pDevice->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    g_pDevice->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);

    g_pDevice->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
    g_pDevice->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

    g_pDevice->SetVertexShader(FVF_2DTEX);
}

static void DrawRect(int x, int y, int w, int h, DWORD color)
{
    if (!g_pDevice) return;
    if (w <= 0 || h <= 0) return;

    const float x0 = (float)x;
    const float y0 = (float)y;
    const float x1 = (float)(x + w);
    const float y1 = (float)(y + h);

    V2D v[4];
    v[0].x = x0; v[0].y = y0; v[0].z = 0.0f; v[0].rhw = 1.0f; v[0].color = color;
    v[1].x = x1; v[1].y = y0; v[1].z = 0.0f; v[1].rhw = 1.0f; v[1].color = color;
    v[2].x = x0; v[2].y = y1; v[2].z = 0.0f; v[2].rhw = 1.0f; v[2].color = color;
    v[3].x = x1; v[3].y = y1; v[3].z = 0.0f; v[3].rhw = 1.0f; v[3].color = color;

    g_pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(V2D));
}

static void DrawCenteredText(const char* s, int y, float scale, DWORD color)
{
    if (!s || !s[0]) return;
    float w = (float)strlen(s) * (6.0f * scale);
    float x = ((float)SCREEN_W - w) * 0.5f;
    DrawText(x, (float)y, s, scale, color);
}

static void AppendInt(char* dst, int bufsz, int v)
{
    char tmp[16];
    int n = 0;

    if (v < 0)
    {
        int len = (int)strlen(dst);
        if (len < (bufsz - 1))
        {
            dst[len++] = '-';
            dst[len] = 0;
        }
        v = -v;
    }

    if (v == 0)
    {
        int len = (int)strlen(dst);
        if (len < (bufsz - 1))
        {
            dst[len++] = '0';
            dst[len] = 0;
        }
        return;
    }

    while (v > 0 && n < (int)sizeof(tmp))
    {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }

    int len = (int)strlen(dst);
    while (n > 0 && len < (bufsz - 1))
        dst[len++] = tmp[--n];

    dst[len] = 0;
}

static void AppendStr(char* dst, int bufsz, const char* s)
{
    int len = (int)strlen(dst);
    while (*s && len < (bufsz - 1))
        dst[len++] = *s++;
    dst[len] = 0;
}

// ----------------------------------------------------------------------------
// Dual-layer cloud rendering (matching game.cpp's dust/nebula effect)
// ----------------------------------------------------------------------------
static void DrawCloudLayer(LPDIRECT3DTEXTURE8 tex, int texW, int texH,
    float uOff, float vOff, int alpha, bool additive)
{
    if (!tex || texW <= 0 || texH <= 0) return;

    // Tile the 256x256 texture so it seamlessly covers 640x480 screen
    const int tilesX = ((SCREEN_W + texW - 1) / texW) + 1;
    const int tilesY = ((SCREEN_H + texH - 1) / texH) + 1;

    const float invW = 1.0f / (float)texW;
    const float invH = 1.0f / (float)texH;

    DWORD color;
    if (additive)
    {
        // Brighter, additive layer
        color = D3DCOLOR_ARGB(alpha, 255, 255, 255);
        g_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
    }
    else
    {
        // Base layer, normal blend
        color = D3DCOLOR_ARGB(alpha, 255, 255, 255);
        g_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    }

    for (int ty = 0; ty < tilesY; ++ty)
    {
        for (int tx = 0; tx < tilesX; ++tx)
        {
            float sx = (float)(tx * texW);
            float sy = (float)(ty * texH);
            float ex = sx + (float)texW;
            float ey = sy + (float)texH;

            float u0 = (sx * invW) + uOff;
            float v0 = (sy * invH) + vOff;
            float u1 = (ex * invW) + uOff;
            float v1 = (ey * invH) + vOff;

            V2DTex v[4];
            v[0].x = sx; v[0].y = sy; v[0].z = 0.0f; v[0].rhw = 1.0f; v[0].color = color; v[0].u = u0; v[0].v = v0;
            v[1].x = ex; v[1].y = sy; v[1].z = 0.0f; v[1].rhw = 1.0f; v[1].color = color; v[1].u = u1; v[1].v = v0;
            v[2].x = sx; v[2].y = ey; v[2].z = 0.0f; v[2].rhw = 1.0f; v[2].color = color; v[2].u = u0; v[2].v = v1;
            v[3].x = ex; v[3].y = ey; v[3].z = 0.0f; v[3].rhw = 1.0f; v[3].color = color; v[3].u = u1; v[3].v = v1;

            g_pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(V2DTex));
        }
    }
}

// ------------------------------
// Sprite renderer (4bpp packed, paletted) - matching game.cpp
// ------------------------------
static __forceinline uint8_t GetSpriteIndexAt(const Sprite4& spr, int x, int y)
{
    int w = (int)spr.w;
    int idx = y * w + x;
    int byteIndex = idx >> 1;
    uint8_t b = spr.data[byteIndex];
    if ((idx & 1) == 0) return (uint8_t)(b >> 4);
    return (uint8_t)(b & 0x0F);
}

static void DrawSprite4(const SpritePack4* pack, SpriteId id, int x, int y, int scale)
{
    if (!pack) return;
    if ((uint32_t)id >= pack->spriteCount) return;

    const Sprite4& spr = pack->sprites[(uint32_t)id];
    if (!spr.data || spr.w == 0 || spr.h == 0) return;

    for (int yy = 0; yy < (int)spr.h; ++yy)
    {
        for (int xx = 0; xx < (int)spr.w; ++xx)
        {
            uint8_t pi = GetSpriteIndexAt(spr, xx, yy);
            if (pi == 0) continue;

            DWORD col = (DWORD)pack->paletteARGB[pi];
            DrawRect(x + xx * scale, y + yy * scale, scale, scale, col);
        }
    }
}

// ----------------------------------------------------------------------------
// Stars
// ----------------------------------------------------------------------------
static void ResetStars()
{
    for (int i = 0; i < STAR_COUNT; ++i)
    {
        s_starX[i] = RngRange(0, SCREEN_W - 1);
        s_starY[i] = RngRange(0, SCREEN_H - 1);
        s_starSpd[i] = 1 + (int)(RngNext() % 3);
    }
}

// ----------------------------------------------------------------------------
// Enemies
// ----------------------------------------------------------------------------
static void ResetEnemies()
{
    const int startX = 92;
    const int startY = 80;
    const int cellX = 40;
    const int cellY = 26;

    for (int r = 0; r < EN_ROWS; ++r)
    {
        for (int c = 0; c < EN_COLS; ++c)
        {
            s_en[r][c].alive = true;
            s_en[r][c].x = startX + c * cellX;
            s_en[r][c].y = startY + r * cellY;

            // Assign types (matching game.cpp)
            if (r == 0) s_en[r][c].type = 2;
            else if (r <= 2) s_en[r][c].type = 1;
            else s_en[r][c].type = 0;
        }
    }

    s_enDir = 1;
    s_enStepDelay = 18;
    s_enStepTimer = s_enStepDelay;
    s_enDropPending = 0;
}

static void UpdateEnemies()
{
    if (s_enStepTimer > 0) s_enStepTimer--;
    if (s_enStepTimer > 0) return;
    s_enStepTimer = s_enStepDelay;

    // bounds
    int minX = 9999, maxX = -9999;
    for (int r = 0; r < EN_ROWS; ++r)
    {
        for (int c = 0; c < EN_COLS; ++c)
        {
            if (!s_en[r][c].alive) continue;
            int x = s_en[r][c].x;
            if (x < minX) minX = x;
            if (x > maxX) maxX = x;
        }
    }
    if (minX == 9999) // none alive -> reset
    {
        ResetEnemies();
        return;
    }

    const int leftLimit = 22;
    const int rightLimit = SCREEN_W - 22;

    if (s_enDropPending)
    {
        s_enDropPending = 0;
        for (int r = 0; r < EN_ROWS; ++r)
            for (int c = 0; c < EN_COLS; ++c)
                if (s_en[r][c].alive) s_en[r][c].y += 12;
        return;
    }

    int step = s_enDir * 2;
    if (s_enDir < 0 && (minX + step) < leftLimit)
    {
        s_enDir = 1;
        s_enDropPending = 1;
        return;
    }
    if (s_enDir > 0 && (maxX + step) > rightLimit)
    {
        s_enDir = -1;
        s_enDropPending = 1;
        return;
    }

    for (int r = 0; r < EN_ROWS; ++r)
        for (int c = 0; c < EN_COLS; ++c)
            if (s_en[r][c].alive) s_en[r][c].x += step;
}

// ----------------------------------------------------------------------------
// Enemy firing logic
// ----------------------------------------------------------------------------
static void TryEnemyFire()
{
    // Find a free bullet slot
    int slot = -1;
    for (int i = 0; i < ENEMY_BUL_MAX; ++i)
    {
        if (!s_ebActive[i])
        {
            slot = i;
            break;
        }
    }
    if (slot < 0) return;

    // Pick a random alive enemy from bottom rows (higher chance for lower rows)
    int shooter = -1;
    int shooterX = 0;
    int shooterY = 0;

    for (int attempt = 0; attempt < 10; ++attempt)
    {
        int r = RngRange(2, EN_ROWS - 1); // bottom 3 rows
        int c = RngRange(0, EN_COLS - 1);
        if (s_en[r][c].alive)
        {
            shooter = r * EN_COLS + c;
            shooterX = s_en[r][c].x + 11; // center of enemy
            shooterY = s_en[r][c].y + 14; // bottom of enemy
            break;
        }
    }

    if (shooter < 0) return;

    // Fire bullet
    s_ebActive[slot] = true;
    s_ebX[slot] = shooterX;
    s_ebY[slot] = shooterY;
}

// ----------------------------------------------------------------------------
// Dodging AI - evaluates threats and moves player away
// ----------------------------------------------------------------------------
static bool IsIncomingThreat(int* outBulletX)
{
    // Check if any enemy bullet is above player and within horizontal threat range
    const int threatRange = 60; // horizontal distance to consider a bullet dangerous
    int closestDist = 9999;
    int closestX = 0;

    for (int i = 0; i < ENEMY_BUL_MAX; ++i)
    {
        if (!s_ebActive[i]) continue;
        if (s_ebY[i] >= s_playerY) continue; // only care about bullets above us

        int dx = s_ebX[i] - s_playerX;
        if (dx < 0) dx = -dx;

        if (dx < threatRange)
        {
            int dist = s_playerY - s_ebY[i];
            if (dist < closestDist)
            {
                closestDist = dist;
                closestX = s_ebX[i];
            }
        }
    }

    if (closestDist < 9999)
    {
        *outBulletX = closestX;
        return true;
    }

    return false;
}

static void UpdatePlayer()
{
    const int speed = 2;
    const int minX = 40;
    const int maxX = SCREEN_W - 40;

    // Check for incoming threats
    int threatX = 0;
    if (IsIncomingThreat(&threatX))
    {
        if (!s_dodging)
        {
            // Start dodging - move away from bullet
            s_dodging = true;
            s_dodgeTimer = 30; // dodge for half a second

            // Decide which direction is safer
            int leftDist = s_playerX - minX;
            int rightDist = maxX - s_playerX;

            if (threatX < s_playerX)
            {
                // Bullet is on our left, prefer moving right
                s_dodgeTargetX = s_playerX + 80;
            }
            else
            {
                // Bullet is on our right, prefer moving left
                s_dodgeTargetX = s_playerX - 80;
            }

            // Clamp target
            if (s_dodgeTargetX < minX) s_dodgeTargetX = minX;
            if (s_dodgeTargetX > maxX) s_dodgeTargetX = maxX;
        }
    }

    if (s_dodging)
    {
        // Move toward dodge target
        if (s_playerX < s_dodgeTargetX)
        {
            s_playerX += speed + 1; // dodge faster than normal
            if (s_playerX > s_dodgeTargetX) s_playerX = s_dodgeTargetX;
        }
        else if (s_playerX > s_dodgeTargetX)
        {
            s_playerX -= speed + 1;
            if (s_playerX < s_dodgeTargetX) s_playerX = s_dodgeTargetX;
        }

        s_dodgeTimer--;
        if (s_dodgeTimer <= 0 || s_playerX == s_dodgeTargetX)
        {
            s_dodging = false;
            // Resume normal patrol - reverse direction if we hit a boundary
            if (s_playerX <= minX) s_playerDir = 1;
            else if (s_playerX >= maxX) s_playerDir = -1;
        }
    }
    else
    {
        // Normal autopilot: bounce between bounds
        s_playerX += s_playerDir * speed;
        if (s_playerX < minX)
        {
            s_playerX = minX;
            s_playerDir = 1;
        }
        if (s_playerX > maxX)
        {
            s_playerX = maxX;
            s_playerDir = -1;
        }
    }

    // Firing logic
    if (s_playerCooldown > 0) s_playerCooldown--;

    // fire every ~24..52 frames if no bullet
    if (!s_bulletActive && s_playerCooldown == 0)
    {
        int fireWait = 24 + (int)(RngNext() % 28);
        if ((s_frame % fireWait) == 0)
        {
            s_bulletActive = true;
            s_bulletX = s_playerX;
            s_bulletY = s_playerY - 12;
            s_playerCooldown = 10;
        }
    }
}

// ----------------------------------------------------------------------------
// Collision helpers
// ----------------------------------------------------------------------------
static bool BulletHitsEnemy(int bx, int by)
{
    // simple collision: check vs any alive enemy rect
    for (int r = 0; r < EN_ROWS; ++r)
    {
        for (int c = 0; c < EN_COLS; ++c)
        {
            if (!s_en[r][c].alive) continue;

            int ex = s_en[r][c].x;
            int ey = s_en[r][c].y;
            int ew = 22;
            int eh = 14;

            if (bx >= ex && bx < (ex + ew) && by >= ey && by < (ey + eh))
            {
                s_en[r][c].alive = false;
                return true;
            }
        }
    }
    return false;
}

// ----------------------------------------------------------------------------
// Update logic
// ----------------------------------------------------------------------------
static void UpdateBullet()
{
    if (!s_bulletActive) return;

    s_bulletY -= 6;
    if (s_bulletY < -20)
    {
        s_bulletActive = false;
        return;
    }

    // hit check (use bullet tip)
    if (BulletHitsEnemy(s_bulletX, s_bulletY))
    {
        s_bulletActive = false;
        s_fakeScore += 10;
    }
}

static void UpdateEnemyBullets()
{
    for (int i = 0; i < ENEMY_BUL_MAX; ++i)
    {
        if (!s_ebActive[i]) continue;

        s_ebY[i] += 4;

        if (s_ebY[i] > SCREEN_H + 40)
        {
            s_ebActive[i] = false;
            continue;
        }
    }
}

static void UpdateStars()
{
    for (int i = 0; i < STAR_COUNT; ++i)
    {
        s_starY[i] += s_starSpd[i];
        if (s_starY[i] >= SCREEN_H)
        {
            s_starY[i] = 0;
            s_starX[i] = RngRange(0, SCREEN_W - 1);
            s_starSpd[i] = 1 + (int)(RngNext() % 3);
        }
    }
}

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------
void Attract_Init(bool secretMode)
{
    s_secret = secretMode;
    s_secretMode = Title_IsSecret() ? true : false;
    s_pack = s_secretMode ? &g_packSecret : &g_packClassic;

    s_running = true;

    s_prevButtons = 0;
    s_frame = 0;

    s_demoFramesLeft = kDemoSeconds * kFPS;

    s_rng = secretMode ? 0xDEADC0DEu : 0xC0FFEE01u;

    s_fakeScore = 0;
    s_playerX = SCREEN_W / 2;
    s_playerDir = 1;
    s_playerCooldown = 0;
    s_bulletActive = false;

    // Enemy bullets
    for (int i = 0; i < ENEMY_BUL_MAX; ++i)
    {
        s_ebActive[i] = false;
    }
    s_enemyFireTimer = 40;

    // Dodging AI
    s_dodging = false;
    s_dodgeTargetX = 0;
    s_dodgeTimer = 0;

    // Initialize sprite animations
    if (s_pack->animations && s_pack->animCount >= 3)
    {
        s_animInvaderA.Play(&s_pack->animations[ANIM_INVADER_A]);
        s_animInvaderB.Play(&s_pack->animations[ANIM_INVADER_B]);
        s_animInvaderC.Play(&s_pack->animations[ANIM_INVADER_C]);
    }

    ResetStars();
    ResetEnemies();

    // Dual-layer clouds
    if (s_clouds) { s_clouds->Release(); s_clouds = NULL; }
    s_cloudsW = s_cloudsH = 0;
    s_cloudU0 = 0.0f;
    s_cloudV0 = 0.0f;
    s_cloudU1 = 0.5f;
    s_cloudV1 = 0.25f;

    if (g_pDevice)
        s_clouds = LoadTextureFromDDS_Rect(kCloudsDDS, s_cloudsW, s_cloudsH);
}

void Attract_Shutdown()
{
    if (s_clouds) { s_clouds->Release(); s_clouds = NULL; }
    s_cloudsW = s_cloudsH = 0;

    s_running = false;
}

bool Attract_Update()
{
    if (!s_running) return false;

    s_frame++;

    // Exit conditions:
    // - any input press (START/A/B/dpad)
    // - timer ends
    WORD now = GetButtons();
    WORD changed = (WORD)(now & (WORD)~s_prevButtons);

    if (changed & (BTN_START | BTN_A | BTN_B | BTN_DPAD_LEFT | BTN_DPAD_RIGHT | BTN_DPAD_UP | BTN_DPAD_DOWN))
        return false;

    s_prevButtons = now;

    if (s_demoFramesLeft > 0) s_demoFramesLeft--;
    if (s_demoFramesLeft <= 0)
        return false;

    UpdateStars();
    UpdateEnemies();
    UpdatePlayer();
    UpdateBullet();
    UpdateEnemyBullets();

    // Update sprite animations (assuming 60 FPS, each frame is ~16.67ms)
    const uint32_t deltaMs = 17; // approximately 1000ms / 60fps
    s_animInvaderA.Update(deltaMs);
    s_animInvaderB.Update(deltaMs);
    s_animInvaderC.Update(deltaMs);

    // Enemy firing
    if (s_enemyFireTimer > 0) s_enemyFireTimer--;
    if (s_enemyFireTimer <= 0)
    {
        TryEnemyFire();
        s_enemyFireTimer = 40 + (int)(RngNext() % 60); // Fire every 0.66-1.66 seconds
    }

    // Update dual cloud layers (matching game.cpp speeds)
    const float invTexW = s_cloudsW > 0 ? (1.0f / (float)s_cloudsW) : 0.0f;
    const float invTexH = s_cloudsH > 0 ? (1.0f / (float)s_cloudsH) : 0.0f;

    s_cloudU0 += 0.15f * invTexW;
    s_cloudV0 += 0.10f * invTexH;

    s_cloudU1 -= 0.20f * invTexW;
    s_cloudV1 += 0.15f * invTexH;

    return true;
}

void Attract_Render()
{
    if (!g_pDevice) return;

    // ------------------------------------------------------------
    // Last 5 seconds: keep background (stars + clouds), but replace
    // playfield with HIGH SCORES overlay.
    // ------------------------------------------------------------
    if (s_demoFramesLeft <= (5 * kFPS))
    {
        // Background base
        Prepare2D_NoTex();

        // Stars (same as normal render)
        for (int i = 0; i < STAR_COUNT; ++i)
        {
            DWORD colStar = s_secret ? D3DCOLOR_XRGB(120, 120, 160) : D3DCOLOR_XRGB(120, 120, 120);
            if (s_starSpd[i] == 3) colStar = s_secret ? D3DCOLOR_XRGB(180, 180, 220) : D3DCOLOR_XRGB(200, 200, 200);
            DrawRect(s_starX[i], s_starY[i], 1, 1, colStar);
        }

        // Clouds overlay (same as normal render)
        if (s_clouds)
        {
            Prepare2D_Tex();
            g_pDevice->SetTexture(0, s_clouds);
            DrawCloudLayer(s_clouds, s_cloudsW, s_cloudsH, s_cloudU0, s_cloudV0, 30, false);
            DrawCloudLayer(s_clouds, s_cloudsW, s_cloudsH, s_cloudU1, s_cloudV1, 18, true);
        }

        // Font state (same as your HUD section)
        g_pDevice->SetTexture(0, NULL);
        g_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        g_pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        g_pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
        g_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        g_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
        g_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
        g_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        g_pDevice->SetVertexShader(FVF_2D);

        // Fun neon-ish color cycle
        DWORD col = D3DCOLOR_XRGB(255, 0, 255); // magenta
        switch ((s_frame / 10) & 3)
        {
        default:
        case 0: col = D3DCOLOR_XRGB(255, 0, 255); break;   // magenta
        case 1: col = D3DCOLOR_XRGB(0, 255, 255); break;   // cyan
        case 2: col = D3DCOLOR_XRGB(255, 210, 0); break;   // amber
        case 3: col = D3DCOLOR_XRGB(255, 255, 255); break; // white
        }

        DrawCenteredText("HIGH SCORES", 48, 3.0f, col);

        // Table overlay (no playfield drawn)
        ScoreHS_RenderTable(180.0f, 110.0f, 2.0f, col);

        DrawCenteredText("PRESS START", SCREEN_H - 44, 2.0f, D3DCOLOR_XRGB(255, 255, 255));
        return;
    }


    // Background
    Prepare2D_NoTex();

    // Stars
    for (int i = 0; i < STAR_COUNT; ++i)
    {
        DWORD col = s_secret ? D3DCOLOR_XRGB(120, 120, 160) : D3DCOLOR_XRGB(120, 120, 120);
        if (s_starSpd[i] == 3) col = s_secret ? D3DCOLOR_XRGB(180, 180, 220) : D3DCOLOR_XRGB(200, 200, 200);
        DrawRect(s_starX[i], s_starY[i], 1, 1, col);
    }

    // Dual-layer dust/nebula overlay (matching game.cpp)
    if (s_clouds)
    {
        Prepare2D_Tex();
        g_pDevice->SetTexture(0, s_clouds);

        // First layer - slower, dimmer
        DrawCloudLayer(s_clouds, s_cloudsW, s_cloudsH, s_cloudU0, s_cloudV0, 30, false);

        // Second layer - faster, additive
        DrawCloudLayer(s_clouds, s_cloudsW, s_cloudsH, s_cloudU1, s_cloudV1, 18, true);
    }

    // Back to non-textured for gameplay elements
    Prepare2D_NoTex();

    // Ground line
    DWORD ground = s_secret ? D3DCOLOR_XRGB(120, 255, 120) : D3DCOLOR_XRGB(80, 255, 80);
    DrawRect(0, SCREEN_H - 60, SCREEN_W, 1, ground);

    // Enemies (sprites with animation)
    for (int r = 0; r < EN_ROWS; ++r)
    {
        for (int c = 0; c < EN_COLS; ++c)
        {
            const Enemy& e = s_en[r][c];
            if (!e.alive) continue;

            // Get animated sprite based on enemy type
            SpriteId sid = SPR_INVADER_C;
            if (e.type == 2)
                sid = s_animInvaderA.GetCurrentSprite();
            else if (e.type == 1)
                sid = s_animInvaderB.GetCurrentSprite();
            else
                sid = s_animInvaderC.GetCurrentSprite();

            DrawSprite4(s_pack, sid, e.x, e.y, SPR_SCALE);
        }
    }

    // Player (sprite)
    {
        int px = s_playerX - 13;
        DrawSprite4(s_pack, SPR_PLAYER, px, s_playerY, SPR_SCALE);
    }

    // Bullet (sprite)
    if (s_bulletActive)
    {
        DrawSprite4(s_pack, SPR_PLAYER_BULLET, s_bulletX - 1, s_bulletY, SPR_SCALE);
    }

    // Enemy bullets (sprites)
    for (int i = 0; i < ENEMY_BUL_MAX; ++i)
    {
        if (!s_ebActive[i]) continue;

        SpriteId bid = SPR_EBULLET_ZIG;
        if ((i % 3) == 1) bid = SPR_EBULLET_PLUNGER;
        else if ((i % 3) == 2) bid = SPR_EBULLET_ROLL;

        DrawSprite4(s_pack, bid, s_ebX[i] - 1, s_ebY[i], SPR_SCALE);
    }

    // HUD text (font requires caller state)
    g_pDevice->SetTexture(0, NULL);
    g_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    g_pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    g_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
    g_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    g_pDevice->SetVertexShader(FVF_2D);

    // "DEMO" label
    DrawCenteredText("DEMO PLAY", 24, 2.5f, s_secret ? D3DCOLOR_XRGB(255, 210, 0) : D3DCOLOR_XRGB(255, 255, 255));

    // fake score top-left
    char line[64];
    line[0] = 0;
    AppendStr(line, (int)sizeof(line), "SCORE ");
    AppendInt(line, (int)sizeof(line), s_fakeScore);
    DrawText(24.0f, 8.0f, line, 2.0f, D3DCOLOR_XRGB(255, 255, 255));

    // exit hint
    DrawCenteredText("PRESS START", SCREEN_H - 44, 2.0f, D3DCOLOR_XRGB(255, 255, 255));
}