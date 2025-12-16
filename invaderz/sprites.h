#pragma once
#include <stdint.h>

// 4bpp paletted sprite:
// - index 0 = transparent
// - pixels are packed: high nibble = left pixel, low nibble = right pixel
// - row-major, no padding
struct Sprite4
{
    uint16_t        w;
    uint16_t        h;
    const uint8_t* data;   // size = (w*h)/2 bytes for even w
};

enum SpriteId : uint32_t
{
    SPR_PLAYER = 0,
    SPR_PLAYER_BULLET,

    SPR_INVADER_A,
    SPR_INVADER_A2,        // Animation frame 2
    SPR_INVADER_B,
    SPR_INVADER_B2,        // Animation frame 2
    SPR_INVADER_C,
    SPR_INVADER_C2,        // Animation frame 2

    SPR_EBULLET_ZIG,
    SPR_EBULLET_PLUNGER,
    SPR_EBULLET_ROLL,

    SPR_UFO,

    SPR_BARRIER_TILE,

    SPR_COUNT
};

// Animation frame definition
struct SpriteAnimFrame
{
    SpriteId spriteId;     // Which sprite to display
    uint32_t durationMs;   // How long to show this frame (milliseconds)
};

// Animation sequence definition
struct SpriteAnim
{
    const SpriteAnimFrame* frames;
    uint32_t frameCount;
    bool loop;             // True = loop forever, False = play once
};

struct SpritePack4
{
    const uint32_t* paletteARGB;   // 16 entries, 0xAARRGGBB
    const Sprite4* sprites;        // SPR_COUNT entries
    uint32_t        spriteCount;
    const SpriteAnim* animations;  // Optional animation definitions
    uint32_t animCount;            // Number of animations
};

// Standard animation indices for sprite packs
enum AnimId : uint32_t
{
    ANIM_INVADER_A = 0,
    ANIM_INVADER_B,
    ANIM_INVADER_C,

    ANIM_COUNT
};