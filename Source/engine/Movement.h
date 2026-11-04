#pragma once

#include "Module.h"

// MOVEMENT: Doubler, Vibrato, Phaser, Tremolo, Pitch. The yellow Rate and Amount knobs,
// with Drift adding each effect's own kind of instability.
namespace pp
{
class Movement
{
public:
    enum { Doubler = 0, Vibrato, Phaser, Tremolo, Pitch, Off };
    static constexpr int kMaxStages = 12;

    void prepare (float sampleRate)
    {
        fs = sampleRate;
        swap.prepare (fs);
        for (int c = 0; c < 2; ++c)
        {
            auto& k = ch[c];
            k.d.init (nextPow2 ((int) (0.12f * fs)));
            k.d2.init (nextPow2 ((int) (0.12f * fs)));
            k.flutter.seed (0xF00Du + 7u * (uint32_t) c, 0.4f);
            k.shift.prepare (fs);
            k.lfo.seed (0x1234u + 77u * (uint32_t) c, c * 0.25f);
            k.wow.seed (0xBEEFu + 31u * (uint32_t) c, 0.1f + c * 0.37f);
            k.tone.setCutoff (9000.0f, fs);
            k.fb = 0.0f;
        }
        kS.setTime (0.03f, fs);
        aS.setTime (0.02f, fs);
        pitchS.setTime (0.12f, fs); // Pitch glides between settings
        blip = 0.0f; blipTarget = 0.0f; blipTimer = 0;
    }

    void reset()
    {
        for (auto& k : ch) { k.d.clear(); k.d2.clear(); k.shift.clear(); for (auto& a : k.ap) a.reset(); k.fb = 0.0f; k.tone.reset(); }
        blip = blipTarget = 0.0f;
    }

