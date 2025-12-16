// game.cpp
#include "game.h"

#include <xtl.h>
#include <xgraphics.h>
#include <string.h>
#include <stdlib.h>

#include "input.h"
#include "font.h"
#include "music.h"
#include "title.h"            // Title_IsSecret()
#include "sprites.h"
#include "sprites_classic.h"
#include "sprites_secret.h"
#include "SpriteAnimator.h"
#include "score.h"            // High score table + render

// Device provided by main.cpp
extern LPDIRECT3DDEVICE8 g_pDevice;

static const int SCREEN_W = 640;
static const int SCREEN_H = 480;

// ------------------------------
// Game music (your filenames)
// ------------------------------
static const char* kGameTrm_Normal = "D:\\snd\\gamea.trm";
static const char* kGameTrm_Secret = "D:\\snd\\gameb.trm";

// ------------------------------
// SFX slots (your filenames)
// ------------------------------
enum
{
    SFX_ENEMY_DEATH = 0,  // death.wav
    SFX_SHOOT = 1,        // shoot.wav
    SFX_HIT = 2,          // hit.wav
    SFX_PLAYER_DEAD = 3,  // pdeath.wav
    SFX_1UP = 4,          // life.wav
    SFX_UFO = 5,          // ufo.wav
};

static const char* kSfxPath_EnemyDeath = "D:\\snd\\death.wav";
static const char* kSfxPath_Shoot = "D:\\snd\\shoot.wav";
static const char* kSfxPath_Hit = "D:\\snd\\hit.wav";
static const char* kSfxPath_PlayerDead = "D:\\snd\\pdeath.wav";
static const char* kSfxPath_1Up = "D:\\snd\\life.wav";
static const char* kSfxPath_Ufo = "D:\\snd\\ufo.wav";

// ------------------------------
// Background assets
// ------------------------------
static const char* kCloudsDDS = "D:\\tex\\cloud_256.dds";

// ------------------------------
// Render helpers (2D)
// ------------------------------
struct V2D
{
    float x, y, z, rhw;
    DWORD color;
};

#define FVF_2D (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

struct V2DTex
{
    float x, y, z, rhw;
    DWORD color;
    float u, v;
};

#define FVF_2D_TEX (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)

// ------------------------------
// Tiny RNG (integer-only)
// ------------------------------
static DWORD s_rng = 0x12345678;
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

// ------------------------------
// Tiny text formatting helpers (no sprintf / no stdio)
// ------------------------------
static __forceinline void StrCpy(char* dst, const char* src)
{
    while (*src) { *dst++ = *src++; }
    *dst = 0;
}

static __forceinline char* StrEnd(char* s)
{
    while (*s) ++s;
    return s;
}

static char* AppendInt(char* dst, int v)
{
    char tmp[16];
    int n = 0;

    if (v < 0)
    {
        *dst++ = '-';
        v = -v;
    }

    if (v == 0)
    {
        *dst++ = '0';
        *dst = 0;
        return dst;
    }

    while (v > 0 && n < (int)sizeof(tmp))
    {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }

    while (n > 0)
        *dst++ = tmp[--n];

    *dst = 0;
    return dst;
}

static void MakeLabelInt(char* out, const char* label, int value)
{
    StrCpy(out, label);
    char* p = StrEnd(out);
    AppendInt(p, value);
}

// ------------------------------
// D3D state helpers
// ------------------------------
static void Prepare2D()
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

static void Prepare2DTextured(bool additive, bool useTextureAlpha, DWORD tf0 = D3DTEXF_POINT)
{
    if (!g_pDevice) return;

    g_pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    g_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);

    g_pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

    g_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pDevice->SetRenderState(D3DRS_DESTBLEND, additive ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);

    g_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_pDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);

    if (useTextureAlpha)
    {
        g_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        g_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        g_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    }
    else
    {
        g_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
        g_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    }

    g_pDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, tf0);
    g_pDevice->SetTextureStageState(0, D3DTSS_MINFILTER, tf0);
    g_pDevice->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);

    g_pDevice->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
    g_pDevice->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);

    g_pDevice->SetVertexShader(FVF_2D_TEX);
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

static void DrawHLine(int x, int y, int w, DWORD color)
{
    DrawRect(x, y, w, 1, color);
}

static void DrawCenteredText(const char* s, int y, float scale, DWORD color)
{
    if (!s || !s[0]) return;

    float w = (float)strlen(s) * (6.0f * scale);
    float x = ((float)SCREEN_W - w) * 0.5f;
    DrawText(x, (float)y, s, scale, color);
}

// ------------------------------
// Sprite renderer (4bpp packed, paletted)
// ------------------------------
static const SpritePack4* s_pack = &g_packClassic;
static bool s_secretMode = false;

// Animation support - one animator per invader type
static SpriteAnimator s_animInvaderA;
static SpriteAnimator s_animInvaderB;
static SpriteAnimator s_animInvaderC;

// pixel scale for the whole game look (2 = "chunky arcade")
static const int SPR_SCALE = 2;

static __forceinline uint8_t GetSpriteIndexAt(const Sprite4& spr, int x, int y)
{
    // packed: high nibble = left pixel, low nibble = right pixel
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

    // NOTE: This is intentionally simple (DrawRect per solid pixel).
    // Sprites are tiny, so it's fine and matches the "chunky" look.
    for (int yy = 0; yy < (int)spr.h; ++yy)
    {
        for (int xx = 0; xx < (int)spr.w; ++xx)
        {
            uint8_t pi = GetSpriteIndexAt(spr, xx, yy);
            if (pi == 0) continue; // transparent

            DWORD col = (DWORD)pack->paletteARGB[pi];
            DrawRect(x + xx * scale, y + yy * scale, scale, scale, col);
        }
    }
}

