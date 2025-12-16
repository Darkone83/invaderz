#pragma once
#include <xtl.h>


void Title_Init(const char* normalTitleDDS, const char* secretTitleDDS);
void Title_Shutdown();

// Call once per frame (after PumpInput()).
// Returns true when START is pressed (edge-triggered).
bool Title_Update();

// Render only. Caller owns Clear/BeginScene/EndScene/Present.
void Title_Render();

// Query current mode (set by Konami code).
bool Title_IsSecret();
