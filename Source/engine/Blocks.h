#pragma once

// Building blocks shared by the four modules: anti-aliased saturation, tilt EQ,
// LFOs, envelope followers, all-pass stages and a two-grain pitch shifter.

#include "DspUtil.h"
#include <array>

namespace pp
{
// tanh with first order antiderivative anti-aliasing, so heavy drive stays clean
struct AdaaTanh
{
    float x1 = 0.0f;
    static float F (float x)
    {
        const float ax = std::fabs (x);
        return ax > 12.0f ? ax - 0.693147f : std::log (std::cosh (x));
    }
    float process (float x)
    {
        const float d = x - x1;
        float y;
        if (std::fabs (d) < 1.0e-4f) y = std::tanh (0.5f * (x + x1));
        else y = (F (x) - F (x1)) / d;
        x1 = x;
        return y;
    }
    void reset() { x1 = 0.0f; }
};

// hard-knee fuzz clipper x / (1 + |x|) with the same anti-aliasing trick
struct AdaaFuzz
{
    float x1 = 0.0f;
    static float F (float x) { const float ax = std::fabs (x); return ax - std::log (1.0f + ax); }
    float process (float x)
    {
        const float d = x - x1;
        float y;
        if (std::fabs (d) < 1.0e-4f) { const float m = 0.5f * (x + x1); y = m / (1.0f + std::fabs (m)); }
        else y = (F (x) - F (x1)) / d;
        x1 = x;
        return y;
    }
    void reset() { x1 = 0.0f; }
};

// Tilt EQ: one pivot around 800 Hz. t in -1..1, -1 dark, +1 bright, up to +-9 dB.
struct Tilt
{
    OnePole lp;
    float gLow = 1.0f, gHigh = 1.0f;
    void prepare (float fs) { lp.setCutoff (800.0f, fs); lp.reset(); }
    void set (float t)
    {
        const float db = clampf (t, -1.0f, 1.0f) * 9.0f;
        gLow = dbToGain (-db * 0.5f);
        gHigh = dbToGain (db * 0.5f);
    }
    float process (float x)
    {
        const float l = lp.process (x);
        return l * gLow + (x - l) * gHigh;
    }
    void reset() { lp.reset(); }
};

// Sine to smoothed-random LFO. morph 0 is a sine, 1 is a wandering random line.
struct Lfo
{
    float phase = 0.0f, rnd = 0.0f, rndTarget = 0.0f, rndPrev = 0.0f;
    Rng rng;
    float next (float hz, float fs, float morph = 0.0f)
    {
        phase += hz / fs;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            rndPrev = rndTarget;
            rndTarget = rng.next();
        }
        const float sine = std::sin (kTwoPi * phase);
        if (morph <= 0.001f) return sine;
        // cosine-eased random steps, once per cycle
        const float e = 0.5f - 0.5f * std::cos (kPi * phase);
        rnd = rndPrev + (rndTarget - rndPrev) * e;
        return lerpf (sine, rnd, clampf (morph, 0.0f, 1.0f));
    }
    void seed (uint32_t s, float ph) { rng.s = s; phase = ph; }
};

struct Envelope
{
    float env = 0.0f, att = 0.0f, rel = 0.0f;
    void set (float attackS, float releaseS, float fs)
    {
        att = std::exp (-1.0f / std::max (1.0f, attackS * fs));
        rel = std::exp (-1.0f / std::max (1.0f, releaseS * fs));
    }
    float process (float x)
    {
        const float ax = std::fabs (x);
        env = ax > env ? ax + att * (env - ax) : ax + rel * (env - ax);
        return env;
    }
    void reset() { env = 0.0f; }
};

// First order all-pass, coefficient set from a cutoff
struct Allpass1
{
    float z = 0.0f;
    float process (float x, float a)
    {
        const float y = -a * x + z;
        z = x + a * y;
        return y;
    }
    void reset() { z = 0.0f; }
};

// Two overlapping grains reading a delay line at a new rate. ratio 0.5..2.
struct PitchShifter
{
    DelayLine d;
    float phase = 0.0f, grain = 2400.0f;
    void prepare (float fs) { d.init (nextPow2 ((int) (0.25f * fs))); grain = 0.06f * fs; phase = 0.0f; }
    void clear() { d.clear(); phase = 0.0f; }
    float process (float x, float ratio)
    {
        d.push (x);
        // the read point drifts at (1 - ratio) samples per sample through the grain window
        phase += (1.0f - ratio) / grain;
        phase -= std::floor (phase);
        const float p2 = phase + 0.5f - std::floor (phase + 0.5f);
        const float w1 = std::sin (kPi * phase), w2 = std::sin (kPi * p2);
        return d.tap (4.0f + phase * grain) * w1 * w1 + d.tap (4.0f + p2 * grain) * w2 * w2;
    }
};

// Bit and sample-rate reduction
struct Crusher
{
    float held = 0.0f, ph = 0.0f;
    float process (float x, float rateRatio, float bits)
    {
        ph += rateRatio;
        if (ph >= 1.0f) { ph -= std::floor (ph); held = x; }
        const float q = std::pow (2.0f, bits);
        return std::round (held * q) / q;
    }
};

// Tempo-friendly time choices used by Rate and Time when synced
struct Sync
{
    // beats per cycle, from slowest to fastest
    static constexpr int kSteps = 12;
    static float beats (float v01)
    {
        static const float b[kSteps] = { 8.0f, 4.0f, 3.0f, 2.0f, 1.5f, 1.0f, 0.75f, 2.0f / 3.0f, 0.5f, 1.0f / 3.0f, 0.25f, 0.125f };
        return b[std::min (kSteps - 1, std::max (0, (int) std::lround (v01 * (kSteps - 1))))];
    }
};
} // namespace pp