// ------------------------------
// DDS loader (swizzled A8R8G8B8) for clouds overlay
// ------------------------------
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
    outW = 0;
    outH = 0;

    if (!g_pDevice || !path || !path[0])
        return NULL;

    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
        return NULL;

    DWORD bytesRead = 0;

    DWORD magic = 0;
    if (!ReadFile(hFile, &magic, sizeof(DWORD), &bytesRead, NULL) ||
        bytesRead != sizeof(DWORD) ||
        magic != 0x20534444)
    {
        CloseHandle(hFile);
        return NULL;
    }

    DDS_HEADER hdr;
    if (!ReadFile(hFile, &hdr, sizeof(DDS_HEADER), &bytesRead, NULL) ||
        bytesRead != sizeof(DDS_HEADER))
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

    if (!ReadFile(hFile, pixels, pixelBytes, &bytesRead, NULL) ||
        bytesRead != pixelBytes)
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

// ------------------------------
// Background: starfield + clouds overlay
// ------------------------------
static const int STAR_COUNT = 96;

static int  s_starX[STAR_COUNT];
static int  s_starY[STAR_COUNT];
static int  s_starSpd[STAR_COUNT];   // 1..3
static BYTE s_starB[STAR_COUNT];     // brightness

static LPDIRECT3DTEXTURE8 s_texClouds = NULL;
static int  s_cloudW = 0;
static int  s_cloudH = 0;

// UV scroll accumulators
static int s_cloudU0 = 0;
static int s_cloudV0 = 0;
static int s_cloudU1 = 0;
static int s_cloudV1 = 0;

static void Background_Init()
{
    for (int i = 0; i < STAR_COUNT; ++i)
    {
        s_starX[i] = RngRange(0, SCREEN_W - 1);
        s_starY[i] = RngRange(0, SCREEN_H - 1);
        s_starSpd[i] = RngRange(1, 3);

        int b = RngRange(90, 220);
        s_starB[i] = (BYTE)b;
    }

    if (s_texClouds) { s_texClouds->Release(); s_texClouds = NULL; }
    s_cloudW = 0;
    s_cloudH = 0;

    s_texClouds = LoadTextureFromDDS_Rect(kCloudsDDS, s_cloudW, s_cloudH);

    s_cloudU0 = 0;
    s_cloudV0 = 0;
    s_cloudU1 = 128;
    s_cloudV1 = 64;
}

static void Background_Shutdown()
{
    if (s_texClouds) { s_texClouds->Release(); s_texClouds = NULL; }
    s_cloudW = s_cloudH = 0;
}

static void Background_Update()
{
    for (int i = 0; i < STAR_COUNT; ++i)
    {
        s_starY[i] += s_starSpd[i];
        if (s_starY[i] >= SCREEN_H)
        {
            s_starY[i] -= SCREEN_H;
            s_starX[i] = RngRange(0, SCREEN_W - 1);
            if ((RngNext() & 7) == 0) s_starSpd[i] = RngRange(1, 3);
        }
    }

    s_cloudU0 += (1 << 13);
    s_cloudV0 += (1 << 12);
    s_cloudU1 -= (1 << 12);
    s_cloudV1 += (1 << 13);
}

static void RenderStars()
{
    // Local anim counter so we don't depend on any external s_frame.
    static int s_starAnim = 0;
    s_starAnim++;

    for (int i = 0; i < STAR_COUNT; ++i)
    {
        int bb = (int)s_starB[i];

        // tiny twinkle on a subset (no RNG)
        if ((i & 7) == 0)
        {
            int t = (s_starAnim + i * 13) & 31;    // 0..31
            int wobble = (t < 16) ? t : (31 - t);  // 0..15 triangle wave
            bb += wobble * 2;
            if (bb > 255) bb = 255;
        }

        BYTE b = (BYTE)bb;

        DrawRect(s_starX[i], s_starY[i], 1, 1, D3DCOLOR_XRGB(b, b, b));

        if ((i & 15) == 0)
        {
            int x = s_starX[i];
            int y = s_starY[i];
            DrawRect(x, y, 2, 1, D3DCOLOR_XRGB(b, b, b));
        }

        // "dust" stars (bigger points)
        if ((i & 23) == 0)
        {
            int x = s_starX[i] - 1;
            int y = s_starY[i] - 1;
            DrawRect(x, y, 3, 3, D3DCOLOR_XRGB(b, b, b));
        }
    }
}

