#pragma once

#include "DspUtil.h"
#include <array>

// CAPTURE: an ephemeral sustainer and looper with up to 30 seconds of stereo.
// Hold to record, release to play. A short hold becomes a soft-edged pad, a longer one a loop.
// Tap to stop and clear. With overdub on, holding during playback layers onto the loop.
namespace pp
{
class Capture
{
public:
    enum State { Empty = 0, Recording, Pad, Loop, Overdubbing };
    static constexpr float kMaxSeconds = 30.0f;
    static constexpr float kPadBelow = 0.7f; // seconds held

    void prepare (float sampleRate)
    {
        fs = sampleRate;
        cap = (int) (kMaxSeconds * fs);
        for (auto& b : buf)
            if ((int) b.size() != cap + 4) { b.assign ((size_t) cap + 4, 0.0f); state = Empty; len = 0; }
        fadeIn = 0.0f;
        fadeStep = 1.0f / (0.01f * fs);
    }

    // ---- commands, called on the audio thread ----
    void holdStart (bool overdubAllowed)
    {
        if ((state == Loop || state == Pad) && overdubAllowed && len > 0) { state = Overdubbing; return; }
        recPos = 0; len = 0; state = Recording;
    }
    void holdEnd (float beatSamples, bool quantise)
    {
        if (state == Overdubbing) { state = Loop; return; }
        if (state != Recording) return;
        len = recPos;
        if (len < (int) (0.03f * fs)) { clear(); return; }
        if (quantise && beatSamples > 0.0f && (float) len > beatSamples * 0.75f)
            len = std::min (recPos, std::max ((int) beatSamples, (int) (std::round ((float) len / beatSamples) * beatSamples)));
        const bool pad = (float) recPos / fs < kPadBelow;
        if (pad) startPad(); else startLoop();
    }
    void clear() { state = Empty; len = 0; recPos = 0; fadeIn = 0.0f; }

    // Adds playback into out. in is what gets recorded.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            if (state == Recording)
            {
                if (recPos < cap) { buf[0][(size_t) recPos] = inL[i]; buf[1][(size_t) recPos] = inR[i]; ++recPos; }
                else holdEnd (0.0f, false);
                continue;
            }
            if (state == Empty || len <= 0) continue;
            fadeIn = std::min (1.0f, fadeIn + fadeStep);

            float l, r;
            if (state == Pad)
            {
                // two overlapping windowed grains with tiny drifts, so a short snapshot sustains like a pad
                l = r = 0.0f;
                for (auto& gr : grains)
                {
                    gr.pos += gr.rate;
                    gr.age += 1.0f;
                    if (gr.age >= grainLen) newGrain (gr);
                    const float w = std::sin (kPi * gr.age / grainLen);
                    l += readAt (0, gr.pos) * w * w;
                    r += readAt (1, gr.pos) * w * w;
                }
            }
            else
            {
                const int p = playPos;
                l = buf[0][(size_t) p];
                r = buf[1][(size_t) p];
                if (state == Overdubbing)
                {
                    buf[0][(size_t) p] = l * 0.85f + inL[i];
                    buf[1][(size_t) p] = r * 0.85f + inR[i];
                }
                if (++playPos >= len) playPos = 0;
            }
            outL[i] += l * fadeIn;
            outR[i] += r * fadeIn;
        }
        uiState.store (state);
        uiSeconds.store ((float) (state == Recording ? recPos : len) / fs);
        uiPhase.store (state == Loop || state == Overdubbing ? (float) playPos / (float) std::max (1, len)
                       : (state == Pad ? grains[0].age / grainLen : 0.0f));
    }

    bool hasAudio() const { return len > 0 && state != Empty && state != Recording; }
    int length() const { return len; }
    bool isPad() const { return state == Pad; }
    void copyOut (std::vector<float>& l, std::vector<float>& r) const
    {
        l.assign (buf[0].begin(), buf[0].begin() + len);
        r.assign (buf[1].begin(), buf[1].begin() + len);
    }
    // restore a saved capture, stopped or playing
    void load (const std::vector<float>& l, const std::vector<float>& r, bool pad)
    {
        len = (int) std::min ({ l.size(), r.size(), (size_t) cap });
        if (len <= 0) { clear(); return; }
        std::copy (l.begin(), l.begin() + len, buf[0].begin());
        std::copy (r.begin(), r.begin() + len, buf[1].begin());
        recPos = len;
        fadeIn = 0.0f;
        if (pad) startPad(); else startLoop (false);
    }

    std::atomic<int> uiState { 0 };
    std::atomic<float> uiSeconds { 0.0f }, uiPhase { 0.0f };

private:
    struct Grain { float pos = 0.0f, age = 0.0f, rate = 1.0f; };

    float readAt (int c, float pos) const
    {
        const float wrapped = pos - std::floor (pos / (float) len) * (float) len;
        const int a = (int) wrapped;
        const float f = wrapped - (float) a;
        const int b = a + 1 >= len ? 0 : a + 1;
        return buf[(size_t) c][(size_t) a] + (buf[(size_t) c][(size_t) b] - buf[(size_t) c][(size_t) a]) * f;
    }

    void newGrain (Grain& g)
    {
        g.age = 0.0f;
        g.pos = rng.uni() * (float) len;
        g.rate = 1.0f + rng.next() * 0.003f;
    }

    void startPad()
    {
        state = Pad;
        grainLen = std::max (0.05f * fs, 0.9f * (float) len);
        grains[0] = {}; grains[1] = {};
        newGrain (grains[0]);
        newGrain (grains[1]);
        grains[1].age = grainLen * 0.5f;
        fadeIn = 0.0f;
    }

    void startLoop (bool blendSeam = true)
    {
        // blend the end into the start so the loop point never clicks
        const int xf = std::min (len / 4, (int) (0.012f * fs));
        if (blendSeam && xf > 4)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < xf; ++i)
                {
                    const float t = (float) i / (float) xf;
                    auto& b = buf[(size_t) c];
                    b[(size_t) i] = b[(size_t) i] * t + b[(size_t) (len - xf + i)] * (1.0f - t);
                }
            len -= xf;
        }
        state = Loop;
        playPos = 0;
    }

    float fs = 48000.0f;
    std::array<std::vector<float>, 2> buf;
    int cap = 0, len = 0, recPos = 0, playPos = 0;
    State state = Empty;
    Grain grains[2];
    float grainLen = 2400.0f, fadeIn = 0.0f, fadeStep = 0.001f;
    Rng rng;
};
} // namespace pp
