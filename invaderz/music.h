#pragma once
#include <xtl.h>

// -----------------------------------------------------------------------------
// Layered audio system (RXDK-safe)
//
//  - Music: one streaming PCM WAV player (looping)
//  - SFX:   short PCM WAVs loaded into RAM and played on demand
//
// Usage:
//   Music_Init("D:\\snd\\title.wav");   // starts immediately
//   each frame: Music_Update();
//   Sfx_Load(0, "D:\\snd\\shoot.wav");
//   Sfx_Play(0);
//   exit: Music_Shutdown();
// -----------------------------------------------------------------------------

// ---------------------- MUSIC ----------------------

bool Music_Init(const char* path);   // Loads + starts looping immediately
void Music_Update();                // Stream refill + volume ramp (call once per frame)
void Music_Shutdown();

bool Music_IsReady();
bool Music_IsPlaying();

// Optional: smooth volume changes (DSBVOLUME_MIN..DSBVOLUME_MAX)
void Music_SetVolume(LONG dsVolume);

// ---------------------- SFX ----------------------

enum { SFX_MAX = 32 };

// Load a PCM WAV into slot [0..SFX_MAX-1]. Replaces existing.
bool Sfx_Load(int slot, const char* path);

// Play slot once. volume: DSBVOLUME_MIN..DSBVOLUME_MAX (0 means full)
void Sfx_Play(int slot, LONG volume = DSBVOLUME_MAX);

// Free all SFX slots + voices (music keeps running).
void Sfx_UnloadAll();