static void DrawCloudLayer(LPDIRECT3DTEXTURE8 tex, int tw, int th, int uFix, int vFix, BYTE alpha, bool additive)
{
    if (!g_pDevice || !tex || tw <= 0 || th <= 0) return;

    float u0 = (float)uFix / (float)(tw << 16);
    float v0 = (float)vFix / (float)(th << 16);

    float uSpan = (float)SCREEN_W / (float)tw;
    float vSpan = (float)SCREEN_H / (float)th;

    float u1 = u0 + uSpan;
    float v1 = v0 + vSpan;

    // Tint + stronger additive so the "dust/nebula" actually reads
    BYTE r = 255, g = 255, b = 255;

    if (additive)
    {
        r = 200; g = 120; b = 255;   // purple-ish highlight wisps
        if (alpha < 60) alpha = 60;
    }
    else
    {
        r = 80; g = 110; b = 255;    // bluish base haze
        if (alpha < 35) alpha = 35;
    }

    DWORD col = D3DCOLOR_ARGB(alpha, r, g, b);

    V2DTex v[4];
    v[0].x = 0.0f;            v[0].y = 0.0f;            v[0].z = 0.0f; v[0].rhw = 1.0f; v[0].color = col; v[0].u = u0; v[0].v = v0;
    v[1].x = (float)SCREEN_W; v[1].y = 0.0f;            v[1].z = 0.0f; v[1].rhw = 1.0f; v[1].color = col; v[1].u = u1; v[1].v = v0;
    v[2].x = 0.0f;            v[2].y = (float)SCREEN_H; v[2].z = 0.0f; v[2].rhw = 1.0f; v[2].color = col; v[2].u = u0; v[2].v = v1;
    v[3].x = (float)SCREEN_W; v[3].y = (float)SCREEN_H; v[3].z = 0.0f; v[3].rhw = 1.0f; v[3].color = col; v[3].u = u1; v[3].v = v1;

    Prepare2DTextured(additive, true, D3DTEXF_LINEAR);
    g_pDevice->SetTexture(0, tex);
    g_pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(V2DTex));

    Prepare2D();
}

// ------------------------------
// Game state
// ------------------------------
static bool s_running = false;
static bool s_gameOver = false;

static WORD s_prevButtons = GetButtonsAny();
static int  s_frame = 0;

static int s_score = 0;
static int s_lives = 3;
static int s_level = 1;

static int s_scoreFor1Up = 0;
static bool s_ufoActive = false;
static int  s_ufoX = 0;
static int  s_ufoDir = 1;
static int  s_ufoTimer = 0;

// Player
static int s_playerX = SCREEN_W / 2;
static int s_playerY = SCREEN_H - 42;
static int s_playerW = 26;
static int s_playerH = 10;
static int s_playerCooldown = 0;
static int s_playerDeadTimer = 0;

// Bullet (player)
static bool s_bulletActive = false;
static int  s_bulletX = 0;
static int  s_bulletY = 0;
static int  s_bulletW = 2;
static int  s_bulletH = 10;

// Enemy bullets
static const int ENEMY_BUL_MAX = 3;  // Classic Space Invaders had 3 max
static bool s_ebActive[ENEMY_BUL_MAX];
static int  s_ebX[ENEMY_BUL_MAX];
static int  s_ebY[ENEMY_BUL_MAX];
static int  s_ebW = 2;
static int  s_ebH = 8;

// Enemies
static const int EN_COLS = 11;
static const int EN_ROWS = 5;

struct Enemy
{
    bool alive;
    int  x, y;
    int  w, h;
    int  type; // 0..2 for scoring flavor
};

static Enemy s_en[EN_ROWS][EN_COLS];
static int s_enDir = 1;
static int s_enSpeed = 1;
static int s_enStepTimer = 0;
static int s_enStepDelay = 24;
static int s_enDropPending = 0;
static int s_enemyShotTimer = 0;



// Shields
static const int SHIELDS = 4;
static const int SHIELD_TILES_W = 6;
static const int SHIELD_TILES_H = 3;
static int s_shX[SHIELDS];
static int s_shY[SHIELDS];
static bool s_shTiles[SHIELDS][SHIELD_TILES_H][SHIELD_TILES_W];  // per-tile alive state

// UI
static bool s_showReady = true;
static int  s_readyTimer = 90;

// ------------------------------
// GAME OVER high score flow
// ------------------------------
static bool s_goQualifies = false;
static bool s_goEntryMode = false;
static bool s_goSubmitted = false;

static char s_goInitials[4] = { 'A','A','A',0 };
static int  s_goCursor = 0;

// ------------------------------
// Helpers
// ------------------------------
static __forceinline bool EdgePressed(WORD now, WORD prev, WORD bit)
{
    return ((now & bit) && !(prev & bit)) ? true : false;
}

static __forceinline void ClampCursor()
{
    if (s_goCursor < 0) s_goCursor = 0;
    if (s_goCursor > 2) s_goCursor = 2;
}

static __forceinline void IncLetter(int idx, int delta)
{
    char c = s_goInitials[idx];
    if (c < 'A' || c > 'Z') c = 'A';
    int v = (int)(c - 'A');
    v += delta;
    while (v < 0) v += 26;
    while (v >= 26) v -= 26;
    s_goInitials[idx] = (char)('A' + v);
}

static void BeginGameOverFlow()
{
    // Ensure table is loaded (main does it, but safe here too)
    ScoreHS_Init();

    s_goQualifies = ScoreHS_Qualifies(s_score);
    s_goEntryMode = s_goQualifies ? true : false;
    s_goSubmitted = false;

    s_goInitials[0] = 'A';
    s_goInitials[1] = 'A';
    s_goInitials[2] = 'A';
    s_goInitials[3] = 0;
    s_goCursor = 0;
}

