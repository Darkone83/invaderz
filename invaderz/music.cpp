#include "music.h"
#include <xtl.h>
#include <string.h>
#include <stdlib.h>

// RXDK/XDK: DirectSound types live in xtl headers.
static LPDIRECTSOUND8 s_ds = NULL;

// ---------------------- MUSIC (streaming) ----------------------
static LPDIRECTSOUNDBUFFER s_buf = NULL;
static HANDLE              s_file = INVALID_HANDLE_VALUE;

static DWORD  s_dataOffset = 0;
static DWORD  s_dataSize = 0;
static DWORD  s_dataPos = 0;

static WAVEFORMATEX s_wfx;
static bool   s_ready = false;
static bool   s_playing = false;

// Streaming buffer
static DWORD s_bufBytes = 0;
static DWORD s_writeCursor = 0;

// Startup squelch / volume ramp (integer-only, RXDK-safe)
static LONG s_targetVol = DSBVOLUME_MAX;
static LONG s_curVol = DSBVOLUME_MAX;
static int  s_rampLeft = 0;

// Keep some safety margin ahead of play cursor
static const DWORD STREAM_CHUNK_BYTES = (32 * 1024);
static const DWORD STREAM_BUF_BYTES = (128 * 1024);

// ---------------------- SFX (loaded) ----------------------
struct SfxSlot
{
    BYTE* data;
    DWORD        bytes;
    WAVEFORMATEX wfx;
};

static SfxSlot s_sfx[SFX_MAX];

// Voice pool for overlap (no DuplicateSoundBuffer)
static const int SFX_VOICES = 12;

struct SfxVoice
{
    LPDIRECTSOUNDBUFFER buf;
    int                slot;
    DWORD              bytes;
    WAVEFORMATEX       wfx;
};

static SfxVoice s_voice[SFX_VOICES];

// --------------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------------
static DWORD AlignDown(DWORD v, DWORD align)
{
    if (align == 0) return v;
    return v - (v % align);
}

static DWORD ReadU32(HANDLE f)
{
    DWORD v = 0, br = 0;
    ReadFile(f, &v, 4, &br, NULL);
    return v;
}

static bool ReadChunkHeader(HANDLE f, DWORD& outId, DWORD& outSize)
{
    DWORD br = 0;
    if (!ReadFile(f, &outId, 4, &br, NULL) || br != 4) return false;
    if (!ReadFile(f, &outSize, 4, &br, NULL) || br != 4) return false;
    return true;
}

// Minimal PCM WAV parser (RIFF/WAVE, fmt , data)
static bool ParseWav(HANDLE f, WAVEFORMATEX& outFmt, DWORD& outDataOffset, DWORD& outDataSize)
{
    SetFilePointer(f, 0, NULL, FILE_BEGIN);

    DWORD riff = ReadU32(f);
    ReadU32(f); // riff size
    DWORD wave = ReadU32(f);

    if (riff != 'FFIR' || wave != 'EVAW')
        return false;

    bool gotFmt = false;
    bool gotData = false;

    DWORD id = 0, size = 0;
    while (ReadChunkHeader(f, id, size))
    {
        DWORD here = SetFilePointer(f, 0, NULL, FILE_CURRENT);

        if (id == ' tmf')
        {
            if (size < 16) return false;

            ZeroMemory(&outFmt, sizeof(outFmt));
            DWORD br = 0;
            ReadFile(f, &outFmt.wFormatTag, 2, &br, NULL);
            ReadFile(f, &outFmt.nChannels, 2, &br, NULL);
            ReadFile(f, &outFmt.nSamplesPerSec, 4, &br, NULL);
            ReadFile(f, &outFmt.nAvgBytesPerSec, 4, &br, NULL);
            ReadFile(f, &outFmt.nBlockAlign, 2, &br, NULL);
            ReadFile(f, &outFmt.wBitsPerSample, 2, &br, NULL);

            if (size > 16)
                SetFilePointer(f, size - 16, NULL, FILE_CURRENT);

            gotFmt = true;
        }
        else if (id == 'atad')
        {
            outDataOffset = here;
            outDataSize = size;
            SetFilePointer(f, size, NULL, FILE_CURRENT);
            gotData = true;
        }
        else
        {
            SetFilePointer(f, size, NULL, FILE_CURRENT);
        }

        if (size & 1)
            SetFilePointer(f, 1, NULL, FILE_CURRENT);

        if (gotFmt && gotData)
            break;
    }

    if (!gotFmt || !gotData)
        return false;

    // PCM only
    return outFmt.wFormatTag == 1;
}

