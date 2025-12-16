// main.cpp - Space Invaders (RXDK) bootstrap
//
// State flow:
//   TITLE  -> (START) -> GAME  -> (Game_Update()==false) -> TITLE
//   TITLE  -> (BACK)     -> DASHBOARD
//

#include <xtl.h>
#include <xgraphics.h>
#include <string.h>

#include "input.h"
#include "music.h"
#include "title.h"
#include "game.h"
#include "score.h"

// Global D3D device used by title.cpp / font.cpp / etc.
LPDIRECT3DDEVICE8 g_pDevice = NULL;
static LPDIRECT3D8 s_d3d = NULL;

static const UINT SCREEN_W = 640;
static const UINT SCREEN_H = 480;

static bool InitD3D()
{
    s_d3d = Direct3DCreate8(D3D_SDK_VERSION);
    if (!s_d3d)
        return false;

    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));

    pp.BackBufferWidth = SCREEN_W;
    pp.BackBufferHeight = SCREEN_H;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;

    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = NULL;

    pp.Windowed = FALSE;

    // keep simple like your other scenes
    pp.EnableAutoDepthStencil = FALSE;

    pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE;

    HRESULT hr = s_d3d->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        NULL,
        D3DCREATE_HARDWARE_VERTEXPROCESSING,
        &pp,
        &g_pDevice
    );

    if (FAILED(hr) || !g_pDevice)
    {
        // fallback
        hr = s_d3d->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            NULL,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &pp,
            &g_pDevice
        );
    }

    if (FAILED(hr) || !g_pDevice)
        return false;

    // Viewport (DON'T assume defaults)
    D3DVIEWPORT8 vp;
    vp.X = 0;
    vp.Y = 0;
    vp.Width = SCREEN_W;
    vp.Height = SCREEN_H;
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;
    g_pDevice->SetViewport(&vp);

    // Safe baseline state for 2D
    g_pDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    g_pDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pDevice->SetRenderState(D3DRS_LIGHTING, FALSE);

    return true;
}

static void ShutdownD3D()
{
    if (g_pDevice)
    {
        g_pDevice->Release();
        g_pDevice = NULL;
    }
    if (s_d3d)
    {
        s_d3d->Release();
        s_d3d = NULL;
    }
}

static void ExitToDashboard()
{
    // XDK-style return to dash
    XLaunchNewImage(NULL, NULL);
}

enum AppState
{
    STATE_TITLE = 0,
    STATE_GAME = 1,
};

int __cdecl main()
{
    if (!InitD3D())
        return 0;

    // Input
    InitInput();

    AppState state = STATE_TITLE;

	ScoreHS_Init();

    // Title assets (Title_Init owns texture load and also starts title music)
    Title_Init("D:\\tex\\title_classic.dds", "D:\\tex\\title_secret.dds");

    // Edge tracking for dashboard exit on Title only
    WORD prevButtons = 0;

    bool running = true;
    while (running)
    {
        PumpInput();

        // Stream music + handle ramps (must be every frame)
        Music_Update();

        WORD nowButtons = GetButtons();
        WORD pressedEdges = (WORD)(nowButtons & (WORD)~prevButtons);

        // Global clear
        g_pDevice->Clear(
            0, NULL,
            D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(0, 0, 0),
            1.0f,
            0
        );

        if (state == STATE_TITLE)
        {
            // X exits to dash ONLY on title (edge-triggered)
            if (pressedEdges & BTN_BACK)
            {
                ExitToDashboard();
                // If it returns for any reason, just keep rendering.
            }

            // Title_Update returns true when START edge is detected
            if (Title_Update())
            {
                // Latch secret mode BEFORE Title_Shutdown (Title may clear its internal flag on shutdown)
                const bool secretMode = Title_IsSecret();

                // Transition to game
                Title_Shutdown();

                Game_Init(secretMode);
                state = STATE_GAME;

                // IMPORTANT: skip rendering Title this frame after shutdown
                prevButtons = nowButtons;
                g_pDevice->Present(NULL, NULL, NULL, NULL);
                continue;
            }


            if (SUCCEEDED(g_pDevice->BeginScene()))
            {
                Title_Render();
                g_pDevice->EndScene();
            }
        }
        else // STATE_GAME
        {
            // Game_Update returns false when it wants to exit back to title
            if (!Game_Update())
            {
                Game_Shutdown();

                // Back to title (re-init assets + title music)
                Title_Init("D:\\tex\\title_classic.dds", "D:\\tex\\title_secret.dds");
                state = STATE_TITLE;

                // reset edge tracking so X doesn’t instantly fire on return
                prevButtons = nowButtons;
                g_pDevice->Present(NULL, NULL, NULL, NULL);
                continue;
            }

            if (SUCCEEDED(g_pDevice->BeginScene()))
            {
                Game_Render();
                g_pDevice->EndScene();
            }
        }

        g_pDevice->Present(NULL, NULL, NULL, NULL);
        prevButtons = nowButtons;
    }

    // Shutdown
    if (state == STATE_GAME)
        Game_Shutdown();
    else
        Title_Shutdown();

    Music_Shutdown();
    ShutdownD3D();
    return 0;
}
