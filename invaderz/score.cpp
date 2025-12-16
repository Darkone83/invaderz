// score.cpp
#include "score.h"

#include <xtl.h>
#include <string.h>
#include <stdlib.h>

#include "font.h"

// -----------------------------------------------------------------------------
// Small helpers (existing)
// -----------------------------------------------------------------------------
static __forceinline int ClampMin(int v, int lo)
{
    return (v < lo) ? lo : v;
}

static __forceinline int ClampMax(int v, int hi)
{
    return (v > hi) ? hi : v;
}

static __forceinline int Clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// -----------------------------------------------------------------------------
// Existing Score_* API (unchanged behavior)
// -----------------------------------------------------------------------------
void Score_Init(ScoreState& s, int startLives)
{
    s.score = 0;
    s.highScore = 0;
    s.lives = Clamp(startLives, 0, 9);
    s.level = 1;
}

void Score_ResetRun(ScoreState& s, int startLives)
{
    s.score = 0;
    s.lives = Clamp(startLives, 0, 9);
    s.level = 1;
}

void Score_Add(ScoreState& s, int points)
{
    if (points <= 0)
        return;

    const int kMaxScore = 2000000000;
    int cur = s.score;

    if (cur >= kMaxScore)
        return;

    if (points > (kMaxScore - cur))
        cur = kMaxScore;
    else
        cur += points;

    s.score = cur;

    if (s.score > s.highScore)
        s.highScore = s.score;
}

void Score_SetLevel(ScoreState& s, int level)
{
    s.level = ClampMin(level, 1);
}

void Score_LoseLife(ScoreState& s)
{
    if (s.lives > 0)
        s.lives--;
    s.lives = ClampMin(s.lives, 0);
}

bool Score_IsGameOver(const ScoreState& s)
{
    return (s.lives <= 0);
}

// -----------------------------------------------------------------------------
// Persistent High Scores - UDATA (RXDK/XDK style)
// -----------------------------------------------------------------------------
//
// Xbox.h signature you provided:
// DWORD XCreateSaveGame(
//   LPCSTR  lpRootPathName,
//   LPCWSTR lpSaveGameName,
//   DWORD   dwCreationDisposition,
//   DWORD   dwCreateFlags,
//   LPSTR   lpPathBuffer,
//   UINT    uSize
// );
//
// Also in Xbox.h:
// BOOL XMountUtilityDrive(IN BOOL fFormatClean);
//
// IMPORTANT:
// - Save/utility content is on U:\ (utility drive), not E:\
// - If U: is not mounted, U:\UDATA won't exist / won't be writable
// -----------------------------------------------------------------------------

static const char* kHS_TitleId8 = "494E5644";          // 8 hex chars
static const char* kHS_RootA = "U:\\UDATA";         // <-- FIX: use U: not E:
static const WCHAR  kHS_SaveNameW[] = L"HighScores";      // save folder name
static const char* kHS_DataFileA = "highscore.dat";

#ifndef OPEN_ALWAYS
#define OPEN_ALWAYS 4
#endif

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

static bool s_hsLoaded = false;
static HighScoreEntry s_hs[SCORE_HS_MAX];

static bool s_hsPathReady = false;
static char s_hsSaveDirA[MAX_PATH];   // directory path
static char s_hsFilePathA[MAX_PATH];  // directory + filename

static bool s_utilMounted = false;