// --------------------------------------------------------------------------
// DirectSound init (shared)
// --------------------------------------------------------------------------
static bool EnsureDirectSound()
{
    if (s_ds)
        return true;

    if (FAILED(DirectSoundCreate(NULL, &s_ds, NULL)) || !s_ds)
        return false;

    for (int i = 0; i < SFX_MAX; ++i)
    {
        s_sfx[i].data = NULL;
        s_sfx[i].bytes = 0;
        ZeroMemory(&s_sfx[i].wfx, sizeof(WAVEFORMATEX));
    }

    for (int i = 0; i < SFX_VOICES; ++i)
    {
        s_voice[i].buf = NULL;
        s_voice[i].slot = -1;
        s_voice[i].bytes = 0;
        ZeroMemory(&s_voice[i].wfx, sizeof(WAVEFORMATEX));
    }

    return true;
}

// --------------------------------------------------------------------------
// Music stream helpers
// --------------------------------------------------------------------------
static void ClearBufferToSilence()
{
    if (!s_buf || s_bufBytes == 0)
        return;

    void* p1 = NULL; void* p2 = NULL;
    DWORD b1 = 0;    DWORD b2 = 0;

    if (FAILED(s_buf->Lock(0, s_bufBytes, &p1, &b1, &p2, &b2, 0)))
        return;

    if (p1 && b1) memset(p1, 0, b1);
    if (p2 && b2) memset(p2, 0, b2);

    s_buf->Unlock(p1, b1, p2, b2);
}

static void VolumeRamp_Update()
{
    if (!s_buf || !s_playing || s_rampLeft <= 0)
        return;

    LONG cur = s_curVol;
    LONG tgt = s_targetVol;
    LONG delta = tgt - cur;

    LONG step = delta / (LONG)s_rampLeft;
    if (step == 0)
        step = (delta > 0) ? 1 : -1;

    cur += step;
    s_curVol = cur;
    s_rampLeft--;

    s_buf->SetVolume(cur);

    if (s_rampLeft <= 0)
    {
        s_curVol = tgt;
        s_buf->SetVolume(tgt);
    }
}

static DWORD ReadAudioLoop(BYTE* dst, DWORD bytes)
{
    if (!dst || bytes == 0 || s_file == INVALID_HANDLE_VALUE)
        return 0;

    DWORD total = 0;

    while (bytes > 0)
    {
        DWORD remaining = s_dataSize - s_dataPos;
        DWORD toRead = (bytes < remaining) ? bytes : remaining;

        DWORD br = 0;
        SetFilePointer(s_file, s_dataOffset + s_dataPos, NULL, FILE_BEGIN);
        ReadFile(s_file, dst, toRead, &br, NULL);

        if (br < toRead)
            toRead = br;

        dst += toRead;
        bytes -= toRead;
        total += toRead;

        s_dataPos += toRead;
        if (s_dataPos >= s_dataSize)
            s_dataPos = 0;

        if (toRead == 0)
            break;
    }

    return total;
}

static void FillBuffer(DWORD bytes)
{
    if (!s_buf || !s_ready || bytes == 0)
        return;

    bytes = AlignDown(bytes, s_wfx.nBlockAlign);
    if (bytes == 0) return;

    void* p1 = NULL; void* p2 = NULL;
    DWORD b1 = 0;    DWORD b2 = 0;

    if (FAILED(s_buf->Lock(s_writeCursor, bytes, &p1, &b1, &p2, &b2, 0)))
        return;

    if (p1 && b1) ReadAudioLoop((BYTE*)p1, b1);
    if (p2 && b2) ReadAudioLoop((BYTE*)p2, b2);

    s_buf->Unlock(p1, b1, p2, b2);

    s_writeCursor = (s_writeCursor + bytes) % s_bufBytes;
}

