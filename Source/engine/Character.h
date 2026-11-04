#pragma once

#include "Module.h"

// CHARACTER: Drive, Sweeten, Fuzz, Howl, Swell. The red Tilt and Amount knobs.
// Tilt sets tone and also leans on each saturator's bias and voicing.
namespace pp
{
class Character
{
public:
    enum { Drive = 0, Sweeten, Fuzz, Howl, Swell, Off };

    void prepare (float sampleRate)
    {
        fs = sampleRate;
        swap.prepare (fs);
        for (int c = 0; c < 2; ++c)
        {
            ch[c] = {};
            ch[c].tiltIn.prepare (fs);
            ch[c].tiltOut.prepare (fs);
            ch[c].dc.prepare (fs);
            ch[c].post.lowpass (8000.0f, 0.707f, fs);
            ch[c].gate.set (0.002f, 0.08f, fs);
        }
        comp.set (0.005f, 0.12f, fs);
        howlEnv.set (0.004f, 0.18f, fs);
        swFast.set (0.001f, 0.03f, fs);
        swSlow.set (0.03f, 0.3f, fs);
        kS.setTime (0.02f, fs);
        aS.setTime (0.02f, fs);
        swellGain = 1.0f;
        lockout = 0;
    }

    void reset()
    {
        for (auto& c : ch) { c.sat.reset(); c.fz.reset(); c.fz2.reset(); c.svf.reset(); c.post.reset(); c.tiltIn.reset(); c.tiltOut.reset(); c.dc.reset(); }
        comp.reset(); howlEnv.reset(); swFast.reset(); swSlow.reset();
        swellGain = 1.0f;
    }

    Stereo process (Stereo in, int fx, const ModuleControls& m, const Shared& s)
    {
        const float fade = swap.next (fx);
        if (swap.switched) reset();
        const int e = swap.current;
        if (e == Off) return in;

        const float knob = kS.next (m.knob), amt = aS.next (m.amount);
        const float tilt = knob * 2.0f - 1.0f;
        // Sensitivity moves the breakup point by up to 12 dB either way
        const float sensGain = dbToGain ((s.sens - 0.5f) * 24.0f);
        const float vol = dbToGain (m.volDb);

        float x[2] = { in.l, in.r };
        float y[2];

        if (e == Sweeten)
        {
            const float det = comp.process (std::max (std::fabs (x[0]), std::fabs (x[1])) * sensGain);
            const float thr = dbToGain (-12.0f - 18.0f * amt), ratio = 1.0f + 5.0f * amt;
            float gr = 1.0f;
            if (det > thr) gr = std::pow (det / thr, 1.0f / ratio - 1.0f);
            const float makeup = dbToGain (6.0f * amt);
            for (int c = 0; c < 2; ++c)
            {
                auto& k = ch[c];
                k.tiltOut.set (tilt * 0.6f + 0.25f * amt);
                const float drive = 1.0f + 3.0f * amt * sensGain;
                const float v = x[c] * gr * makeup;
                y[c] = k.tiltOut.process (lerpf (v, k.sat.process (v * drive) / std::tanh (drive), 0.4f + 0.4f * amt));
            }
        }
        else if (e == Swell)
        {
            // envelope-triggered swells: each new note restarts the fade-in
            const float peak = std::max (std::fabs (x[0]), std::fabs (x[1]));
            const float f = swFast.process (peak), sl = swSlow.process (peak);
            const float thr = dbToGain (-30.0f - 25.0f * s.sens);
            if (lockout > 0) --lockout;
            if (f > thr && f > sl * 1.7f && lockout == 0) { swellGain = 0.0f; lockout = (int) (0.06f * fs); }
            const float attack = 0.03f + 1.6f * std::pow (amt, 1.5f), decay = 0.2f + 1.8f * amt;
            if (f > thr * 0.5f) swellGain = std::min (1.0f, swellGain + 1.0f / (attack * fs));
            else swellGain *= std::exp (-1.0f / (decay * fs));
            const float g = swellGain * swellGain;
            for (int c = 0; c < 2; ++c) { ch[c].tiltOut.set (tilt); y[c] = ch[c].tiltOut.process (x[c] * g); }
        }
        else
        {
            const float env = howlEnv.process (std::max (std::fabs (x[0]), std::fabs (x[1])));
            for (int c = 0; c < 2; ++c)
            {
                auto& k = ch[c];
                float v = x[c] * sensGain;
                if (e == Drive)
                {
                    // warm preamp to full overdrive, slightly asymmetric like a tube stage
                    k.tiltIn.set (tilt * 0.7f);
                    const float g = std::pow (40.0f, amt);
                    const float bias = 0.12f;
                    v = k.tiltIn.process (v);
                    float d = k.sat.process (v * g + bias) - std::tanh (bias);
                    d = k.dc.process (d);
                    k.post.lowpass (9000.0f - 4500.0f * amt, 0.707f, fs);
                    y[c] = k.post.process (d) / std::sqrt (g) * 1.25f;
                }
                else if (e == Fuzz)
                {
                    // Tilt moves tone and bias together: left is round and gated, right is bright and spitty
                    const float bias = tilt * 0.35f;
                    const float g = 8.0f * std::pow (60.0f, amt);
                    float d = k.fz.process (v * g + bias) - bias / (1.0f + std::fabs (bias));
                    d = k.fz2.process (d * 2.5f) * 0.7f;
                    const float gate = clampf (k.gate.process (v) / 0.0015f, 0.0f, 1.0f);
                    d = k.dc.process (d) * gate;
                    k.tiltOut.set (tilt);
                    k.post.lowpass (3200.0f + 3000.0f * knob, 0.707f, fs);
                    y[c] = k.tiltOut.process (k.post.process (d)) * 0.42f;
                }
                else // Howl
                {
                    const float g = 4.0f * std::pow (40.0f, amt);
                    float d = k.fz.process (v * g) * 0.8f;
                    const float cutoff = 150.0f * std::pow (30.0f, knob) * (1.0f + 3.0f * std::min (1.0f, env * 3.0f));
                    k.svf.set (std::min (cutoff, 0.4f * fs), 1.5f + 9.0f * amt, fs);
                    float lo, bp, hi;
                    k.svf.tick (d, lo, bp, hi);
                    y[c] = k.sat.process ((lo + 0.6f * bp) * 1.2f) * 0.55f;
                }
            }
        }

        // into the next module: blend from dry while an effect swap is under way
        return { lerpf (in.l, y[0] * vol, fade), lerpf (in.r, y[1] * vol, fade) };
    }

private:
    struct Chan
    {
        AdaaTanh sat;
        AdaaFuzz fz, fz2;
        Svf svf;
        Biquad post;
        Tilt tiltIn, tiltOut;
        DcBlock dc;
        Envelope gate;
    };
    float fs = 48000.0f;
    Chan ch[2];
    Envelope comp, howlEnv, swFast, swSlow;
    Smoother kS, aS;
    SwapFade swap;
    float swellGain = 1.0f;
    int lockout = 0;
};
} // namespace pp
