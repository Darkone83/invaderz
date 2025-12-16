#pragma once
#include <xtl.h>

// -----------------------------------------------------------------------------
// Unified digital mask used everywhere.
//
// Notes:
// - D-Pad, START/BACK, thumb clicks = native XINPUT bits.
// - ABXY / White / Black / LT / RT = synthetic flags derived from analog buttons.
// -----------------------------------------------------------------------------

enum
{
    BTN_DPAD_UP = XINPUT_GAMEPAD_DPAD_UP,
    BTN_DPAD_DOWN = XINPUT_GAMEPAD_DPAD_DOWN,
    BTN_DPAD_LEFT = XINPUT_GAMEPAD_DPAD_LEFT,
    BTN_DPAD_RIGHT = XINPUT_GAMEPAD_DPAD_RIGHT,

    BTN_START = XINPUT_GAMEPAD_START,
    BTN_BACK = XINPUT_GAMEPAD_BACK,
    BTN_LTHUMB = XINPUT_GAMEPAD_LEFT_THUMB,
    BTN_RTHUMB = XINPUT_GAMEPAD_RIGHT_THUMB,

    // Synthetic analog-button digital flags
    BTN_A = 0x1000,
    BTN_B = 0x2000,
    BTN_X = 0x4000,
    BTN_Y = 0x8000,

    BTN_WHITE = 0x0100,
    BTN_BLACK = 0x0200,
    BTN_LTRIG = 0x0400,
    BTN_RTRIG = 0x0800,
};

// -----------------------------------------------------------------------------
// Existing API (UNCHANGED)
// -----------------------------------------------------------------------------

void InitInput();
void PumpInput();
WORD GetButtons();   // P1 / first connected
void GetSticks(int& lx, int& ly, int& rx, int& ry); // P1

// -----------------------------------------------------------------------------
// NEW: minimal multi-player extensions
// -----------------------------------------------------------------------------

// Returns true if controller [0..3] is connected
bool IsPadConnected(int port);

// Returns synthesized button mask for specific controller [0..3]
WORD GetButtons(int port);

// Returns sticks for specific controller [0..3]
// If not connected, all outputs are zero.
void GetSticks(int port, int& lx, int& ly, int& rx, int& ry);

// Helper: OR of all connected controller button masks
WORD GetButtonsAny();
