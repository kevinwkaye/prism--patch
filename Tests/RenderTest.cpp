// Offline render test for the Prism Patch engine (no JUCE needed).
// Plays a guitar-like phrase through every effect, full chains in different orders,
// Capture and bypass with trails, then checks for NaNs, runaway levels, clicks and silence.
// Pass a folder to also write WAV files.
//
// Usage: prism_render_test [output_folder]

#include "PrismEngine.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

using namespace pp;

static constexpr float kFs = 48000.0f;

static void makeInput (std::vector<float>& l, std::vector<float>& r, int n)
{
    l.assign ((size_t) n, 0.0f);
    r.assign ((size_t) n, 0.0f);
    const float ratios[8] = { 1.0f, 1.26f, 1.498f, 2.0f, 1.498f, 1.26f, 1.122f, 1.0f };
    for (int k = 0; k < 8; ++k)
    {
        const int s0 = (int) ((0.15f + k * 0.62f) * kFs);
        const bool chord = k == 3 || k == 7;
        for (int i = 0; i < (int) (1.8f * kFs) && s0 + i < n; ++i)
        {
            const float t = (float) i / kFs;
            const float e = std::exp (-t * 2.2f) * std::min (1.0f, t * 400.0f);
            float v = 0.0f;
            for (int c = 0; c < (chord ? 3 : 1); ++c)
            {
                const float f = 196.0f * ratios[k] * (c == 0 ? 1.0f : (c == 1 ? 1.26f : 1.498f));
                v += 0.2f * e * (std::sin (kTwoPi * f * t) + 0.5f * std::sin (kTwoPi * 2 * f * t + 0.3f) + 0.25f * std::sin (kTwoPi * 3 * f * t));
            }
            l[(size_t) (s0 + i)] += v;
            r[(size_t) (s0 + i)] += v * 0.97f;
        }
    }
}

static void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const uint32_t n = (uint32_t) l.size(), dataBytes = n * 4, sr = (uint32_t) kFs;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (2); u32 (sr); u32 (sr * 4); u16 (4); u16 (16);
    std::fwrite ("data", 1, 4, f); u32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
    {
        int16_t a = (int16_t) (clampf (l[i], -1, 1) * 32767.0f);
        int16_t b = (int16_t) (clampf (r[i], -1, 1) * 32767.0f);
        std::fwrite (&a, 2, 1, f); std::fwrite (&b, 2, 1, f);
    }
    std::fclose (f);
}

struct Result { float peak = 0, rms = 0, tailRms = 0, ms = 0; bool nan = false; };
using Script = std::function<void (float t, EngineParams&, PrismEngine&)>;

static EngineParams soloParams (int module, int effect)
{
    EngineParams p;
    for (int m = 0; m < kNumModules; ++m) { p.fx[m] = kOff; p.ctl[m] = { 0.5f, 0.6f, 0.0f, 0.0f }; }
    p.fx[module] = effect;
    return p;
}

static Result render (const std::string& name, EngineParams p, const Script& script, const std::string& outDir, float seconds = 7.0f)
{
    static std::unique_ptr<PrismEngine> eng;
    eng = std::make_unique<PrismEngine>();
    eng->prepare (kFs);
    const int n = (int) (seconds * kFs);
    std::vector<float> l, r;
    makeInput (l, r, n);
    const int block = 128;
    Result res;
    double sum = 0, tail = 0;
    int tailCount = 0;
    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int s = 0; s < n; s += block)
    {
        const int len = std::min (block, n - s);
        if (script) script ((float) s / kFs, p, *eng);
        eng->process (l.data() + s, r.data() + s, len, p);
    }
    res.ms = (float) std::chrono::duration<double, std::milli> (std::chrono::high_resolution_clock::now() - t0).count();
    for (int i = 0; i < n; ++i)
    {
        const float a = l[(size_t) i], b = r[(size_t) i];
        if (! std::isfinite (a) || ! std::isfinite (b)) res.nan = true;
        res.peak = std::max (res.peak, std::max (std::fabs (a), std::fabs (b)));
        sum += a * a + b * b;
        if (i > n - (int) kFs) { tail += a * a + b * b; ++tailCount; }
    }
    res.rms = (float) std::sqrt (sum / (2.0 * n));
    res.tailRms = (float) std::sqrt (tail / (2.0 * std::max (1, tailCount)));
    if (! outDir.empty()) writeWav (outDir + "/" + name + ".wav", l, r);
    const float rt = res.ms / (seconds * 1000.0f) * 100.0f;
    std::printf ("%-28s peak %6.3f  rms %6.4f  tail %7.5f  cpu %5.1f%%%s\n", name.c_str(), res.peak, res.rms, res.tailRms, rt, res.nan ? "  NAN" : "");
    return res;
}

