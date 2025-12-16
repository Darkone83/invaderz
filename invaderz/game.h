#pragma once

// Classic Space Invaders-style gameplay loop.
//
// Usage:
//   Game_Init(secretMode);
//   while (Game_Update()) { Game_Render(); }
//   Game_Shutdown();

// secretMode is latched from Title before Title_Shutdown().
void Game_Init(bool secretMode);
void Game_Shutdown();

// Returns false when the game loop should exit back to caller (e.g., START to quit).
bool Game_Update();

void Game_Render();