static void ResetWave()
{
    // Use sprite sizes (scaled) if available
    int invW = 22;
    int invH = 14;

    if (s_pack)
    {
        const Sprite4& a = s_pack->sprites[SPR_INVADER_A];
        invW = (int)a.w * SPR_SCALE;
        invH = (int)a.h * SPR_SCALE;
    }

    // Formation layout (classic-ish)
    const int startX = 92;
    const int startY = 80;
    const int cellX = invW + 16;      // spacing
    const int cellY = invH + 10;

    for (int r = 0; r < EN_ROWS; ++r)
    {
        for (int c = 0; c < EN_COLS; ++c)
        {
            Enemy& e = s_en[r][c];
            e.alive = true;
            e.x = startX + c * cellX;
            e.y = startY + r * cellY;
            e.w = invW;
            e.h = invH;

            if (r == 0) e.type = 2;
            else if (r <= 2) e.type = 1;
            else e.type = 0;
        }
    }

    s_enDir = 1;
    s_enSpeed = 2;  // Classic Space Invaders speed

    // Progressive wave difficulty: each wave starts faster
    s_enStepDelay = 24 - (s_level - 1) * 2;
    if (s_enStepDelay < 8) s_enStepDelay = 8;  // Cap minimum at 8 frames

    s_enStepTimer = s_enStepDelay;
    s_enDropPending = 0;

    // Player dims from sprite
    if (s_pack)
    {
        const Sprite4& p = s_pack->sprites[SPR_PLAYER];
        s_playerW = (int)p.w * SPR_SCALE;
        s_playerH = (int)p.h * SPR_SCALE;
    }
    s_playerX = SCREEN_W / 2;
    s_playerCooldown = 0;
    s_playerDeadTimer = 0;

    // Bullet dims from sprite
    if (s_pack)
    {
        const Sprite4& pb = s_pack->sprites[SPR_PLAYER_BULLET];
        s_bulletW = (int)pb.w * SPR_SCALE;
        s_bulletH = (int)pb.h * SPR_SCALE;

        const Sprite4& eb = s_pack->sprites[SPR_EBULLET_ZIG];
        s_ebW = (int)eb.w * SPR_SCALE;
        s_ebH = (int)eb.h * SPR_SCALE;
    }

    s_bulletActive = false;
    for (int i = 0; i < ENEMY_BUL_MAX; ++i) s_ebActive[i] = false;

    // Shields (rendered as tiles)
    const int baseY = SCREEN_H - 120;
    for (int i = 0; i < SHIELDS; ++i)
    {
        s_shX[i] = 92 + i * 138;
        s_shY[i] = baseY;

        // Initialize all tiles as alive
        for (int ty = 0; ty < SHIELD_TILES_H; ++ty)
            for (int tx = 0; tx < SHIELD_TILES_W; ++tx)
                s_shTiles[i][ty][tx] = true;
    }

    // UFO
    s_ufoActive = false;
    s_ufoTimer = RngRange(240, 420);

    s_showReady = true;
    s_readyTimer = 75;
    s_enemyShotTimer = 90;
}

static int AliveEnemyCount()
{
    int n = 0;
    for (int r = 0; r < EN_ROWS; ++r)
        for (int c = 0; c < EN_COLS; ++c)
            if (s_en[r][c].alive) ++n;
    return n;
}

static void ScoreAdd(int pts)
{
    s_score += pts;
    s_scoreFor1Up += pts;

    if (s_scoreFor1Up >= 1500)
    {
        s_scoreFor1Up -= 1500;
        s_lives++;
        Sfx_Play(SFX_1UP, DSBVOLUME_MAX);
    }
}

static bool Aabb(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    if (ax + aw <= bx) return false;
    if (bx + bw <= ax) return false;
    if (ay + ah <= by) return false;
    if (by + bh <= ay) return false;
    return true;
}

static void KillPlayer()
{
    if (s_playerDeadTimer > 0) return;

    s_lives--;
    s_playerDeadTimer = 90;
    s_bulletActive = false;

    Sfx_Play(SFX_PLAYER_DEAD, DSBVOLUME_MAX);

    // GAME OVER when lives reach 0 (not -1)
    if (s_lives <= 0)
    {
        s_gameOver = true;
        BeginGameOverFlow();
    }
}

static __forceinline int EnemyShotDelayFromAlive(int alive)
{
    // 60fps-ish delays. Fewer invaders => faster firing (more pressure).
    if (alive > 40)      return 90;
    else if (alive > 25) return 70;
    else if (alive > 12) return 50;
    else                 return 32;
}

static __forceinline int PlayerColumn()
{
    // Map player X to [0..EN_COLS-1] without floats.
    // s_playerX is center X.
    int col = (s_playerX * EN_COLS) / SCREEN_W;
    if (col < 0) col = 0;
    if (col >= EN_COLS) col = EN_COLS - 1;
    return col;
}

static __forceinline int WrapCol(int c)
{
    while (c < 0) c += EN_COLS;
    while (c >= EN_COLS) c -= EN_COLS;
    return c;
}

