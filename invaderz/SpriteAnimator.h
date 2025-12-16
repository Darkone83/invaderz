#pragma once
#include "sprites.h"

// Helper class to manage sprite animation playback
class SpriteAnimator
{
public:
    SpriteAnimator()
        : m_anim(nullptr)
        , m_currentFrame(0)
        , m_elapsedMs(0)
        , m_isPlaying(false)
    {
    }

    // Start playing an animation
    void Play(const SpriteAnim* anim)
    {
        if (anim && anim->frameCount > 0)
        {
            m_anim = anim;
            m_currentFrame = 0;
            m_elapsedMs = 0;
            m_isPlaying = true;
        }
    }

    // Stop the animation
    void Stop()
    {
        m_isPlaying = false;
    }

    // Reset to first frame
    void Reset()
    {
        m_currentFrame = 0;
        m_elapsedMs = 0;
    }

    // Update animation (call once per frame with delta time)
    void Update(uint32_t deltaMs)
    {
        if (!m_isPlaying || !m_anim || m_anim->frameCount == 0)
            return;

        m_elapsedMs += deltaMs;

        const SpriteAnimFrame& frame = m_anim->frames[m_currentFrame];

        // Check if we need to advance to next frame
        while (m_elapsedMs >= frame.durationMs)
        {
            m_elapsedMs -= frame.durationMs;
            m_currentFrame++;

            // Handle end of animation
            if (m_currentFrame >= m_anim->frameCount)
            {
                if (m_anim->loop)
                {
                    m_currentFrame = 0;  // Loop back to start
                }
                else
                {
                    m_currentFrame = m_anim->frameCount - 1;  // Stay on last frame
                    m_isPlaying = false;
                    break;
                }
            }
        }
    }

    // Get the current sprite ID to render
    SpriteId GetCurrentSprite() const
    {
        if (!m_anim || m_anim->frameCount == 0)
            return SPR_PLAYER;  // Default fallback

        return m_anim->frames[m_currentFrame].spriteId;
    }

    // Check if animation is currently playing
    bool IsPlaying() const { return m_isPlaying; }

    // Get current frame index
    uint32_t GetCurrentFrame() const { return m_currentFrame; }

private:
    const SpriteAnim* m_anim;
    uint32_t m_currentFrame;
    uint32_t m_elapsedMs;
    bool m_isPlaying;
};