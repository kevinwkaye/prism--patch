#pragma once

// The whole Prism Patch signal path, in plain C++ so it can be tested without JUCE.
//
// input -> calibration -> [Capture pre] -> four modules in any order -> spare slot
//       -> soft clip -> [Capture post] -> Mix with the dry signal -> Output Level

#include "Capture.h"
#include "Character.h"
#include "Diffusion.h"
#include "Movement.h"
#include "Texture.h"
#include <memory>

namespace pp
{
enum ModuleId { ModCharacter = 0, ModMovement, ModDiffusion, ModTexture, kNumModules };
constexpr int kEffectsPerModule = 5;
constexpr int kOff = 5;

struct EngineParams
{
    int order[kNumModules] = { 0, 1, 2, 3 };     // which module sits in each slot
    int fx[kNumModules] = { 0, 1, 2, 2 };        // 0..4, or kOff
    bool on[kNumModules] = { true, true, true, true };
    ModuleControls ctl[kNumModules];

    int spareFx = -1;                            // -1 off, else module * 5 + effect
    ModuleControls spareCtl;

    float mix = 1.0f;
    bool trueDry = false;    // Mix at zero: false keeps the pedal's faint colour, true is fully dry
    float outDb = 0.0f;
    int calLevel = 1;        // 0 low, 1 medium, 2 high, 3 very high headroom
    Shared shared;
    bool bypass = false;
    bool trails = true;
    bool capturePre = false;
    bool captureOverdub = false;
    bool captureQuantise = false;
};

class PrismEngine
{
public:
    enum Command { CmdCaptureHold = 1, CmdCaptureRelease = 2, CmdCaptureClear = 4 };

    PrismEngine()
    {
        for (int i = 0; i < 2; ++i)
        {
            character[i] = std::make_unique<Character>();
            movement[i] = std::make_unique<Movement>();
            diffusion[i] = std::make_unique<Diffusion>();
            texture[i] = std::make_unique<Texture>();
        }
    }

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        for (int i = 0; i < 2; ++i)
        {
            character[i]->prepare (fs);
            movement[i]->prepare (fs);
            diffusion[i]->prepare (fs);
            texture[i]->prepare (fs);
        }
        capture.prepare (fs);
        for (auto& g : slotGain) g = 1.0f;
        bypassGain = 1.0f;
        fadeStep = 1.0f / (0.01f * fs);
        calS.setTime (0.05f, fs);
        mixS.setTime (0.02f, fs);
        outS.setTime (0.02f, fs);
        for (int c = 0; c < 2; ++c) { colourLp[c].setCutoff (17000.0f, fs); colourLp[c].reset(); colourSat[c].reset(); }
        for (auto* v : { &scratchL, &scratchR, &dryL, &dryR, &tailL, &tailR }) v->assign (8192, 0.0f);
    }

    double sampleRate() const { return fs; }
    void postCommand (int cmd) { commands.fetch_or (cmd); }
    Capture& getCapture() { return capture; }
    const Capture& getCapture() const { return capture; }

