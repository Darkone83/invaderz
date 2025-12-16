#include "title.h"

#include <xgraphics.h>   // XGSwizzleRect
#include <string.h>
#include <stdlib.h>

#include "font.h"
#include "input.h"
#include "music.h"
#include "attract.h"

// Device provided by main.cpp
extern LPDIRECT3DDEVICE8 g_pDevice;

static const float SCREEN_W = 640.0f;
static const float SCREEN_H = 480.0f;

struct TitleVertex
{
    float x, y, z, rhw;
    DWORD color;
    float u, v;
};

#define TITLE_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)
#define TEXT_FVF  (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

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

// -----------------------------------------------------------------------------
// Defaults
// -----------------------------------------------------------------------------
static const char* kDefaultNormalTex = "D:\\tex\\title_classic.dds";
static const char* kDefaultSecretTex = "D:\\tex\\title_secret.dds";
static const char* kDefaultTitleTrm = "D:\\snd\\title.trm";
static const char* kSfxPath_PlayerDead = "D:\\snd\\pdeath.wav";  // Player death sound for Konami code

// SFX slot for player death sound
enum
{
    SFX_KONAMI = 0  // Use slot 0 for the Konami code sound
};

// -----------------------------------------------------------------------------
// Local state
// -----------------------------------------------------------------------------
static LPDIRECT3DTEXTURE8 s_texNormal = NULL;
static LPDIRECT3DTEXTURE8 s_texSecret = NULL;

static int  s_texNormalW = 0, s_texNormalH = 0;
static int  s_texSecretW = 0, s_texSecretH = 0;

static bool s_secret = false;
static int  s_frame = 0;
static WORD s_prevButtons = GetButtons();

// Attract state
enum TitleMode
{
    MODE_TITLE = 0,
    MODE_ATTRACT = 1
};
static TitleMode s_mode = MODE_TITLE;
static int  s_idleFrames = 0;

// 60fps assumptions
static const int kIdleToAttractFrames = 15 * 60;

// Konami sequence
static const WORD s_konamiSeq[] =
{
    BTN_DPAD_UP,
    BTN_DPAD_UP,
    BTN_DPAD_DOWN,
    BTN_DPAD_DOWN,
    BTN_DPAD_LEFT,
    BTN_DPAD_RIGHT,
    BTN_DPAD_LEFT,
    BTN_DPAD_RIGHT,
    BTN_B,
    BTN_A
};
static int  s_konamiPos = 0;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static __forceinline int IsPow2(int v) { return (v > 0) && ((v & (v - 1)) == 0); }
static __forceinline int AbsI(int v) { return (v < 0) ? -v : v; }

static void PrepareForText2D()
{
    if (!g_pDevice) return;

    g_pDevice->SetTexture(0, NULL);

    g_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    g_pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    g_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);

    g_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
    g_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    g_pDevice->SetVertexShader(TEXT_FVF);
}

// Strict DDS loader for OG Xbox swizzled textures.
// Supports: uncompressed A8R8G8B8 only, power-of-two dimensions.
//
// NOTE: We apply a simple COLOR-KEY on load to kill the "grey box":
//   - key color is sampled from the top-left pixel
//   - any pixel within tolerance of that key gets alpha=0
static LPDIRECT3DTEXTURE8 LoadTextureFromDDS_Rect_ColorKey(const char* path, int& outW, int& outH, int tol)
{
    outW = 0;
    outH = 0;

    if (!g_pDevice || !path || !path[0])
        return NULL;

    HANDLE hFile = CreateFileA(
        path, GENERIC_READ, FILE_SHARE_READ, NULL,
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

    // -------------------------------------------------------------
    // Color-key punchout: sample the top-left pixel as the "background"
    // -------------------------------------------------------------
    if (tol < 0) tol = 0;
    if (tol > 255) tol = 255;

    // DDS data is A8R8G8B8 in memory as 0xAARRGGBB (little-endian bytes: BB GG RR AA)
    const BYTE keyB = pixels[0];
    const BYTE keyG = pixels[1];
    const BYTE keyR = pixels[2];

    BYTE* p = pixels;
    DWORD count = (DWORD)(w * h);
    for (DWORD i = 0; i < count; ++i, p += 4)
    {
        const int b = (int)p[0];
        const int g = (int)p[1];
        const int r = (int)p[2];

        if (AbsI(b - (int)keyB) <= tol &&
            AbsI(g - (int)keyG) <= tol &&
            AbsI(r - (int)keyR) <= tol)
        {
            // set alpha=0
            p[3] = 0;
        }
        else
        {
            // force solid alpha for foreground so it reads crisp
            p[3] = 255;
        }
    }

    // Create swizzled texture and upload
    LPDIRECT3DTEXTURE8 tex = NULL;
    if (FAILED(g_pDevice->CreateTexture(
        (UINT)w, (UINT)h,
        1,
        0,
        D3DFMT_A8R8G8B8,
        0,
        &tex)))
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

static void DrawCenteredText(const char* s, float y, float scale, DWORD color)
{
    if (!s || !s[0]) return;

    float w = (float)strlen(s) * (6.0f * scale);
    float x = (SCREEN_W - w) * 0.5f;
    DrawText(x, y, s, scale, color);
}

// 80's title: logo top-centered, scaled to fit within a box (no fullscreen cover).
static void DrawTextureTopCenteredFit(LPDIRECT3DTEXTURE8 tex, int tw, int th,
    float yTop, float maxW, float maxH)
{
    if (!g_pDevice || !tex || tw <= 0 || th <= 0)
        return;

    float sx = maxW / (float)tw;
    float sy = maxH / (float)th;
    float s = (sx < sy) ? sx : sy;

    float w = (float)tw * s;
    float h = (float)th * s;

    float x0 = (SCREEN_W - w) * 0.5f;
    float y0 = yTop;
    float x1 = x0 + w;
    float y1 = y0 + h;

    TitleVertex v[4];
    v[0].x = x0; v[0].y = y0; v[0].z = 0.0f; v[0].rhw = 1.0f; v[0].color = 0xFFFFFFFF; v[0].u = 0.0f; v[0].v = 0.0f;
    v[1].x = x1; v[1].y = y0; v[1].z = 0.0f; v[1].rhw = 1.0f; v[1].color = 0xFFFFFFFF; v[1].u = 1.0f; v[1].v = 0.0f;
    v[2].x = x0; v[2].y = y1; v[2].z = 0.0f; v[2].rhw = 1.0f; v[2].color = 0xFFFFFFFF; v[2].u = 0.0f; v[2].v = 1.0f;
    v[3].x = x1; v[3].y = y1; v[3].z = 0.0f; v[3].rhw = 1.0f; v[3].color = 0xFFFFFFFF; v[3].u = 1.0f; v[3].v = 1.0f;

    g_pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    g_pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    g_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);

    g_pDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    g_pDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_pDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

    g_pDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
    g_pDevice->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
    g_pDevice->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);

    g_pDevice->SetTexture(0, tex);
    g_pDevice->SetVertexShader(TITLE_FVF);
    g_pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(TitleVertex));
}

