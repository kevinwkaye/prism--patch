#pragma once

#include "Module.h"

// TEXTURE: Filter, Squash, Cassette, Broken, Interference. The blue Amount knob.
namespace pp
{
class Texture
{
public:
    enum { Filter = 0, Squash, Cassette, Broken, Interference, Off };
    enum { StyleTilt = 0, StyleLowPass, StyleHighPass };

    void prepare (float sampleRate)
    {
        fs = sampleRate;
        swap.prepare (fs);
        for (int c = 0; c < 2; ++c)
        {
            auto& k = ch[c];
            k.d.init (nextPow2 ((int) (0.25f * fs)));
            k.wow.seed (0x5150u, 0.0f);          // one tape transport for both sides
            k.flutter.seed (0x2112u, 0.0f);
            k.fm.seed (0x8086u + (uint32_t) c, 0.2f * c);
            k.dc.prepare (fs);
            k.hiss.setCutoff (6000.0f, fs);
        }
        comp.set (0.002f, 0.09f, fs);
        aS.setTime (0.02f, fs);
        dropGain = 1.0f; dropTimer = 0;
        brokenTimer = (int) fs; brokenRamp = 0.0f; brokenDepth = 0.0f; brokenPhase = 0.0f;
        glitchLeft = 0; glitchLen = 0; packetTimer = 0; packetGain = 1.0f; crackle = 0.0f;
        bufIdx = 0;
        for (auto& b : glitchBuf) b.assign ((size_t) (0.08f * fs) + 4, 0.0f);
    }

    void reset()
    {
        for (auto& k : ch) { k.d.clear(); k.svf.reset(); k.svf2.reset(); k.sat.reset(); k.lp.reset(); k.dc.reset(); k.hiss.reset(); }
        comp.reset();
        glitchLeft = 0;
    }

