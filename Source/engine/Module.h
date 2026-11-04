#pragma once

#include "Blocks.h"

// What every module gets each sample, and the small helpers they share.
namespace pp
{
// The four knobs that belong to one module. knob is Tilt, Rate or Time (unused on Texture).
struct ModuleControls
{
    float knob = 0.5f;
    float amount = 0.5f;
    float drift = 0.0f;   // Movement and Diffusion only
    float volDb = 0.0f;   // Effect Vol
};

// Settings shared across the chain
struct Shared
{
    float fs = 48000.0f;
    float bpm = 120.0f;
    bool rateLocked = false;   // Movement Rate snaps to note values of the tempo
    bool timeLocked = false;   // Diffusion Time snaps to note values of the tempo
    float sens = 0.5f;         // Character breakup point, 0 cleaner, 1 hotter
    int filterStyle = 0;       // 0 tilt, 1 low-pass, 2 high-pass
    int inputMode = 0;         // 0 guitar, 1 synth, 2 bass
};

inline float beatSeconds (const Shared& s) { return 60.0f / std::max (20.0f, s.bpm); }

// Rate knob to Hz when free: 0.1 .. 15 Hz
inline float freeRateHz (float knob) { return 0.1f * std::pow (150.0f, clampf (knob, 0.0f, 1.0f)); }

// Rate knob to Hz, free or tempo-synced
inline float rateHz (float knob, const Shared& s)
{
    if (s.rateLocked) return 1.0f / (beatSeconds (s) * Sync::beats (knob));
    return freeRateHz (knob);
}

// Switching effects inside a module dips to the dry signal for a few milliseconds,
// so the swap never clicks.
struct SwapFade
{
    int current = 0, pending = 0;
    float g = 1.0f, step = 0.002f;
    bool switched = false;
    void prepare (float fs) { step = 1.0f / (0.008f * fs); }
    // returns how much of the effect to hear; sets switched when it is time to change
    float next (int wanted)
    {
        switched = false;
        if (wanted != pending) pending = wanted;
        if (pending != current)
        {
            g -= step;
            if (g <= 0.0f) { g = 0.0f; current = pending; switched = true; }
        }
        else if (g < 1.0f)
            g = std::min (1.0f, g + step);
        return g;
    }
};
} // namespace pp