static __forceinline WORD EdgePressed(WORD now, WORD prev, WORD bit)
{
    return (WORD)((now & bit) && !(prev & bit));
}

static void Konami_Feed(WORD edgeBit)
{
    if (s_secret) return;
    if (edgeBit == 0) return;

    WORD expect = s_konamiSeq[s_konamiPos];

    if (edgeBit == expect)
    {
        s_konamiPos++;
        if (s_konamiPos >= (int)(sizeof(s_konamiSeq) / sizeof(s_konamiSeq[0])))
        {
            s_secret = true;
            s_konamiPos = 0;

            // Play player death sound when Konami code is completed!
            Sfx_Play(SFX_KONAMI, DSBVOLUME_MAX);
        }
    }
    else
    {
        if (edgeBit == s_konamiSeq[0]) s_konamiPos = 1;
        else                          s_konamiPos = 0;
    }
}

static void EnterAttract()
{
    if (s_mode == MODE_ATTRACT)
        return;

    s_mode = MODE_ATTRACT;
    Attract_Init(s_secret);

    // prevent instant edge-trigger behavior when returning
    s_prevButtons = GetButtons();
}

static void ExitAttract()
{
    if (s_mode != MODE_ATTRACT)
        return;

    Attract_Shutdown();
    s_mode = MODE_TITLE;

    // reset idle timer so it doesn't re-enter immediately
    s_idleFrames = 0;

    // resync edge tracking
    s_prevButtons = GetButtons();
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
void Title_Init(const char* normalTitleDDS, const char* secretTitleDDS)
{
    s_frame = 0;
    s_prevButtons = 0;
    s_konamiPos = 0;
    s_secret = false;

    s_mode = MODE_TITLE;
    s_idleFrames = 0;

    if (s_texNormal) { s_texNormal->Release(); s_texNormal = NULL; }
    if (s_texSecret) { s_texSecret->Release(); s_texSecret = NULL; }
    s_texNormalW = s_texNormalH = 0;
    s_texSecretW = s_texSecretH = 0;

    if (!normalTitleDDS || !normalTitleDDS[0]) normalTitleDDS = kDefaultNormalTex;
    if (!secretTitleDDS || !secretTitleDDS[0]) secretTitleDDS = kDefaultSecretTex;

    const int kKeyTol = 16;

    s_texNormal = LoadTextureFromDDS_Rect_ColorKey(normalTitleDDS, s_texNormalW, s_texNormalH, kKeyTol);
    s_texSecret = LoadTextureFromDDS_Rect_ColorKey(secretTitleDDS, s_texSecretW, s_texSecretH, kKeyTol);

    // Load player death sound for Konami code completion
    Sfx_Load(SFX_KONAMI, kSfxPath_PlayerDead);

    // Start title music (Music_Init starts immediately; no Music_Play)
    Music_Init(kDefaultTitleTrm);
}

void Title_Shutdown()
{
    // If we shut down while attract is running, clean it up.
    if (s_mode == MODE_ATTRACT)
        ExitAttract();

    if (s_texNormal) { s_texNormal->Release(); s_texNormal = NULL; }
    if (s_texSecret) { s_texSecret->Release(); s_texSecret = NULL; }

    s_texNormalW = s_texNormalH = 0;
    s_texSecretW = s_texSecretH = 0;

    // Unload the Konami sound
    Sfx_UnloadAll();
}

bool Title_Update()
{
    s_frame++;

    WORD now = GetButtons();

    // Any activity resets idle timer (including held buttons)
    if (now != 0)
        s_idleFrames = 0;
    else
        s_idleFrames++;

    // -----------------------
    // ATTRACT MODE
    // -----------------------
    if (s_mode == MODE_ATTRACT)
    {
        // Any button press exits attract back to title immediately.
        if (now != 0)
        {
            ExitAttract();
            // do NOT start game on this frame
            s_prevButtons = now;
            return false;
        }

        // Let attract run; when it returns false, go back to title.
        if (!Attract_Update())
        {
            ExitAttract();
            s_prevButtons = now;
            return false;
        }

        s_prevButtons = now;
        return false; // title shouldn't start game while in attract
    }

    // -----------------------
    // TITLE MODE
    // -----------------------
    // Enter attract after 15 seconds idle
    if (s_idleFrames >= kIdleToAttractFrames)
    {
        EnterAttract();
        s_prevButtons = now;
        return false;
    }

    // Konami edges
    WORD eUp = EdgePressed(now, s_prevButtons, BTN_DPAD_UP) ? BTN_DPAD_UP : 0;
    WORD eDown = EdgePressed(now, s_prevButtons, BTN_DPAD_DOWN) ? BTN_DPAD_DOWN : 0;
    WORD eLeft = EdgePressed(now, s_prevButtons, BTN_DPAD_LEFT) ? BTN_DPAD_LEFT : 0;
    WORD eRight = EdgePressed(now, s_prevButtons, BTN_DPAD_RIGHT) ? BTN_DPAD_RIGHT : 0;
    WORD eB = EdgePressed(now, s_prevButtons, BTN_B) ? BTN_B : 0;
    WORD eA = EdgePressed(now, s_prevButtons, BTN_A) ? BTN_A : 0;

    if (eUp)    Konami_Feed(eUp);
    else if (eDown)  Konami_Feed(eDown);
    else if (eLeft)  Konami_Feed(eLeft);
    else if (eRight) Konami_Feed(eRight);
    else if (eB)     Konami_Feed(eB);
    else if (eA)     Konami_Feed(eA);

    bool startPressed = EdgePressed(now, s_prevButtons, BTN_START) ? true : false;

    if (startPressed)
    {
        // Consume START fully so it cannot leak into Game_Init / Game_Update
        s_prevButtons = now;
        return true;
    }

    s_prevButtons = now;
    return false;

}

// ... [FILE CONTENT ABOVE IS UNCHANGED — OMITTED HERE FOR BREVITY]
// EVERYTHING YOU PROVIDED REMAINS IDENTICAL UP TO Title_Render()

void Title_Render()
{
    if (!g_pDevice)
        return;

    // If attract is active, it owns the frame.
    if (s_mode == MODE_ATTRACT)
    {
        Attract_Render();
        return;
    }

    LPDIRECT3DTEXTURE8 tex = s_secret ? s_texSecret : s_texNormal;
    int tw = s_secret ? s_texSecretW : s_texNormalW;
    int th = s_secret ? s_texSecretH : s_texNormalH;

    // 80's layout: logo at top, transparent background (color-keyed on load).
    if (tex && tw > 0 && th > 0)
    {
        DrawTextureTopCenteredFit(tex, tw, th,
            48.0f,    // yTop
            560.0f,   // maxW
            220.0f);  // maxH
    }
    else
    {
        PrepareForText2D();
        DrawCenteredText(s_secret ? "MISSING SECRET TITLE" : "MISSING CLASSIC TITLE",
            120.0f, 2.0f, D3DCOLOR_XRGB(255, 0, 0));
    }

    // --- TEXT overlay ---
    PrepareForText2D();

    if (s_secret)
        DrawCenteredText("XBOX SCENE EDITION", 245.0f, 3.0f, D3DCOLOR_XRGB(20, 255, 0));

    bool showPress = ((s_frame / 30) & 1) ? false : true;
    if (showPress)
    {
        DWORD pressCol = s_secret ? D3DCOLOR_XRGB(255, 210, 0) : D3DCOLOR_XRGB(255, 255, 255);
        DrawCenteredText("PRESS START", 300.0f, 2.5f, pressCol);
    }

    DWORD copyCol = s_secret ? D3DCOLOR_XRGB(190, 0, 255) : D3DCOLOR_XRGB(255, 255, 255);
    DrawCenteredText("(C) 2025 DARKONE83", 375.0f, 1.6f, copyCol);

    // -----------------------------------------------------------------
    // RXDK footer (new)
    // -----------------------------------------------------------------
    DrawCenteredText(
        "BUILT WITH RXDK",
        440.0f,                      // near bottom, overscan-safe
        1.2f,
        D3DCOLOR_XRGB(255, 0, 255)   // magenta
    );
}

bool Title_IsSecret()
{
    return s_secret;
}