int main (int argc, char** argv)
{
#if defined(__SSE__) || defined(_M_X64)
    _mm_setcsr (_mm_getcsr() | 0x8040); // flush denormals, as a host does
#endif
    const std::string out = argc > 1 ? argv[1] : "";
    int failures = 0;
    auto check = [&] (const Result& r, bool expectSound, float maxPeak = 2.0f)
    {
        if (r.nan || r.peak > maxPeak || (expectSound && r.rms < 0.002f)) { ++failures; std::printf ("   ^ FAILED\n"); }
    };

    static const char* names[4][5] = { { "drive", "sweeten", "fuzz", "howl", "swell" },
                                       { "doubler", "vibrato", "phaser", "tremolo", "pitch" },
                                       { "cascade", "reels", "space", "collage", "reverse" },
                                       { "filter", "squash", "cassette", "broken", "interference" } };
    for (int m = 0; m < kNumModules; ++m)
        for (int e = 0; e < kEffectsPerModule; ++e)
        {
            auto p = soloParams (m, e);
            if (m != ModTexture) p.ctl[m].drift = 0.4f;
            check (render (names[m][e], p, nullptr, out), true);
            // the extremes, with a knob sweep, must stay finite and bounded
            auto px = p; px.ctl[m].amount = 1.0f; px.ctl[m].drift = m == ModTexture ? 0.0f : 1.0f;
            check (render (std::string (names[m][e]) + "_max_sweep", px, [m] (float t, EngineParams& q, PrismEngine&)
                   { q.ctl[m].knob = 0.5f + 0.5f * std::sin (t * 2.3f); }, out), true, 3.0f);
        }

    // pitch at both octave extremes
    { auto p = soloParams (ModMovement, 4); p.ctl[ModMovement].knob = 0.0f; p.ctl[ModMovement].amount = 1.0f; check (render ("pitch_down", p, nullptr, out), true); }
    { auto p = soloParams (ModMovement, 4); p.ctl[ModMovement].knob = 1.0f; p.ctl[ModMovement].amount = 1.0f; check (render ("pitch_up", p, nullptr, out), true); }

    // full chains, default order and reversed
    EngineParams full;
    for (auto& c : full.ctl) c = { 0.5f, 0.5f, 0.3f, 0.0f };
    full.fx[0] = 0; full.fx[1] = 1; full.fx[2] = 2; full.fx[3] = 2;
    check (render ("chain_default", full, nullptr, out), true);
    auto rev = full; rev.order[0] = 2; rev.order[1] = 3; rev.order[2] = 1; rev.order[3] = 0;
    check (render ("chain_space_first", rev, nullptr, out), true);
    // reverb into fuzz, the pedal's party trick
    auto rf = full; rf.order[0] = 2; rf.order[1] = 0; rf.fx[0] = 2; rf.ctl[0].amount = 0.8f;
    check (render ("chain_reverb_into_fuzz", rf, nullptr, out), true);

    // effect switching every half second while playing
    check (render ("switching", full, [] (float t, EngineParams& q, PrismEngine&)
           { const int k = (int) (t * 2.0f); for (int m = 0; m < 4; ++m) q.fx[m] = (k + m) % 6; }, out), true);

    // spare slot: cassette after the chain
    auto sp = full; sp.spareFx = ModTexture * 5 + 2; sp.spareCtl.amount = 0.6f;
    check (render ("spare_cassette", sp, nullptr, out), true);

    // all calibration levels and the true-dry toggle at mix zero
    for (int c = 0; c < 4; ++c) { auto p = full; p.calLevel = c; check (render ("cal_" + std::to_string (c), p, nullptr, out), true); }
    { auto p = full; p.mix = 0.0f; p.trueDry = true; check (render ("mix_zero_dry", p, nullptr, out), true); }

    // bypass at 2 s with trails: the reverb must still ring in the last second
    {
        auto p = soloParams (ModDiffusion, 1); p.ctl[ModDiffusion].amount = 0.7f;
        const auto r = render ("bypass_trails", p, [] (float t, EngineParams& q, PrismEngine&) { q.bypass = t > 4.6f; }, out, 6.0f);
        check (r, true);
        if (r.tailRms < 0.0005f) { ++failures; std::printf ("   ^ FAILED: no trails after bypass\n"); }
    }

    // Capture: a short pad and a long loop, post and pre
    for (int pre = 0; pre < 2; ++pre)
        for (int padCase = 0; padCase < 2; ++padCase)
        {
            auto p = full; p.capturePre = pre == 1;
            const float holdFor = padCase ? 0.4f : 2.0f;
            bool held = false, released = false;
            const auto r = render (std::string ("capture_") + (pre ? "pre_" : "post_") + (padCase ? "pad" : "loop"), p,
                                   [&] (float t, EngineParams&, PrismEngine& e)
                                   {
                                       if (! held && t > 1.0f) { e.postCommand (PrismEngine::CmdCaptureHold); held = true; }
                                       if (held && ! released && t > 1.0f + holdFor) { e.postCommand (PrismEngine::CmdCaptureRelease); released = true; }
                                   }, out, 9.0f);
            check (r, true);
            if (r.tailRms < 0.001f) { ++failures; std::printf ("   ^ FAILED: capture is silent at the end\n"); }
        }

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