static void EnemyShootTimed()
{
    // Don't shoot during READY or GAME OVER
    if (s_showReady || s_gameOver)
        return;

    int alive = AliveEnemyCount();
    if (alive <= 0)
        return;

    if (s_enemyShotTimer > 0)
    {
        s_enemyShotTimer--;
        return;
    }

    // Find free enemy bullet slot
    int slot = -1;
    for (int i = 0; i < ENEMY_BUL_MAX; ++i)
    {
        if (!s_ebActive[i]) { slot = i; break; }
    }
    if (slot < 0)
    {
        // No slot available; try again soon.
        s_enemyShotTimer = 8;
        return;
    }

    // Bias column toward player (more authentic feel than pure uniform RNG)
    // Try a few offsets around the player's column.
    static const int kTryOffs[] = { 0, 1, -1, 2, -2, 3, -3, 4, -4 };
    int base = PlayerColumn();

    // Shuffle starting point a bit (still deterministic)
    int start = (int)(RngNext() % (DWORD)(sizeof(kTryOffs) / sizeof(kTryOffs[0])));

    int chosenCol = -1;
    for (int t = 0; t < (int)(sizeof(kTryOffs) / sizeof(kTryOffs[0])); ++t)
    {
        int idx = start + t;
        if (idx >= (int)(sizeof(kTryOffs) / sizeof(kTryOffs[0]))) idx -= (int)(sizeof(kTryOffs) / sizeof(kTryOffs[0]));

        int col = WrapCol(base + kTryOffs[idx]);

        // Must have a living invader somewhere in this column
        for (int r = EN_ROWS - 1; r >= 0; --r)
        {
            if (s_en[r][col].alive)
            {
                chosenCol = col;
                goto FoundCol;
            }
        }
    }

FoundCol:
    if (chosenCol < 0)
    {
        // No valid shooter column found (should be rare)
        s_enemyShotTimer = 10;
        return;
    }

    // Shoot from the lowest alive enemy in that column
    for (int r = EN_ROWS - 1; r >= 0; --r)
    {
        if (!s_en[r][chosenCol].alive)
            continue;

        const Enemy& e = s_en[r][chosenCol];

        s_ebActive[slot] = true;
        s_ebX[slot] = e.x + (e.w / 2);
        s_ebY[slot] = e.y + e.h;

        break;
    }

    // Set next reload delay based on remaining invaders
    s_enemyShotTimer = EnemyShotDelayFromAlive(alive);
}

static void UpdateUfo()
{
    if (!s_ufoActive)
    {
        if (s_ufoTimer > 0) s_ufoTimer--;
        if (s_ufoTimer == 0)
        {
            s_ufoActive = true;
            s_ufoDir = (RngNext() & 1) ? 1 : -1;
            s_ufoX = (s_ufoDir > 0) ? -80 : (SCREEN_W + 80);
            Sfx_Play(SFX_UFO, -1200);
        }
        return;
    }

    s_ufoX += s_ufoDir * 2;

    if (s_ufoDir > 0 && s_ufoX > SCREEN_W + 80)
    {
        s_ufoActive = false;
        s_ufoTimer = RngRange(240, 520);
    }
    else if (s_ufoDir < 0 && s_ufoX < -80)
    {
        s_ufoActive = false;
        s_ufoTimer = RngRange(240, 520);
    }
}

