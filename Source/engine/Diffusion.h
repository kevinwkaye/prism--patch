#pragma once

#include "Module.h"
#include "Reverb.h"
#include <memory>

// DIFFUSION: Cascade, Reels, Space, Collage, Reverse. The green Time and Amount knobs.
// Effect Vol here sets only the wet level. With trails on, the tails keep ringing after bypass.
namespace pp
{
class Diffusion
{
public:
    enum { Cascade = 0, Reels, Space, Collage, Reverse, Off };

    Diffusion() : verbA (std::make_unique<Reverb>()), verbB (std::make_unique<Reverb>()) {}

    void prepare (float sampleRate)
    {
        fs = sampleRate;
        swap.prepare (fs);
        for (int c = 0; c < 2; ++c)
        {
            auto& k = ch[c];
            k.d.init (nextPow2 ((int) (4.2f * fs)));
            k.lp.setCutoff (2500.0f, fs);
            k.hp.setCutoff (150.0f, fs);
            k.lfo.seed (0xACE1u + 99u * (uint32_t) c, 0.3f * c);
            k.flutter.seed (0x7777u + 13u * (uint32_t) c, 0.6f * c);
            k.fb = 0.0f;
            k.dc.prepare (fs);
        }
        verbA->prepare (fs);
        verbB->prepare (fs);
        verbA->setMode (0);
        verbB->setMode (1);
        timeS.setTime (0.15f, fs);    // delay time glides, so twisting Time bends the pitch like a BBD
        collageS.setTime (0.06f, fs);
        aS.setTime (0.02f, fs);
        revPhase = 0.0f;
        dropGain = 1.0f; dropTimer = 0;
        dbl = 0.0f; dblLen = 0.0f;
    }

    void reset()
    {
        for (auto& k : ch) { k.d.clear(); k.fb = 0.0f; k.lp.reset(); k.hp.reset(); k.dc.reset(); }
        verbA->clear(); verbB->clear();
        revPhase = 0.0f;
        dbl = 0.0f; dblLen = 0.0f;
    }