    void process (float* L, float* R, int n, const EngineParams& p)
    {
        if ((int) dryL.size() < n) { dryL.resize ((size_t) n); dryR.resize ((size_t) n); tailL.resize ((size_t) n); tailR.resize ((size_t) n); }
        std::copy (L, L + n, dryL.begin());
        std::copy (R, R + n, dryR.begin());

        const int cmd = commands.exchange (0);
        const float beat = 60.0f / std::max (20.0f, p.shared.bpm) * fs;
        if (cmd & CmdCaptureClear) capture.clear();
        if (cmd & CmdCaptureHold) capture.holdStart (p.captureOverdub);
        if (cmd & CmdCaptureRelease) capture.holdEnd (beat, p.captureQuantise);

        static const float calDb[4] = { 9.0f, 4.0f, 0.0f, -6.0f };
        const float calTarget = dbToGain (calDb[std::min (3, std::max (0, p.calLevel))]);

        // Capture before the effects records the input and feeds its loop into the chain
        if ((int) scratchL.size() < n) { scratchL.resize ((size_t) n); scratchR.resize ((size_t) n); }
        float inPeak = 0.0f, outPeak = 0.0f;
        float modPeak[kNumModules] = {};

        if (p.capturePre)
        {
            std::copy (L, L + n, scratchL.begin());
            std::copy (R, R + n, scratchR.begin());
            capture.process (L, R, scratchL.data(), scratchR.data(), n);
        }

        for (int i = 0; i < n; ++i)
        {
            const float dryL = L[i], dryR = R[i];
            inPeak = std::max (inPeak, std::max (std::fabs (dryL), std::fabs (dryR)));
            const float cal = calS.next (calTarget);
            Stereo x = p.capturePre ? Stereo { scratchL[(size_t) i], scratchR[(size_t) i] } : Stereo { dryL, dryR };
            x = x * cal;

            const float bTarget = p.bypass ? 0.0f : 1.0f;
            bypassGain += clampf (bTarget - bypassGain, -fadeStep, fadeStep);
            Stereo tail {};

            if (bypassGain <= 0.0f)
            {
                // bypassed: with trails, the delays and reverbs ring out, fed silence
                if (p.trails && p.on[ModDiffusion]) tail = runModule (0, ModDiffusion, {}, p.fx[ModDiffusion], p.ctl[ModDiffusion], p.shared);
                x = {};
            }
            else
            {
                for (int s = 0; s < kNumModules; ++s)
                {
                    const int m = std::min (kNumModules - 1, std::max (0, p.order[s]));
                    const float target = p.on[m] && p.fx[m] != kOff ? 1.0f : 0.0f;
                    slotGain[m] += clampf (target - slotGain[m], -fadeStep, fadeStep);
                    if (slotGain[m] <= 0.0f)
                    {
                        if (m == ModDiffusion && p.trails && p.fx[m] != kOff)
                            x += runModule (0, m, {}, p.fx[m], p.ctl[m], p.shared);
                        continue;
                    }
                    const Stereo y = runModule (0, m, x, p.fx[m], p.ctl[m], p.shared);
                    const float g = slotGain[m];
                    x = { softClip (lerpf (x.l, y.l, g)), softClip (lerpf (x.r, y.r, g)) };
                    modPeak[m] = std::max (modPeak[m], std::max (std::fabs (y.l), std::fabs (y.r)));
                }

                // the spare slot can host any of the twenty effects, after the four modules
                if (p.spareFx >= 0)
                {
                    const int m = std::min (kNumModules - 1, p.spareFx / kEffectsPerModule);
                    const int e = p.spareFx % kEffectsPerModule;
                    const Stereo y = runModule (1, m, x, e, p.spareCtl, p.shared);
                    x = { softClip (y.l), softClip (y.r) };
                }
            }

            const float inv = 1.0f / cal;
            L[i] = x.l * inv;
            R[i] = x.r * inv;
            tailL[(size_t) i] = tail.l * inv;
            tailR[(size_t) i] = tail.r * inv;
        }

        // Capture after the effects records the wet chain and plays on top of it
        if (! p.capturePre)
        {
            std::copy (L, L + n, scratchL.begin());
            std::copy (R, R + n, scratchR.begin());
            capture.process (scratchL.data(), scratchR.data(), L, R, n);
        }

        // Mix, output level and bypass. dry is re-read from the caller's input copy.
        for (int i = 0; i < n; ++i)
        {
            const float mix = mixS.next (p.mix);
            const float out = outS.next (dbToGain (p.outDb));
            float dl = dryIn (0, i), dr = dryIn (1, i);
            if (! p.trueDry)
            {
                // the pedal's buffered path: a whisper of saturation and top-end roll-off
                dl = colourLp[0].process (colourSat[0].process (dl * 1.05f) / std::tanh (1.05f));
                dr = colourLp[1].process (colourSat[1].process (dr * 1.05f) / std::tanh (1.05f));
            }
            const float wl = L[i] * out, wr = R[i] * out;
            const float ml = lerpf (dl, wl, mix), mr = lerpf (dr, wr, mix);
            // bypass fades to the clean input, with any trails ringing over it
            L[i] = lerpf (dryIn (0, i), ml, bypassGain) + tailL[(size_t) i] * out * mix;
            R[i] = lerpf (dryIn (1, i), mr, bypassGain) + tailR[(size_t) i] * out * mix;
            outPeak = std::max (outPeak, std::max (std::fabs (L[i]), std::fabs (R[i])));
        }

        uiInput.store (inPeak);
        uiOutput.store (outPeak);
        for (int m = 0; m < kNumModules; ++m) uiModule[m].store (modPeak[m]);
    }

    std::atomic<float> uiInput { 0.0f }, uiOutput { 0.0f };
    std::atomic<float> uiModule[kNumModules] {};

private:
    float dryIn (int c, int i) const { return c == 0 ? dryL[(size_t) i] : dryR[(size_t) i]; }

    Stereo runModule (int bank, int m, Stereo x, int fx, const ModuleControls& c, const Shared& s)
    {
        switch (m)
        {
            case ModCharacter: return character[bank]->process (x, fx, c, s);
            case ModMovement:  return movement[bank]->process (x, fx, c, s);
            case ModDiffusion: return diffusion[bank]->process (x, fx, c, s);
            default:           return texture[bank]->process (x, fx, c, s);
        }
    }

    float fs = 48000.0f;
    std::unique_ptr<Character> character[2];
    std::unique_ptr<Movement> movement[2];
    std::unique_ptr<Diffusion> diffusion[2];
    std::unique_ptr<Texture> texture[2];
    Capture capture;
    float slotGain[kNumModules] = { 1, 1, 1, 1 };
    float bypassGain = 1.0f, fadeStep = 0.002f;
    Smoother calS, mixS, outS;
    AdaaTanh colourSat[2];
    OnePole colourLp[2];
    std::vector<float> scratchL, scratchR, dryL, dryR, tailL, tailR;
    std::atomic<int> commands { 0 };
};
} // namespace pp