// --------------------------------------------------------------------------
// Public API: Music
// --------------------------------------------------------------------------
bool Music_Init(const char* path)
{
    if (s_buf)
    {
        s_buf->Stop();
        s_buf->Release();
        s_buf = NULL;
    }
    if (s_file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(s_file);
        s_file = INVALID_HANDLE_VALUE;
    }

    s_ready = false;
    s_playing = false;

    s_dataOffset = 0;
    s_dataSize = 0;
    s_dataPos = 0;
    s_bufBytes = 0;
    s_writeCursor = 0;

    s_targetVol = DSBVOLUME_MAX;
    s_curVol = DSBVOLUME_MAX;
    s_rampLeft = 0;

    if (!path || !path[0])
        return false;

    if (!EnsureDirectSound())
        return false;

    s_file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (s_file == INVALID_HANDLE_VALUE)
        return false;

    if (!ParseWav(s_file, s_wfx, s_dataOffset, s_dataSize))
    {
        CloseHandle(s_file);
        s_file = INVALID_HANDLE_VALUE;
        return false;
    }

    s_bufBytes = AlignDown(STREAM_BUF_BYTES, s_wfx.nBlockAlign);

    DSBUFFERDESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DSBCAPS_CTRLVOLUME;
    desc.dwBufferBytes = s_bufBytes;
    desc.lpwfxFormat = &s_wfx;

    if (FAILED(s_ds->CreateSoundBuffer(&desc, &s_buf, NULL)) || !s_buf)
    {
        CloseHandle(s_file);
        s_file = INVALID_HANDLE_VALUE;
        return false;
    }

    s_ready = true;

    ClearBufferToSilence();
    FillBuffer(s_bufBytes);

    s_targetVol = DSBVOLUME_MAX;
    s_curVol = -2400; // ~ -24 dB
    s_rampLeft = 12;

    s_buf->SetVolume(s_curVol);
    s_buf->Play(0, 0, DSBPLAY_LOOPING);

    s_playing = true;
    return true;
}

void Music_SetVolume(LONG dsVolume)
{
    s_targetVol = dsVolume;
    s_rampLeft = 8;
}

void Music_Update()
{
    if (!s_ready || !s_buf || !s_playing)
        return;

    DWORD play = 0, write = 0;
    if (FAILED(s_buf->GetCurrentPosition(&play, &write)))
        return;

    VolumeRamp_Update();

    DWORD targetAhead = s_bufBytes / 2;
    DWORD ahead = (s_writeCursor >= play)
        ? (s_writeCursor - play)
        : (s_bufBytes - play + s_writeCursor);

    while (ahead < targetAhead)
    {
        DWORD bytes = AlignDown(STREAM_CHUNK_BYTES, s_wfx.nBlockAlign);
        if (!bytes) break;

        FillBuffer(bytes);

        ahead = (s_writeCursor >= play)
            ? (s_writeCursor - play)
            : (s_bufBytes - play + s_writeCursor);
    }
}

void Music_Shutdown()
{
    s_ready = false;
    s_playing = false;

    if (s_buf)
    {
        s_buf->Stop();
        s_buf->Release();
        s_buf = NULL;
    }

    if (s_file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(s_file);
        s_file = INVALID_HANDLE_VALUE;
    }

    Sfx_UnloadAll();

    if (s_ds)
    {
        s_ds->Release();
        s_ds = NULL;
    }
}

bool Music_IsReady() { return s_ready; }
bool Music_IsPlaying() { return s_playing; }

// --------------------------------------------------------------------------
// Public API: SFX
// --------------------------------------------------------------------------
static void Sfx_FreeSlot(int slot)
{
    if (slot < 0 || slot >= SFX_MAX) return;

    if (s_sfx[slot].data)
    {
        free(s_sfx[slot].data);
        s_sfx[slot].data = NULL;
    }
    s_sfx[slot].bytes = 0;
    ZeroMemory(&s_sfx[slot].wfx, sizeof(WAVEFORMATEX));

    for (int i = 0; i < SFX_VOICES; ++i)
    {
        if (s_voice[i].slot == slot)
        {
            if (s_voice[i].buf)
            {
                s_voice[i].buf->Stop();
                s_voice[i].buf->Release();
                s_voice[i].buf = NULL;
            }
            s_voice[i].slot = -1;
            s_voice[i].bytes = 0;
            ZeroMemory(&s_voice[i].wfx, sizeof(WAVEFORMATEX));
        }
    }
}

