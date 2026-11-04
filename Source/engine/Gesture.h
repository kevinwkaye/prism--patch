#pragma once

#include "DspUtil.h"
#include <array>

// GESTURE: records knob movements and loops them, one loop per primary knob.
// Playback rides on top of the knob's own value, so Ableton automation and the
// recorded movement work together: automation shifts the whole loop up or down.
namespace pp
{
class Gesture
{
public:
    static constexpr int kLanes = 8;      // Tilt, Rate, Time, Mix and the four Amounts
    static constexpr float kRate = 100.0f; // control points per second
    static constexpr int kMaxPoints = (int) (60.0f * kRate);

    struct Lane
    {
        std::vector<float> points;
        float anchor = 0.0f;      // the knob's value when recording ended
        float recordBpm = 120.0f;
        float lengthBeats = 0.0f; // set when quantised to bars, 0 for free length
        bool recording = false, active = false;
        float phase = 0.0f;
    };

    Gesture() { for (auto& l : lanes) l.points.reserve ((size_t) kMaxPoints); }

    void startRecording()
    {
        armed = true;
        for (auto& l : lanes) l.recording = false;
    }

    // ends recording; each moved knob starts looping from where its movement began
    void stopRecording (float bpm, int quantiseBars)
    {
        armed = false;
        for (auto& l : lanes)
            if (l.recording)
            {
                l.recording = false;
                if (l.points.size() < 8) { l.active = false; l.points.clear(); continue; }
                l.anchor = l.points.back();
                l.recordBpm = bpm;
                l.lengthBeats = quantiseBars > 0 ? 4.0f * (float) quantiseBars : 0.0f;
                l.phase = 0.0f;
                l.active = true;
            }
    }

    bool isRecording() const { return armed; }
    bool anyActive() const { for (auto& l : lanes) if (l.active) return true; return false; }
    int activeCount() const { int n = 0; for (auto& l : lanes) n += l.active ? 1 : 0; return n; }
    void erase (int lane) { lanes[(size_t) lane].active = false; lanes[(size_t) lane].points.clear(); }
    void clear() { for (int i = 0; i < kLanes; ++i) erase (i); armed = false; }
    const Lane& lane (int i) const { return lanes[(size_t) i]; }
    Lane& lane (int i) { return lanes[(size_t) i]; }

    // Called once per control tick (1 / kRate seconds) with each knob's own value.
    // Writes the value each knob should actually have now.
    void tick (const float* knobs, float* effective, float bpm, float speed)
    {
        for (int i = 0; i < kLanes; ++i)
        {
            auto& l = lanes[(size_t) i];
            const float v = knobs[i];
            if (armed)
            {
                // a knob joins the recording the moment it moves
                if (! l.recording && std::fabs (v - last[(size_t) i]) > 0.002f)
                {
                    l.recording = true;
                    l.active = false;
                    l.points.clear();
                }
                if (l.recording && (int) l.points.size() < kMaxPoints) l.points.push_back (v);
            }
            last[(size_t) i] = v;

            if (! l.active || l.points.empty()) { effective[i] = v; continue; }

            // tempo changes speed the loop up or down against the tempo it was recorded at
            const float n = (float) l.points.size();
            float step = speed * std::max (0.1f, bpm) / std::max (1.0f, l.recordBpm);
            if (l.lengthBeats > 0.0f)
            {
                const float loopTicks = l.lengthBeats * 60.0f / std::max (20.0f, bpm) * kRate;
                step = speed * n / std::max (1.0f, loopTicks);
            }
            l.phase += step;
            while (l.phase >= n) l.phase -= n;
            const int a = (int) l.phase, b = (a + 1) % (int) n;
            const float f = l.phase - (float) a;
            const float g = l.points[(size_t) a] + (l.points[(size_t) b] - l.points[(size_t) a]) * f;
            effective[i] = clampf (g + (v - l.anchor), 0.0f, 1.0f);
        }
    }

private:
    std::array<Lane, kLanes> lanes;
    std::array<float, kLanes> last {};
    bool armed = false;
};
} // namespace pp
