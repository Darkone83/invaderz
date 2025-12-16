#include "input.h"
#include <string.h>

#define MAX_PORTS 4
#define ANALOG_THRESHOLD 30
#define STICK_DEADZONE  8000

static HANDLE       g_padHandles[MAX_PORTS];
static DWORD        g_padLastPacket[MAX_PORTS];
static XINPUT_STATE g_padStates[MAX_PORTS];
static WORD         g_padButtons[MAX_PORTS];

// -----------------------------------------------------------------------------
// InitInput
// -----------------------------------------------------------------------------
void InitInput()
{
    XInitDevices(0, 0);
    memset(g_padHandles, 0, sizeof(g_padHandles));
    memset(g_padLastPacket, 0, sizeof(g_padLastPacket));
    memset(g_padStates, 0, sizeof(g_padStates));
    memset(g_padButtons, 0, sizeof(g_padButtons));
}

// -----------------------------------------------------------------------------
// PumpInput
// -----------------------------------------------------------------------------
void PumpInput()
{
    DWORD ins = 0, rem = 0;

    if (XGetDeviceChanges(XDEVICE_TYPE_GAMEPAD, &ins, &rem))
    {
        for (int i = 0; i < MAX_PORTS; ++i)
        {
            if (ins & 1)
            {
                if (!g_padHandles[i])
                {
                    g_padHandles[i] =
                        XInputOpen(XDEVICE_TYPE_GAMEPAD, i, XDEVICE_NO_SLOT, NULL);
                }
            }

            if (rem & 1)
            {
                if (g_padHandles[i])
                {
                    XInputClose(g_padHandles[i]);
                    g_padHandles[i] = NULL;
                }
            }

            ins >>= 1;
            rem >>= 1;
        }
    }

    for (int i = 0; i < MAX_PORTS; ++i)
    {
        if (!g_padHandles[i])
        {
            g_padButtons[i] = 0;
            continue;
        }

        XINPUT_STATE st;
        ZeroMemory(&st, sizeof(st));

        if (XInputGetState(g_padHandles[i], &st) == ERROR_SUCCESS)
        {
            if (st.dwPacketNumber != g_padLastPacket[i])
            {
                g_padLastPacket[i] = st.dwPacketNumber;
                g_padStates[i] = st;

                WORD mask = st.Gamepad.wButtons;
                const BYTE* a = st.Gamepad.bAnalogButtons;

                if (a[XINPUT_GAMEPAD_A] > ANALOG_THRESHOLD) mask |= BTN_A;
                if (a[XINPUT_GAMEPAD_B] > ANALOG_THRESHOLD) mask |= BTN_B;
                if (a[XINPUT_GAMEPAD_X] > ANALOG_THRESHOLD) mask |= BTN_X;
                if (a[XINPUT_GAMEPAD_Y] > ANALOG_THRESHOLD) mask |= BTN_Y;

                if (a[XINPUT_GAMEPAD_WHITE] > ANALOG_THRESHOLD) mask |= BTN_WHITE;
                if (a[XINPUT_GAMEPAD_BLACK] > ANALOG_THRESHOLD) mask |= BTN_BLACK;

                if (a[XINPUT_GAMEPAD_LEFT_TRIGGER] > ANALOG_THRESHOLD) mask |= BTN_LTRIG;
                if (a[XINPUT_GAMEPAD_RIGHT_TRIGGER] > ANALOG_THRESHOLD) mask |= BTN_RTRIG;

                g_padButtons[i] = mask;
            }
        }
        else
        {
            g_padButtons[i] = 0;
        }
    }
}

// -----------------------------------------------------------------------------
// Existing API (UNCHANGED BEHAVIOR)
// -----------------------------------------------------------------------------
WORD GetButtons()
{
    for (int i = 0; i < MAX_PORTS; ++i)
    {
        if (g_padHandles[i])
            return g_padButtons[i];
    }
    return 0;
}

void GetSticks(int& lx, int& ly, int& rx, int& ry)
{
    lx = ly = rx = ry = 0;

    for (int i = 0; i < MAX_PORTS; ++i)
    {
        if (!g_padHandles[i])
            continue;

        const XINPUT_GAMEPAD& gp = g_padStates[i].Gamepad;

        lx = gp.sThumbLX;
        ly = gp.sThumbLY;
        rx = gp.sThumbRX;
        ry = gp.sThumbRY;

        if (abs(lx) < STICK_DEADZONE) lx = 0;
        if (abs(ly) < STICK_DEADZONE) ly = 0;
        if (abs(rx) < STICK_DEADZONE) rx = 0;
        if (abs(ry) < STICK_DEADZONE) ry = 0;

        return;
    }
}

// -----------------------------------------------------------------------------
// NEW: Multi-player API
// -----------------------------------------------------------------------------
bool IsPadConnected(int port)
{
    if (port < 0 || port >= MAX_PORTS)
        return false;
    return g_padHandles[port] != NULL;
}

WORD GetButtons(int port)
{
    if (port < 0 || port >= MAX_PORTS)
        return 0;
    return g_padButtons[port];
}

void GetSticks(int port, int& lx, int& ly, int& rx, int& ry)
{
    lx = ly = rx = ry = 0;

    if (port < 0 || port >= MAX_PORTS)
        return;

    if (!g_padHandles[port])
        return;

    const XINPUT_GAMEPAD& gp = g_padStates[port].Gamepad;

    lx = gp.sThumbLX;
    ly = gp.sThumbLY;
    rx = gp.sThumbRX;
    ry = gp.sThumbRY;

    if (abs(lx) < STICK_DEADZONE) lx = 0;
    if (abs(ly) < STICK_DEADZONE) ly = 0;
    if (abs(rx) < STICK_DEADZONE) rx = 0;
    if (abs(ry) < STICK_DEADZONE) ry = 0;
}

WORD GetButtonsAny()
{
    WORD mask = 0;
    for (int i = 0; i < MAX_PORTS; ++i)
    {
        if (g_padHandles[i])
            mask |= g_padButtons[i];
    }
    return mask;
}