void Sfx_UnloadAll()
{
    for (int i = 0; i < SFX_MAX; ++i)
        Sfx_FreeSlot(i);

    for (int i = 0; i < SFX_VOICES; ++i)
    {
        if (s_voice[i].buf)
        {
            s_voice[i].buf->Stop();
            s_voice[i].buf->Release();
            s_voice[i].buf = NULL;
        }
        s_voice[i].slot = -1;
        s_voice[i].bytes = 0;
        ZeroMemory(&s_voice[i].wfx, sizeof(WAVEFORMATEX));
    }
}

static int FindFreeVoice()
{
    for (int i = 0; i < SFX_VOICES; ++i)
    {
        if (!s_voice[i].buf)
            return i;

        DWORD status = 0;
        if (SUCCEEDED(s_voice[i].buf->GetStatus(&status)))
        {
            if ((status & DSBSTATUS_PLAYING) == 0)
                return i;
        }
        else
        {
            s_voice[i].buf->Release();
            s_voice[i].buf = NULL;
            s_voice[i].slot = -1;
            s_voice[i].bytes = 0;
            ZeroMemory(&s_voice[i].wfx, sizeof(WAVEFORMATEX));
            return i;
        }
    }
    return -1;
}

static bool Voice_ConfigForSlot(int voiceIdx, int slot)
{
    SfxVoice& v = s_voice[voiceIdx];
    const SfxSlot& s = s_sfx[slot];

    if (!s.data || s.bytes == 0)
        return false;

    if (v.buf &&
        v.slot == slot &&
        v.bytes == s.bytes &&
        v.wfx.nSamplesPerSec == s.wfx.nSamplesPerSec &&
        v.wfx.nChannels == s.wfx.nChannels &&
        v.wfx.wBitsPerSample == s.wfx.wBitsPerSample &&
        v.wfx.nBlockAlign == s.wfx.nBlockAlign)
    {
        return true;
    }

    if (v.buf)
    {
        v.buf->Stop();
        v.buf->Release();
        v.buf = NULL;
    }

    DSBUFFERDESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DSBCAPS_CTRLVOLUME;
    desc.dwBufferBytes = s.bytes;
    desc.lpwfxFormat = (WAVEFORMATEX*)&s.wfx;

    if (FAILED(s_ds->CreateSoundBuffer(&desc, &v.buf, NULL)) || !v.buf)
        return false;

    void* p1 = NULL; void* p2 = NULL;
    DWORD b1 = 0;    DWORD b2 = 0;

    if (FAILED(v.buf->Lock(0, s.bytes, &p1, &b1, &p2, &b2, 0)))
    {
        v.buf->Release();
        v.buf = NULL;
        return false;
    }

    if (p1 && b1) memcpy(p1, s.data, b1);
    if (p2 && b2) memcpy(p2, s.data + b1, b2);

    v.buf->Unlock(p1, b1, p2, b2);

    v.slot = slot;
    v.bytes = s.bytes;
    v.wfx = s.wfx;
    return true;
}

bool Sfx_Load(int slot, const char* path)
{
    if (slot < 0 || slot >= SFX_MAX || !path || !path[0])
        return false;

    if (!EnsureDirectSound())
        return false;

    Sfx_FreeSlot(slot);

    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return false;

    WAVEFORMATEX fmt;
    DWORD dataOff = 0, dataSize = 0;

    if (!ParseWav(f, fmt, dataOff, dataSize))
    {
        CloseHandle(f);
        return false;
    }

    BYTE* data = (BYTE*)malloc(dataSize);
    if (!data)
    {
        CloseHandle(f);
        return false;
    }

    DWORD br = 0;
    SetFilePointer(f, dataOff, NULL, FILE_BEGIN);
    ReadFile(f, data, dataSize, &br, NULL);
    CloseHandle(f);

    if (br != dataSize)
    {
        free(data);
        return false;
    }

    s_sfx[slot].data = data;
    s_sfx[slot].bytes = dataSize;
    s_sfx[slot].wfx = fmt;
    return true;
}

void Sfx_Play(int slot, LONG volume)
{
    if (slot < 0 || slot >= SFX_MAX)
        return;
    if (!s_sfx[slot].data || s_sfx[slot].bytes == 0)
        return;

    int v = FindFreeVoice();
    if (v < 0)
        return;

    if (!Voice_ConfigForSlot(v, slot))
        return;

    if (volume == 0)
        volume = DSBVOLUME_MAX;

    s_voice[v].buf->SetCurrentPosition(0);
    s_voice[v].buf->SetVolume(volume);
    s_voice[v].buf->Play(0, 0, 0);
}