    // Returns the dry signal plus the wet effect
    Stereo process (Stereo in, int fx, const ModuleControls& m, const Shared& s)
    {
        const float fade = swap.next (fx);
        if (swap.switched) reset();
        const int e = swap.current;
        if (e == Off) return in;

        const float amt = aS.next (m.amount), drift = m.drift, knob = m.knob;
        const float wetVol = dbToGain (m.volDb);
        Stereo wet {}, out {};

        switch (e)
        {
            case Cascade:
            case Reels:
            {
                // Time: short slapback to quarter-note taps. With a tempo, note subdivisions.
                float secs;
                if (s.timeLocked)
                {
                    static const float sub[6] = { 0.125f, 1.0f / 6.0f, 0.25f, 1.0f / 3.0f, 0.375f, 0.5f };
                    secs = beatSeconds (s) * 2.0f * sub[std::min (5, (int) (knob * 6.0f))];
                }
                else secs = 0.04f * std::pow (12.5f, knob);
                const float dly = timeS.next (secs * fs);
                const bool reels = e == Reels;
                const float fbAmt = amt * (reels ? 1.08f : 1.12f); // past 1.0 it self-oscillates, held by the soft clip

                if (reels && drift > 0.01f)
                {
                    // worn tape: random dropouts in the repeats
                    if (--dropTimer <= 0)
                    {
                        dropTimer = (int) ((0.08f + 0.6f * rng.uni()) * fs);
                        dropTarget = rng.uni() < drift * 0.4f ? 0.35f + 0.4f * rng.uni() : 1.0f;
                    }
                    dropGain += (dropTarget - dropGain) * 0.002f;
                }
                else dropGain = 1.0f;

                float taps[2];
                for (int c = 0; c < 2; ++c)
                {
                    auto& k = ch[c];
                    float mod;
                    if (reels) mod = (k.lfo.next (0.7f, fs, 0.5f) * 0.00025f + k.flutter.next (6.0f, fs, 0.3f) * 0.00008f) * fs * (1.0f + 4.0f * drift);
                    else mod = k.lfo.next (0.4f, fs, drift) * (0.0003f + drift * 0.002f) * fs;
                    // Cascade leans to one side, Reels ping-pongs
                    const float side = reels ? 1.0f : (c == 0 ? 1.0f : 1.0f + 0.018f);
                    taps[c] = k.d.tap (dly * side + mod + (c == 1 && ! reels ? 0.007f * fs : 0.0f));
                }
                for (int c = 0; c < 2; ++c)
                {
                    auto& k = ch[c];
                    float t = reels ? taps[1 - c] : taps[c];
                    if (reels)
                    {
                        k.lp.setCutoff (6500.0f * (1.0f - drift * 0.55f), fs);
                        t = k.lp.process (t);
                        t -= k.hp.process (t);
                        t = std::tanh (t * 1.4f) / 1.4f * dropGain;
                    }
                    else
                    {
                        k.lp.setCutoff (2600.0f * (1.0f - drift * 0.4f), fs);
                        t = k.lp.process (t);
                        t = std::tanh (t * 1.6f) / 1.6f + drift * 0.002f * rng.next();
                    }
                    // Reels feeds both sides into the left head and bounces between them
                    const float input = reels ? (c == 0 ? 0.5f * (in.l + in.r) : 0.0f) : (c == 0 ? in.l : in.r);
                    k.d.push (softClip (k.dc.process (input + t * fbAmt)));
                    (c == 0 ? wet.l : wet.r) = (reels ? taps[c] : t) * (c == 1 && ! reels ? 0.75f : 1.0f);
                }
                out = in + wet * (wetVol * (0.55f + 0.45f * std::min (1.0f, amt * 2.0f)));
                break;
            }

            case Space:
            {
                // Time blends across five rooms by crossfading two reverbs
                const float t = clampf (knob, 0.0f, 1.0f) * (Reverb::kRooms - 1);
                const int lo = std::min (Reverb::kRooms - 2, (int) t);
                const float frac = t - (float) lo;
                const int even = lo % 2 == 0 ? lo : lo + 1, odd = lo % 2 == 0 ? lo + 1 : lo;
                const float wEven = lo % 2 == 0 ? 1.0f - frac : frac;
                verbA->setMode (even);
                verbB->setMode (odd);
                verbA->setModScale (1.0f + drift * 5.0f);
                verbB->setModScale (1.0f + drift * 5.0f);
                float al, ar, bl, br;
                verbA->process (in.l, in.r, al, ar);
                verbB->process (in.l, in.r, bl, br);
                const float ge = std::sqrt (wEven), go = std::sqrt (1.0f - wEven);
                wet = { (al * ge + bl * go) * 2.2f, (ar * ge + br * go) * 2.2f };
                // Amount is the wet and dry mix; fully clockwise is all reverb
                out = in * std::sqrt (1.0f - amt) + wet * (wetVol * std::sqrt (amt));
                break;
            }

            case Collage:
            {
                // A looping delay whose buffer is rewritten as Time moves: knob moves bend and fold the loop
                float secs;
                if (s.timeLocked)
                {
                    static const float sub[7] = { 0.0625f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
                    secs = beatSeconds (s) * sub[std::min (6, (int) (knob * 7.0f))];
                }
                else secs = 0.03f * std::pow (50.0f, knob);
                const float len = collageS.next (std::min (secs, 3.9f) * fs);

                // Drift drops in random double-speed passes
                if (dblLen <= 0.0f && drift > 0.01f && rng.uni() < drift * 2.0f / fs * 2.0f) { dblLen = len; dbl = 0.0f; }
                float extra = 0.0f;
                if (dblLen > 0.0f) { dbl += 1.0f; extra = dbl; if (dbl >= dblLen * 0.5f) dblLen = 0.0f; }

                for (int c = 0; c < 2; ++c)
                {
                    auto& k = ch[c];
                    const float mod = k.lfo.next (1.1f, fs, 1.0f) * drift * 0.001f * fs;
                    const float t = k.d.tap (std::max (8.0f, len * (c == 0 ? 1.0f : 1.01f) - extra + mod));
                    const float input = c == 0 ? in.l : in.r;
                    k.d.push (softClip (k.dc.process (input + t * amt * 1.05f)));
                    (c == 0 ? wet.l : wet.r) = t;
                }
                out = in + wet * (wetVol * 0.9f);
                break;
            }

            default: // Reverse
            {
                // Backwards tape: grains read the recent past in reverse at a speed set by Time
                const float speed = std::pow (2.0f, (knob - 0.5f) * 2.0f) * (1.0f + drift * 0.02f * ch[0].lfo.next (0.6f, fs, 0.7f));
                const float seg = (s.timeLocked ? beatSeconds (s) : 0.45f) * fs;
                revPhase += 1.0f / seg;
                revPhase -= std::floor (revPhase);
                const float p2 = revPhase + 0.5f - std::floor (revPhase + 0.5f);
                for (int c = 0; c < 2; ++c)
                {
                    auto& k = ch[c];
                    k.d.push (c == 0 ? in.l : in.r);
                    // delay grows by (1 + speed) per sample through each grain: playback runs backwards
                    const float g1 = k.d.tap (8.0f + revPhase * seg * (1.0f + speed));
                    const float g2 = k.d.tap (8.0f + p2 * seg * (1.0f + speed));
                    const float w1 = std::sin (kPi * revPhase), w2 = std::sin (kPi * p2);
                    (c == 0 ? wet.l : wet.r) = g1 * w1 * w1 + g2 * w2 * w2;
                }
                out = in * std::sqrt (1.0f - amt) + wet * (wetVol * std::sqrt (amt));
                break;
            }
        }
        return { lerpf (in.l, out.l, fade), lerpf (in.r, out.r, fade) };
    }

private:
    struct Chan
    {
        DelayLine d;
        OnePole lp, hp;
        Lfo lfo, flutter;
        DcBlock dc;
        float fb = 0.0f;
    };
    float fs = 48000.0f;
    Chan ch[2];
    std::unique_ptr<Reverb> verbA, verbB;
    Smoother timeS, collageS, aS;
    SwapFade swap;
    Rng rng;
    float revPhase = 0.0f, dropGain = 1.0f, dropTarget = 1.0f, dbl = 0.0f, dblLen = 0.0f;
    int dropTimer = 0;
};
} // namespace pp