static void UpdateEnemies()
{
    if (s_showReady || s_gameOver) return;

    int alive = AliveEnemyCount();
    if (alive <= 0)
    {
        s_level++;
        ResetWave();
        return;
    }

    // slower start, stronger ramp (60fps)
    if (alive < 6)       s_enStepDelay = 6;    // panic fast
    else if (alive < 12) s_enStepDelay = 10;
    else if (alive < 20) s_enStepDelay = 16;
    else if (alive < 30) s_enStepDelay = 24;
    else                 s_enStepDelay = 40;   // classic-ish slow march

    // IMPORTANT: let enemies attempt to shoot every frame,
    // not only on movement steps.
    EnemyShootTimed();

    if (s_enStepTimer > 0) s_enStepTimer--;
    if (s_enStepTimer > 0) return;
    s_enStepTimer = s_enStepDelay;

    int minX = 9999, maxX = -9999;
    int maxY = -9999;

    for (int r = 0; r < EN_ROWS; ++r)
    {
        for (int c = 0; c < EN_COLS; ++c)
        {
            const Enemy& e = s_en[r][c];
            if (!e.alive) continue;
            if (e.x < minX) minX = e.x;
            if (e.x + e.w > maxX) maxX = e.x + e.w;
            if (e.y + e.h > maxY) maxY = e.y + e.h;
        }
    }

    const int leftLimit = 22;
    const int rightLimit = SCREEN_W - 22;

    if (s_enDropPending > 0)
    {
        s_enDropPending = 0;
        for (int r = 0; r < EN_ROWS; ++r)
            for (int c = 0; c < EN_COLS; ++c)
                if (s_en[r][c].alive) s_en[r][c].y += 12;

        if (maxY >= (SCREEN_H - 140))
            KillPlayer();

        return;
    }

    int step = s_enDir * s_enSpeed;
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

static void UpdatePlayer(WORD now, WORD prev)
{
    if (s_gameOver) return;

    if (s_playerDeadTimer > 0)
    {
        s_playerDeadTimer--;
        if (s_playerDeadTimer == 0 && !s_gameOver)
        {
            s_playerX = SCREEN_W / 2;
            s_showReady = true;
            s_readyTimer = 60;
            s_enemyShotTimer = 90;
        }
        return;
    }

    if (s_showReady)
    {
        if (s_readyTimer > 0) s_readyTimer--;
        if (s_readyTimer == 0) s_showReady = false;
        return;
    }

    const int speed = 3;

    if (now & BTN_DPAD_LEFT)  s_playerX -= speed;
    if (now & BTN_DPAD_RIGHT) s_playerX += speed;

    int halfW = s_playerW / 2;
    if (s_playerX < (halfW + 2)) s_playerX = (halfW + 2);
    if (s_playerX > SCREEN_W - (halfW + 2)) s_playerX = SCREEN_W - (halfW + 2);

    if (s_playerCooldown > 0) s_playerCooldown--;

    bool fire = EdgePressed(now, prev, BTN_A) || EdgePressed(now, prev, BTN_B);
    if (fire && !s_bulletActive && s_playerCooldown == 0)
    {
        s_bulletActive = true;
        s_bulletX = s_playerX;
        s_bulletY = s_playerY - (s_bulletH + 2);
        s_playerCooldown = 10;

        Sfx_Play(SFX_SHOOT, DSBVOLUME_MAX);
    }
}

static void UpdateBullets()
{
    if (s_showReady || s_gameOver) return;

    if (s_bulletActive)
    {
        s_bulletY -= 6;
        if (s_bulletY < -40)
        {
            s_bulletActive = false;
        }
        else
        {
            // HITBOX FIX: Player bullet uses center X, so no need to offset in collision
            int bx = s_bulletX - (s_bulletW / 2);
            int by = s_bulletY;

            // vs UFO
            if (s_ufoActive)
            {
                int ux = s_ufoX;
                int uy = 40;
                int uw = 16 * SPR_SCALE;
                int uh = 7 * SPR_SCALE;

                if (Aabb(bx, by, s_bulletW, s_bulletH, ux, uy, uw, uh))
                {
                    s_bulletActive = false;
                    s_ufoActive = false;

                    // Random UFO score: 50, 100, 150, 200, 250, or 300 (classic)
                    int ufoScore = ((int)(RngNext() % 6) + 1) * 50;
                    ScoreAdd(ufoScore);

                    Sfx_Play(SFX_HIT, DSBVOLUME_MAX);
                }
            }

            // vs enemies
            for (int r = 0; r < EN_ROWS; ++r)
            {
                for (int c = 0; c < EN_COLS; ++c)
                {
                    Enemy& e = s_en[r][c];
                    if (!e.alive) continue;

                    if (Aabb(bx, by, s_bulletW, s_bulletH, e.x, e.y, e.w, e.h))
                    {
                        e.alive = false;
                        s_bulletActive = false;

                        int pts = (e.type == 2) ? 30 : (e.type == 1) ? 20 : 10;
                        ScoreAdd(pts);
                        Sfx_Play(SFX_ENEMY_DEATH, DSBVOLUME_MAX);
                        return;
                    }
                }
            }

            // vs shields (per-tile collision)
            for (int i = 0; i < SHIELDS; ++i)
            {
                const int tileSize = 8 * SPR_SCALE;
                int sx = s_shX[i];
                int sy = s_shY[i];

                // Check each tile
                for (int ty = 0; ty < SHIELD_TILES_H; ++ty)
                {
                    for (int tx = 0; tx < SHIELD_TILES_W; ++tx)
                    {
                        if (!s_shTiles[i][ty][tx]) continue;

                        int tileX = sx + tx * tileSize;
                        int tileY = sy + ty * tileSize;

                        if (Aabb(bx, by, s_bulletW, s_bulletH, tileX, tileY, tileSize, tileSize))
                        {
                            s_shTiles[i][ty][tx] = false;  // Destroy this tile
                            s_bulletActive = false;
                            Sfx_Play(SFX_HIT, -800);
                            return;
                        }
                    }
                }
            }
        }
    }

    for (int i = 0; i < ENEMY_BUL_MAX; ++i)
    {
        if (!s_ebActive[i]) continue;

        s_ebY[i] += 4;
        if (s_ebY[i] > SCREEN_H + 40)
        {
            s_ebActive[i] = false;
            continue;
        }

        // HITBOX FIX: Enemy bullet uses center X, so no need to offset in collision
        int ebx = s_ebX[i] - (s_ebW / 2);
        int eby = s_ebY[i];

        // vs player
        if (s_playerDeadTimer == 0 && !s_showReady)
        {
            int px = s_playerX - (s_playerW / 2);
            int py = s_playerY;
            if (Aabb(ebx, eby, s_ebW, s_ebH, px, py, s_playerW, s_playerH))
            {
                s_ebActive[i] = false;
                KillPlayer();
                continue;
            }
        }

        // vs shields (per-tile collision)
        for (int s = 0; s < SHIELDS; ++s)
        {
            const int tileSize = 8 * SPR_SCALE;
            int sx = s_shX[s];
            int sy = s_shY[s];

            // Check each tile
            for (int ty = 0; ty < SHIELD_TILES_H; ++ty)
            {
                for (int tx = 0; tx < SHIELD_TILES_W; ++tx)
                {
                    if (!s_shTiles[s][ty][tx]) continue;

                    int tileX = sx + tx * tileSize;
                    int tileY = sy + ty * tileSize;

                    if (Aabb(ebx, eby, s_ebW, s_ebH, tileX, tileY, tileSize, tileSize))
                    {
                        s_shTiles[s][ty][tx] = false;  // Destroy this tile
                        s_ebActive[i] = false;
                        Sfx_Play(SFX_HIT, -800);
                        break;
                    }
                }
            }
        }
    }
}

static void RenderHUD()
{
    g_pDevice->SetTexture(0, NULL);
    g_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    g_pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    g_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
    g_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    g_pDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

    char line[128];

    DrawHLine(0, 20, SCREEN_W, D3DCOLOR_XRGB(255, 255, 255));

    MakeLabelInt(line, "SCORE ", s_score);
    DrawText(24.0f, 6.0f, line, 2.0f, D3DCOLOR_XRGB(255, 255, 255));

    MakeLabelInt(line, "LIVES ", (s_lives < 0) ? 0 : s_lives);
    DrawText(420.0f, 6.0f, line, 2.0f, D3DCOLOR_XRGB(255, 255, 255));

    // Wave number display (top right)
    MakeLabelInt(line, "WAVE ", s_level);
    DrawText(540.0f, 6.0f, line, 2.0f, D3DCOLOR_XRGB(255, 255, 255));

    if (s_showReady && !s_gameOver)
        DrawCenteredText("GET READY", 240, 3.0f, D3DCOLOR_XRGB(255, 255, 255));

    // GAME OVER overlay: big flashing title at top + highscores / initials entry
    if (s_gameOver)
    {
        DWORD flash = (((s_frame / 10) & 1) == 0) ? D3DCOLOR_XRGB(255, 60, 60) : D3DCOLOR_XRGB(255, 210, 0);
        DrawCenteredText("GAME OVER", 44, 5.0f, flash);

        if (s_goEntryMode && !s_goSubmitted)
        {
            DrawCenteredText("NEW HIGH SCORE!", 118, 3.0f, D3DCOLOR_XRGB(255, 0, 255));
            DrawCenteredText("ENTER INITIALS", 156, 2.5f, D3DCOLOR_XRGB(255, 255, 255));

            // Initials display with cursor highlight
            char iniLine[32];
            iniLine[0] = 0;
            iniLine[0] = s_goInitials[0];
            iniLine[1] = ' ';
            iniLine[2] = s_goInitials[1];
            iniLine[3] = ' ';
            iniLine[4] = s_goInitials[2];
            iniLine[5] = 0;

            DrawCenteredText(iniLine, 206, 4.0f, D3DCOLOR_XRGB(255, 255, 255));

            // Initials display is: "A A A" (5 chars). Font cell is 6px.
            // For scale=4, each char is 24px. Total width = 5 * 24 = 120.
            const int iniY = 206;
            const int scaleI = 4;
            const int charW = 6 * scaleI;            // 24
            const int totalW = 5 * charW;            // 120
            const int baseX = (SCREEN_W - totalW) / 2;

            // letters are at char indices 0,2,4
            int charPos = (s_goCursor == 0) ? 0 : (s_goCursor == 1) ? 2 : 4;
            int lx = baseX + charPos * charW;

            // underline block under selected letter
            DrawRect(lx, iniY + charW, charW, 4, D3DCOLOR_XRGB(0, 255, 255));


            DrawCenteredText("DPAD UP/DOWN: LETTER", 280, 2.0f, D3DCOLOR_XRGB(200, 200, 200));
            DrawCenteredText("DPAD LEFT/RIGHT: MOVE", 304, 2.0f, D3DCOLOR_XRGB(200, 200, 200));
            DrawCenteredText("A/B: CONFIRM", 328, 2.0f, D3DCOLOR_XRGB(200, 200, 200));

            DrawCenteredText("PRESS START", SCREEN_H - 44, 3.0f, D3DCOLOR_XRGB(255, 255, 255));
        }
        else
        {
            // High score table (like attract) + retry hint
            DrawCenteredText("HIGH SCORES", 102, 3.0f, D3DCOLOR_XRGB(255, 0, 255));

            // Centered table: pass centerX = 320
            ScoreHS_RenderTable(320.0f, 150.0f, 2.0f, D3DCOLOR_XRGB(255, 255, 255));

            DrawCenteredText("PRESS START TO RETRY", SCREEN_H - 44, 3.0f, D3DCOLOR_XRGB(255, 255, 255));
        }
    }
}

// ------------------------------
// Public API
// ------------------------------
void Game_Init(bool secretMode)
{
    s_rng = 0xC0FFEE01u;
    s_frame = 0;
    s_prevButtons = 0;

    s_score = 0;
    s_lives = 3;
    s_level = 1;
    s_scoreFor1Up = 0;

    // Reset game over flow flags
    s_gameOver = false;
    s_goQualifies = false;
    s_goEntryMode = false;
    s_goSubmitted = false;
    s_goInitials[0] = 'A'; s_goInitials[1] = 'A'; s_goInitials[2] = 'A'; s_goInitials[3] = 0;
    s_goCursor = 0;

    // Decide mode from title secret flag (no signature changes)
    s_secretMode = Title_IsSecret() ? true : false;
    s_pack = s_secretMode ? &g_packSecret : &g_packClassic;

    // Initialize sprite animations
    if (s_pack->animations && s_pack->animCount >= 3)
    {
        s_animInvaderA.Play(&s_pack->animations[ANIM_INVADER_A]);
        s_animInvaderB.Play(&s_pack->animations[ANIM_INVADER_B]);
        s_animInvaderC.Play(&s_pack->animations[ANIM_INVADER_C]);
    }

    // Swap to game music track
    Music_Init(s_secretMode ? kGameTrm_Secret : kGameTrm_Normal);

    // Load SFX
    Sfx_Load(SFX_ENEMY_DEATH, kSfxPath_EnemyDeath);
    Sfx_Load(SFX_SHOOT, kSfxPath_Shoot);
    Sfx_Load(SFX_HIT, kSfxPath_Hit);
    Sfx_Load(SFX_PLAYER_DEAD, kSfxPath_PlayerDead);
    Sfx_Load(SFX_1UP, kSfxPath_1Up);
    Sfx_Load(SFX_UFO, kSfxPath_Ufo);

    Background_Init();

    // Place player relative to scaled sprite height
    if (s_pack)
    {
        const Sprite4& p = s_pack->sprites[SPR_PLAYER];
        s_playerW = (int)p.w * SPR_SCALE;
        s_playerH = (int)p.h * SPR_SCALE;
    }

    const int groundY = SCREEN_H - 60;
    s_playerY = groundY + 4;
    if (s_playerY > (SCREEN_H - s_playerH - 2))
        s_playerY = (SCREEN_H - s_playerH - 2);

    ResetWave();

    s_running = true;
}

void Game_Shutdown()
{
    Sfx_UnloadAll();
    Background_Shutdown();
    s_running = false;
}

bool Game_Update()
{
    if (!s_running) return false;

    s_frame++;

    WORD now = GetButtons();

    // GAME OVER flow:
    // - If qualifies: enter initials
    // - Else: show table
    // - START: if entering initials, submit/finish; otherwise restart
    if (s_gameOver)
    {
        // Keep background alive (stars/clouds continue)
        Background_Update();

        // Initials entry
        if (s_goEntryMode && !s_goSubmitted)
        {
            if (EdgePressed(now, s_prevButtons, BTN_DPAD_LEFT)) { s_goCursor--; ClampCursor(); }
            if (EdgePressed(now, s_prevButtons, BTN_DPAD_RIGHT)) { s_goCursor++; ClampCursor(); }

            if (EdgePressed(now, s_prevButtons, BTN_DPAD_UP)) { IncLetter(s_goCursor, +1); }
            if (EdgePressed(now, s_prevButtons, BTN_DPAD_DOWN)) { IncLetter(s_goCursor, -1); }

            // Confirm: A or B advances cursor, final confirm submits
            bool confirm = EdgePressed(now, s_prevButtons, BTN_A) || EdgePressed(now, s_prevButtons, BTN_B);
            bool start = EdgePressed(now, s_prevButtons, BTN_START);

            if (confirm)
            {
                if (s_goCursor < 2)
                {
                    s_goCursor++;
                }
                else
                {
                    // Submit
                    ScoreHS_Submit(s_goInitials, s_score);
                    s_goSubmitted = true;
                    s_goEntryMode = false;
                }
            }
            else if (start)
            {
                // START also submits immediately
                ScoreHS_Submit(s_goInitials, s_score);
                s_goSubmitted = true;
                s_goEntryMode = false;
            }

            s_prevButtons = now;
            return true;
        }

        // Not entering initials (or already submitted) -> START restarts
        if (EdgePressed(now, s_prevButtons, BTN_START))
        {
            s_prevButtons = now;
            return false; // exit to trigger restart
        }

        s_prevButtons = now;
        return true;
    }

    // Normal gameplay
    Background_Update();

    // Update sprite animations (assuming 60 FPS, each frame is ~16.67ms)
    const uint32_t deltaMs = 17;
    s_animInvaderA.Update(deltaMs);
    s_animInvaderB.Update(deltaMs);
    s_animInvaderC.Update(deltaMs);

    UpdateUfo();
    UpdateEnemies();
    UpdatePlayer(now, s_prevButtons);
    UpdateBullets();

    s_prevButtons = now;
    return true;
}

void Game_Render()
{
    if (!g_pDevice) return;

    Prepare2D();

    // Background: stars + dust/nebula overlay
    RenderStars();
    DrawCloudLayer(s_texClouds, s_cloudW, s_cloudH, s_cloudU0, s_cloudV0, 30, false);
    DrawCloudLayer(s_texClouds, s_cloudW, s_cloudH, s_cloudU1, s_cloudV1, 18, true);

    // UFO (sprite)
    if (s_ufoActive)
        DrawSprite4(s_pack, SPR_UFO, s_ufoX, 40, SPR_SCALE);

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

    // Shields (barrier tiles, only draw alive tiles)
    const int tile = 8 * SPR_SCALE;

    for (int i = 0; i < SHIELDS; ++i)
    {
        for (int ty = 0; ty < SHIELD_TILES_H; ++ty)
        {
            for (int tx = 0; tx < SHIELD_TILES_W; ++tx)
            {
                if (!s_shTiles[i][ty][tx]) continue;  // Skip destroyed tiles

                DrawSprite4(s_pack, SPR_BARRIER_TILE, s_shX[i] + tx * tile, s_shY[i] + ty * tile, SPR_SCALE);
            }
        }
    }

    // Player (sprite + blink while dead timer)
    if (!s_gameOver)
    {
        bool drawPlayer = true;
        if (s_playerDeadTimer > 0)
            drawPlayer = (((s_playerDeadTimer / 6) & 1) == 0) ? true : false;

        if (drawPlayer)
        {
            int px = s_playerX - (s_playerW / 2);
            DrawSprite4(s_pack, SPR_PLAYER, px, s_playerY, SPR_SCALE);
        }
    }

    // Player bullet (sprite)
    if (s_bulletActive)
        DrawSprite4(s_pack, SPR_PLAYER_BULLET, s_bulletX - (s_bulletW / 2), s_bulletY, SPR_SCALE);

    // Enemy bullets (cycle sprites per slot)
    for (int i = 0; i < ENEMY_BUL_MAX; ++i)
    {
        if (!s_ebActive[i]) continue;

        SpriteId bid = SPR_EBULLET_ZIG;
        if ((i % 3) == 1) bid = SPR_EBULLET_PLUNGER;
        else if ((i % 3) == 2) bid = SPR_EBULLET_ROLL;

        DrawSprite4(s_pack, bid, s_ebX[i] - (s_ebW / 2), s_ebY[i], SPR_SCALE);
    }

    // Ground line
    DrawHLine(0, SCREEN_H - 60, SCREEN_W, D3DCOLOR_XRGB(80, 255, 80));

    // HUD / text (includes game over overlay now)
    RenderHUD();
}