    Stereo process (Stereo in, int fx, const ModuleControls& m, const Shared& s)
    {
        const float fade = swap.next (fx);
        if (swap.switched) reset();
        const int e = swap.current;
        if (e == Off) return in;

        const float knob = kS.next (m.knob), amt = aS.next (m.amount), drift = m.drift;
        const float vol = dbToGain (m.volDb);
        const float hz = rateHz (knob, s);
        float x[2] = { in.l, in.r }, y[2];

        // Doubler's Drift: now and then a brief pitch blip, like a wobbly overdub
        if (e == Doubler && drift > 0.01f)
        {
            if (--blipTimer <= 0)
            {
                blipTarget = blipTarget != 0.0f ? 0.0f : rng.next() * drift * 0.003f * fs;
                blipTimer = (int) ((blipTarget != 0.0f ? 0.09f : 0.25f + 2.5f * (1.0f - drift) * rng.uni()) * fs);
            }
            blip += (blipTarget - blip) * 0.0015f;
        }
        else blip *= 0.999f;

        for (int c = 0; c < 2; ++c)
        {
            auto& k = ch[c];
            const float v = x[c];
            k.d.push (v);
            switch (e)
            {
                case Doubler:
                {
                    // Two overdubbed takes, one each side. Each drifts in time and pitch on its own,
                    // like a real double. Rate sets the gap: tight double to short slapback.
                    const float mono = 0.5f * (x[0] + x[1]);
                    if (c == 0) k.d2.push (mono);
                    const float base = (0.008f + 0.052f * knob) * fs * (c == 0 ? 1.0f : 1.41f) + (c == 0 ? 0.0f : 0.004f * fs);
                    const float wander = k.wow.next (c == 0 ? 0.55f : 0.8f, fs, 1.0f) * 0.0012f * fs
                                       + k.lfo.next (c == 0 ? 4.1f : 3.3f, fs, 1.0f) * 0.00015f * fs;
                    const float tap = k.tone.process (ch[0].d2.tap (base + wander + (c == 0 ? blip : -blip)));
                    y[c] = v * (1.0f - 0.3f * amt) + tap * amt * 1.15f;
                    break;
                }
                case Vibrato:
                {
                    // Drift morphs the sine to a random tape warble and widens the stereo image
                    const float ph = k.lfo.next (hz, fs, drift);
                    const float spread = (c == 0 ? 1.0f : 1.0f - drift * 0.6f);
                    const float depth = amt * (0.0008f + 0.004f / (1.0f + hz)) * fs;
                    y[c] = k.d.tap (0.006f * fs + depth * ph * spread + depth);
                    break;
                }
                case Phaser:
                {
                    const int stages = 2 + 2 * (int) std::lround (amt * 5.0f);
                    const float lfo = 0.5f + 0.5f * k.lfo.next (hz, fs, drift * 0.8f);
                    const float depth = 0.3f + 0.7f * amt;
                    const float fc = 180.0f * std::pow (2.0f, 4.5f * depth * lfo);
                    const float t = std::tan (kPi * std::min (fc, 0.45f * fs) / fs);
                    const float a = (t - 1.0f) / (t + 1.0f);
                    float p = v + k.fb * (0.3f + 0.3f * amt);
                    for (int i = 0; i < stages; ++i) p = k.ap[(size_t) i].process (p, a);
                    k.fb = softClip (p);
                    y[c] = 0.5f * (v + p) * 1.3f;
                    break;
                }
                case Tremolo:
                {
                    // Amount takes it from a soft amp shimmer to a hard square chop near the top.
                    // One LFO for both sides, as on a guitar amp.
                    if (c == 0)
                    {
                        const float wob = drift > 0.0f ? 1.0f + drift * 0.3f * k.wow.next (0.4f, fs, 1.0f) : 1.0f;
                        const float sn = k.lfo.next (hz * wob, fs, drift * 0.4f);
                        const float sq = 1.0f + 7.0f * std::max (0.0f, (amt - 0.6f) / 0.4f);
                        tremShape = std::tanh (sq * sn) / std::tanh (sq);
                        tremDepth = std::min (1.0f, amt * 1.15f) * (1.0f - drift * 0.25f * (0.5f + 0.5f * k.flutter.next (1.3f, fs, 1.0f)));
                    }
                    y[c] = v * (1.0f - tremDepth * (0.5f - 0.5f * tremShape));
                    break;
                }
                default: // Pitch
                {
                    // Rate sets the pitch: -1 octave fully left, none at noon, +1 octave fully right
                    float semis = (pitchS.nextOnce (c, knob) - 0.5f) * 24.0f;
                    if (std::fabs (semis) < 0.4f) semis = 0.0f;
                    const float jitter = drift * 0.01f * k.wow.next (5.0f, fs, 1.0f);
                    float p = k.shift.process (v, std::pow (2.0f, semis / 12.0f) * (1.0f + jitter));
                    if (drift > 0.01f)
                        p = k.crush.process (p, 1.0f - 0.85f * drift, 16.0f - 11.0f * drift);
                    y[c] = lerpf (v, p, amt);
                    break;
                }
            }
        }
        return { lerpf (in.l, y[0] * vol, fade), lerpf (in.r, y[1] * vol, fade) };
    }

private:
    // smooths a value once per sample frame, shared by both channels
    struct FrameSmoother
    {
        Smoother s;
        float last = 0.0f;
        void setTime (float t, float fs) { s.setTime (t, fs); }
        float nextOnce (int channel, float target) { if (channel == 0) last = s.next (target); return last; }
    };
    struct Chan
    {
        DelayLine d, d2;
        PitchShifter shift;
        Crusher crush;
        std::array<Allpass1, kMaxStages> ap;
        Lfo lfo, wow, flutter;
        OnePole tone;
        float fb = 0.0f;
    };
    float fs = 48000.0f;
    Chan ch[2];
    Smoother kS, aS;
    FrameSmoother pitchS;
    SwapFade swap;
    Rng rng;
    float blip = 0.0f, blipTarget = 0.0f;
    int blipTimer = 0;
    float tremShape = 0.0f, tremDepth = 0.0f;
};
} // namespace pp