// -----------------------------------------------------------------------------
// Tiny string helpers (no sprintf)
// -----------------------------------------------------------------------------
static __forceinline void StrCpySafe(char* dst, int dstsz, const char* src)
{
    if (!dst || dstsz <= 0) return;
    if (!src) { dst[0] = 0; return; }

    int i = 0;
    while (src[i] && i < (dstsz - 1))
    {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static __forceinline void StrCatSafe(char* dst, int dstsz, const char* src)
{
    if (!dst || dstsz <= 0) return;
    if (!src) return;

    int len = (int)strlen(dst);
    if (len >= (dstsz - 1)) return;

    int i = 0;
    while (src[i] && (len + i) < (dstsz - 1))
    {
        dst[len + i] = src[i];
        ++i;
    }
    dst[len + i] = 0;
}

static __forceinline void EnsureTrailingSlash(char* path, int pathsz)
{
    int n = (int)strlen(path);
    if (n <= 0) return;
    char last = path[n - 1];
    if (last != '\\' && last != '/')
        StrCatSafe(path, pathsz, "\\");
}

static void StripTrailingSlash(char* path)
{
    int n = (int)strlen(path);
    while (n > 0)
    {
        char c = path[n - 1];
        if (c == '\\' || c == '/')
        {
            path[n - 1] = 0;
            --n;
            continue;
        }
        break;
    }
}

static bool DirExistsA(const char* path)
{
    if (!path || !path[0]) return false;

    // GetFileAttributesA can be picky with trailing slashes on some impls
    char tmp[MAX_PATH];
    StrCpySafe(tmp, (int)sizeof(tmp), path);
    StripTrailingSlash(tmp);

    DWORD a = GetFileAttributesA(tmp);
    if (a == INVALID_FILE_ATTRIBUTES) return false;
    return (a & FILE_ATTRIBUTE_DIRECTORY) ? true : false;
}

static void MakeDirA(const char* path)
{
    CreateDirectoryA(path, NULL); // ignore failure (already exists)
}

static void BuildFilePathFromDir(char* out, int outsz, const char* dir, const char* file)
{
    out[0] = 0;
    StrCpySafe(out, outsz, dir);
    EnsureTrailingSlash(out, outsz);
    StrCatSafe(out, outsz, file);
}

// -----------------------------------------------------------------------------
// Ensure U: is mounted + ensure save path exists
// -----------------------------------------------------------------------------
static void EnsureUtilityMounted()
{
    if (s_utilMounted)
        return;

    // fFormatClean = FALSE (do not format)
    // If mounting fails, we still proceed with manual create attempts, but
    // most setups require this for U: to exist.
    XMountUtilityDrive(FALSE);
    s_utilMounted = true;
}

static bool HS_EnsurePath()
{
    if (s_hsPathReady)
        return true;

    s_hsSaveDirA[0] = 0;
    s_hsFilePathA[0] = 0;

    EnsureUtilityMounted();

    // 1) Try XCreateSaveGame (proper API)
    // NOTE: root is LPCSTR, save name is LPCWSTR, out path is LPSTR
    DWORD r = XCreateSaveGame(
        kHS_RootA,                  // U:\UDATA
        kHS_SaveNameW,              // L"HighScores"
        (DWORD)OPEN_ALWAYS,         // disposition
        0,                          // flags
        s_hsSaveDirA,               // out dir (ANSI)
        (UINT)sizeof(s_hsSaveDirA)
    );

    if ((r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS) && s_hsSaveDirA[0])
    {
        EnsureTrailingSlash(s_hsSaveDirA, (int)sizeof(s_hsSaveDirA));

        // Some environments claim success but don't actually materialize dirs:
        if (DirExistsA(s_hsSaveDirA))
        {
            BuildFilePathFromDir(s_hsFilePathA, (int)sizeof(s_hsFilePathA), s_hsSaveDirA, kHS_DataFileA);
            s_hsPathReady = true;
            return true;
        }
    }

    // 2) Fallback: manually create U:\UDATA\<TITLEID>\HighScores\
    // Parents first.
    MakeDirA("U:\\UDATA");

    char titleDir[MAX_PATH];
    titleDir[0] = 0;
    StrCpySafe(titleDir, (int)sizeof(titleDir), "U:\\UDATA\\");
    StrCatSafe(titleDir, (int)sizeof(titleDir), kHS_TitleId8);
    MakeDirA(titleDir);

    char hsDir[MAX_PATH];
    hsDir[0] = 0;
    StrCpySafe(hsDir, (int)sizeof(hsDir), titleDir);
    EnsureTrailingSlash(hsDir, (int)sizeof(hsDir));
    StrCatSafe(hsDir, (int)sizeof(hsDir), "HighScores");
    MakeDirA(hsDir);

    if (!DirExistsA(hsDir))
    {
        s_hsPathReady = false;
        return false;
    }

    // Save dir = U:\UDATA\494E5644\HighScores\

    StrCpySafe(s_hsSaveDirA, (int)sizeof(s_hsSaveDirA), "U:\\UDATA\\");
    StrCatSafe(s_hsSaveDirA, (int)sizeof(s_hsSaveDirA), kHS_TitleId8);
    StrCatSafe(s_hsSaveDirA, (int)sizeof(s_hsSaveDirA), "\\HighScores\\");
    EnsureTrailingSlash(s_hsSaveDirA, (int)sizeof(s_hsSaveDirA));

    BuildFilePathFromDir(s_hsFilePathA, (int)sizeof(s_hsFilePathA), s_hsSaveDirA, kHS_DataFileA);

    s_hsPathReady = true;
    return true;
}

// -----------------------------------------------------------------------------
// RNG (deterministic, integer-only)
// -----------------------------------------------------------------------------
static DWORD s_rng = 0xC0FFEE01u;
static __forceinline DWORD RngNext()
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return s_rng;
}
static __forceinline int RngRange(int lo, int hi)
{
    if (hi <= lo) return lo;
    DWORD r = RngNext();
    int span = (hi - lo) + 1;
    return lo + (int)(r % (DWORD)span);
}

// -----------------------------------------------------------------------------
// HS core
// -----------------------------------------------------------------------------
static void HS_Clear()
{
    for (int i = 0; i < SCORE_HS_MAX; ++i)
    {
        s_hs[i].initials[0] = 'A';
        s_hs[i].initials[1] = 'A';
        s_hs[i].initials[2] = 'A';
        s_hs[i].initials[3] = 0;
        s_hs[i].score = 0;
    }
}

static __forceinline void HS_NormalizeInitials(char out3[4], const char in3[4])
{
    out3[0] = (in3 && in3[0]) ? in3[0] : 'A';
    out3[1] = (in3 && in3[1]) ? in3[1] : 'A';
    out3[2] = (in3 && in3[2]) ? in3[2] : 'A';
    out3[3] = 0;

    for (int i = 0; i < 3; ++i)
    {
        char c = out3[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c < 'A' || c > 'Z') c = 'A';
        out3[i] = c;
    }
}

static void HS_SortHighToLow()
{
    for (int pass = 0; pass < SCORE_HS_MAX - 1; ++pass)
    {
        for (int i = 0; i < SCORE_HS_MAX - 1; ++i)
        {
            if (s_hs[i].score < s_hs[i + 1].score)
            {
                HighScoreEntry t = s_hs[i];
                s_hs[i] = s_hs[i + 1];
                s_hs[i + 1] = t;
            }
        }
    }
}

static bool HS_LoadFile()
{
    if (!HS_EnsurePath())
        return false;

    HANDLE h = CreateFileA(s_hsFilePathA, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (h == INVALID_HANDLE_VALUE)
        return false;

    char buf[512];
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, sizeof(buf) - 1, &got, NULL);
    CloseHandle(h);

    if (!ok || got == 0)
        return false;

    buf[got] = 0;

    HS_Clear();

    int idx = 0;
    const char* p = buf;
    while (*p && idx < SCORE_HS_MAX)
    {
        while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') ++p;
        if (!*p) break;

        char a = p[0];
        char b = p[1];
        char c = p[2];
        if (!a || !b || !c) break;
        p += 3;

        if (*p != ' ' && *p != '\t')
            return false;

        while (*p == ' ' || *p == '\t') ++p;

        int val = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9')
        {
            val = val * 10 + (*p - '0');
            ++p;
            ++digits;
            if (digits > 10) break;
        }

        if (digits == 0) return false;
        if (val < 0) val = 0;

        s_hs[idx].initials[0] = a;
        s_hs[idx].initials[1] = b;
        s_hs[idx].initials[2] = c;
        s_hs[idx].initials[3] = 0;
        s_hs[idx].score = val;

        while (*p && *p != '\n') ++p;
        if (*p == '\n') ++p;

        ++idx;
    }

    if (idx == 0)
        return false;

    HS_SortHighToLow();
    return true;
}

static bool HS_SaveFile()
{
    if (!HS_EnsurePath())
        return false;

    // Create/truncate file
    HANDLE h = CreateFileA(s_hsFilePathA, GENERIC_WRITE, FILE_SHARE_READ, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (h == INVALID_HANDLE_VALUE)
        return false;

    char out[512];
    int pos = 0;

    for (int i = 0; i < SCORE_HS_MAX; ++i)
    {
        const char* ini = s_hs[i].initials;

        // "ABC 12345\r\n"
        if (pos + 4 >= (int)sizeof(out)) break;
        out[pos++] = ini[0];
        out[pos++] = ini[1];
        out[pos++] = ini[2];
        out[pos++] = ' ';

        // int -> ascii (no sprintf)
        char tmp[16];
        int n = 0;
        int v = s_hs[i].score;
        if (v < 0) v = 0;

        if (v == 0)
        {
            tmp[n++] = '0';
        }
        else
        {
            while (v > 0 && n < (int)sizeof(tmp))
            {
                tmp[n++] = (char)('0' + (v % 10));
                v /= 10;
            }
        }

        while (n > 0 && pos < (int)sizeof(out) - 1)
            out[pos++] = tmp[--n];

        if (pos + 2 >= (int)sizeof(out)) break;
        out[pos++] = '\r';
        out[pos++] = '\n';
    }

    DWORD wrote = 0;
    BOOL ok = WriteFile(h, out, (DWORD)pos, &wrote, NULL);
    if (ok) FlushFileBuffers(h);
    CloseHandle(h);

    return (ok && wrote == (DWORD)pos);
}

static void HS_GenerateFiller()
{
    DWORD t = GetTickCount();
    s_rng ^= (t ^ 0xA5A5A5A5u);

    int base = RngRange(1200, 4800);

    for (int i = 0; i < SCORE_HS_MAX; ++i)
    {
        char ini[4];
        ini[0] = (char)('A' + (RngNext() % 26));
        ini[1] = (char)('A' + (RngNext() % 26));
        ini[2] = (char)('A' + (RngNext() % 26));
        ini[3] = 0;

        int bump = RngRange(0, 900);
        int score = base + bump + (SCORE_HS_MAX - 1 - i) * RngRange(200, 600);

        HS_NormalizeInitials(s_hs[i].initials, ini);
        s_hs[i].score = score;
    }

    HS_SortHighToLow();
    HS_SaveFile(); // attempt to persist
}

// -----------------------------------------------------------------------------
// Public HS API
// -----------------------------------------------------------------------------
void ScoreHS_Init()
{
    if (s_hsLoaded)
        return;

    HS_Clear();

    if (!HS_LoadFile())
    {
        HS_GenerateFiller();
        // keep in-memory either way
    }

    s_hsLoaded = true;
}

bool ScoreHS_Qualifies(int score)
{
    ScoreHS_Init();

    if (score <= 0)
        return false;

    return (score > s_hs[SCORE_HS_MAX - 1].score) ? true : false;
}

void ScoreHS_Submit(const char initials3[4], int score)
{
    ScoreHS_Init();

    if (score <= 0)
        return;

    if (!ScoreHS_Qualifies(score))
        return;

    char ini[4];
    HS_NormalizeInitials(ini, initials3);

    s_hs[SCORE_HS_MAX - 1].initials[0] = ini[0];
    s_hs[SCORE_HS_MAX - 1].initials[1] = ini[1];
    s_hs[SCORE_HS_MAX - 1].initials[2] = ini[2];
    s_hs[SCORE_HS_MAX - 1].initials[3] = 0;
    s_hs[SCORE_HS_MAX - 1].score = score;

    HS_SortHighToLow();
    HS_SaveFile();
}

bool ScoreHS_Get(int rank, HighScoreEntry& out)
{
    ScoreHS_Init();

    if (rank < 0 || rank >= SCORE_HS_MAX)
        return false;

    out = s_hs[rank];
    return true;
}

void ScoreHS_RenderTable(float x, float y, float scale, DWORD color)
{
    ScoreHS_Init();

    int maxDigits = 1;
    for (int i = 0; i < SCORE_HS_MAX; ++i)
    {
        int v = s_hs[i].score;
        if (v < 0) v = 0;

        int d = 0;
        if (v == 0) d = 1;
        else { while (v > 0) { v /= 10; d++; } }

        if (d > maxDigits) maxDigits = d;
    }
    if (maxDigits < 4) maxDigits = 4;
    if (maxDigits > 8) maxDigits = 8;

    for (int i = 0; i < SCORE_HS_MAX; ++i)
    {
        char line[64];
        int pos = 0;

        int rank = i + 1;
        line[pos++] = (char)('0' + (rank / 10));
        line[pos++] = (char)('0' + (rank % 10));
        line[pos++] = ' ';
        line[pos++] = ' ';

        line[pos++] = s_hs[i].initials[0];
        line[pos++] = s_hs[i].initials[1];
        line[pos++] = s_hs[i].initials[2];
        line[pos++] = ' ';
        line[pos++] = ' ';

        int v = s_hs[i].score;
        if (v < 0) v = 0;

        char digits[16];
        int n = 0;
        if (v == 0) digits[n++] = '0';
        else
        {
            while (v > 0 && n < (int)sizeof(digits))
            {
                digits[n++] = (char)('0' + (v % 10));
                v /= 10;
            }
        }

        int pad = maxDigits - n;
        while (pad-- > 0 && pos < (int)sizeof(line) - 1)
            line[pos++] = ' ';

        while (n > 0 && pos < (int)sizeof(line) - 1)
            line[pos++] = digits[--n];

        line[pos] = 0;

        float w = (float)strlen(line) * (6.0f * scale);
        float drawX = x - (w * 0.5f);

        DrawText(drawX, y + (float)i * (12.0f * scale), line, scale, color);
    }
}