    Stereo process (Stereo in, int fx, const ModuleControls& m, const Shared& s)
    {
        const float fade = swap.next (fx);
        if (swap.switched) reset();
        const int e = swap.current;
        if (e == Off) return in;

        const float amt = aS.next (m.amount);
        const float vol = dbToGain (m.volDb);
        float x[2] = { in.l, in.r }, y[2] = { in.l, in.r };

        switch (e)
        {
            case Filter:
            {
                for (int c = 0; c < 2; ++c)
                {
                    auto& k = ch[c];
                    if (s.filterStyle == StyleLowPass)
                    {
                        // neutral fully clockwise, turn left to cut highs
                        if (amt > 0.995f) { y[c] = x[c]; continue; }
                        k.svf.set (60.0f * std::pow (20000.0f / 60.0f, amt), 0.75f, fs);
                        y[c] = k.svf.lp (x[c]);
                    }
                    else if (s.filterStyle == StyleHighPass)
                    {
                        // neutral fully anticlockwise, turn right to cut lows
                        if (amt < 0.005f) { y[c] = x[c]; continue; }
                        k.svf.set (20.0f * std::pow (400.0f, amt), 0.75f, fs);
                        y[c] = k.svf.hp (x[c]);
                    }
                    else
                    {
                        // tilt: neutral at noon, left cuts highs, right cuts lows
                        const float t = (amt - 0.5f) * 2.0f;
                        if (std::fabs (t) < 0.02f) { y[c] = x[c]; continue; }
                        if (t < 0.0f) { k.svf.set (20000.0f * std::pow (0.01f, -t), 0.707f, fs); y[c] = k.svf.lp (x[c]); }
                        else { k.svf.set (20.0f * std::pow (200.0f, t), 0.707f, fs); y[c] = k.svf.hp (x[c]); }
                    }
                }
                break;
            }

            case Squash:
            {
                // compression that tips into overdrive towards the top of the knob
                const float det = comp.process (std::max (std::fabs (x[0]), std::fabs (x[1])));
                const float thr = dbToGain (-6.0f - 30.0f * amt), ratio = 2.0f + 18.0f * amt;
                float gr = 1.0f;
                if (det > thr) gr = std::pow (det / thr, 1.0f / ratio - 1.0f);
                const float makeup = dbToGain (18.0f * amt * (1.0f - 1.0f / ratio) * 0.7f);
                const float od = amt > 0.7f ? 1.0f + (amt - 0.7f) * 12.0f : 1.0f;
                for (int c = 0; c < 2; ++c)
                {
                    const float v = x[c] * gr * makeup;
                    y[c] = od > 1.0f ? ch[c].sat.process (v * od) / std::tanh (od) * 0.8f : v;
                }
                break;
            }

            case Cassette:
            {
                // flavours of tape across one knob: wow, flutter, saturation, darkening, hiss, dropouts
                if (amt > 0.65f)
                {
                    if (--dropTimer <= 0)
                    {
                        dropTimer = (int) ((0.06f + 0.5f * rng.uni()) * fs);
                        dropTarget = rng.uni() < (amt - 0.65f) * 1.2f ? 0.45f : 1.0f;
                    }
                    dropGain += (dropTarget - dropGain) * 0.003f;
                }
                else dropGain = 1.0f;
                const float wowS = ch[0].wow.next (0.55f, fs, 0.6f), flS = ch[0].flutter.next (7.0f, fs, 0.4f);
                for (int c = 0; c < 2; ++c)
                {
                    auto& k = ch[c];
                    k.d.push (x[c]);
                    const float mod = (wowS * amt * 0.0009f + flS * amt * 0.00012f) * fs;
                    float v = k.d.tap (0.005f * fs + mod + (c == 1 ? 0.00004f * fs : 0.0f));
                    const float drive = 1.0f + 2.5f * amt;
                    v = k.sat.process (v * drive) / std::tanh (drive);
                    k.svf.set (15000.0f * std::pow (0.25f, amt), 0.6f, fs);
                    v = k.svf.lp (v);
                    v += k.hiss.process (rng.next()) * 0.012f * amt * amt;
                    y[c] = v * dropGain;
                }
                break;
            }

            case Broken:
            {
                // a failing motor: periodic pitch drops, plus wobbling level and speed
                if (--brokenTimer <= 0)
                {
                    brokenTimer = (int) ((1.6f - 1.1f * amt) * (0.6f + 0.8f * rng.uni()) * fs);
                    brokenRamp = 0.0f;
                    brokenDepth = amt * (0.006f + 0.02f * rng.uni()) * fs;
                }
                brokenRamp = std::min (1.0f, brokenRamp + 1.0f / (0.25f * fs));
                // the drop: slow down fast, recover a little slower
                const float env = brokenRamp < 0.35f ? brokenRamp / 0.35f : 1.0f - (brokenRamp - 0.35f) / 0.65f;
                const float drop = brokenDepth * env * env;
                for (int c = 0; c < 2; ++c)
                {
                    auto& k = ch[c];
                    k.d.push (x[c]);
                    const float fm = k.fm.next (3.0f + 2.0f * amt, fs, 0.8f) * amt * 0.0012f * fs;
                    float v = k.d.tap (0.03f * fs + drop + fm);
                    const float am = 1.0f - amt * 0.5f * (0.5f + 0.5f * k.wow.next (9.0f, fs, 0.9f));
                    y[c] = v * am;
                }
                break;
            }

            default: // Interference
            {
                // telecom glitches and radio static, layered in as Amount rises
                const float a2 = amt * amt;
                // packet loss
                if (--packetTimer <= 0)
                {
                    packetTimer = (int) ((0.01f + 0.03f * rng.uni()) * fs);
                    packetGain = rng.uni() < a2 * 0.35f ? 0.0f : 1.0f;
                }
                // stutter: capture a short grain and repeat it a few times
                if (glitchLeft <= 0 && rng.uni() < amt * 2.5f / fs)
                {
                    glitchLen = (int) ((0.02f + 0.05f * rng.uni()) * fs);
                    glitchLeft = glitchLen * (2 + (int) (rng.uni() * 4.0f));
                    bufIdx = 0;
                }
                // crackle
                if (rng.uni() < amt * 40.0f / fs) crackle = rng.next() * 0.5f * amt;
                crackle *= 0.9f;
                const float sweep = 0.5f + 0.5f * ch[0].fm.next (0.13f, fs, 1.0f);
                for (int c = 0; c < 2; ++c)
                {
                    auto& k = ch[c];
                    float v = x[c];
                    // narrow the band like a phone line
                    k.svf.set (1200.0f * std::pow (2.0f, (sweep - 0.5f) * 2.0f), 0.9f, fs);
                    const float band = k.svf.bpNorm (v);
                    v = lerpf (v, band * 1.4f, std::min (1.0f, amt * 0.9f));
                    if (glitchLeft > 0)
                    {
                        const int pos = (glitchLen * 8 - glitchLeft) % std::max (1, glitchLen);
                        if (bufIdx < glitchLen) glitchBuf[c][(size_t) bufIdx] = v;
                        v = glitchBuf[c][(size_t) std::min (pos, (int) glitchBuf[c].size() - 1)];
                    }
                    // tuned static
                    k.svf2.set (2500.0f * (0.5f + sweep), 2.5f, fs);
                    const float stat = k.svf2.bpNorm (rng.next()) * 0.06f * a2;
                    y[c] = (v * packetGain + stat + crackle) ;
                }
                if (glitchLeft > 0) { --glitchLeft; ++bufIdx; }
                break;
            }
        }
        return { lerpf (in.l, y[0] * vol, fade), lerpf (in.r, y[1] * vol, fade) };
    }

private:
    struct Chan
    {
        DelayLine d;
        Svf svf, svf2;
        AdaaTanh sat;
        OnePole lp, hiss;
        Lfo wow, flutter, fm;
        DcBlock dc;
    };
    float fs = 48000.0f;
    Chan ch[2];
    Envelope comp;
    Smoother aS;
    SwapFade swap;
    Rng rng;
    float dropGain = 1.0f, dropTarget = 1.0f;
    int dropTimer = 0;
    int brokenTimer = 0;
    float brokenRamp = 0.0f, brokenDepth = 0.0f, brokenPhase = 0.0f;
    int glitchLeft = 0, glitchLen = 0, packetTimer = 0, bufIdx = 0;
    float packetGain = 1.0f, crackle = 0.0f;
    std::array<std::vector<float>, 2> glitchBuf;
};
} // namespace pp
