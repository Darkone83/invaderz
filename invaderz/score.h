// score.h
#pragma once

#include <xtl.h>

// Simple score/lives/high-score tracker.
// Integer-only, no allocations.

struct ScoreState
{
    int score;
    int highScore;
    int lives;
    int level;
};

void Score_Init(ScoreState& s, int startLives);
void Score_ResetRun(ScoreState& s, int startLives);     // reset score/level/lives (keep highScore)
void Score_Add(ScoreState& s, int points);              // clamps at 0..2,000,000,000
void Score_SetLevel(ScoreState& s, int level);          // clamps >= 1
void Score_LoseLife(ScoreState& s);                     // lives-- (min 0)
bool Score_IsGameOver(const ScoreState& s);             // lives <= 0

// -----------------------------------------------------------------------------
// Persistent High Scores (Top 10)
// -----------------------------------------------------------------------------
#define SCORE_HS_MAX 10

struct HighScoreEntry
{
    char initials[4]; // 3 letters + null
    int  score;
};

void ScoreHS_Init();
bool ScoreHS_Qualifies(int score);
void ScoreHS_Submit(const char initials3[4], int score);
bool ScoreHS_Get(int rank, HighScoreEntry& out);

// Renders a centered table where x is center-X.
void ScoreHS_RenderTable(float x, float y, float scale, DWORD color);