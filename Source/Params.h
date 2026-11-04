#pragma once

#include "engine/PrismEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>

// Every control on the Prism Patch panel. All are automatable, MIDI-mappable in
// Ableton and saved with the set.
namespace ParamIDs
{
// per module, indexed by pp::ModuleId
inline const char* const fx[4] = { "charFx", "moveFx", "diffFx", "texFx" };
inline const char* const knob[4] = { "tilt", "rate", "time", "" };
inline const char* const amount[4] = { "charAmount", "moveAmount", "diffAmount", "texAmount" };
inline const char* const on[4] = { "charOn", "moveOn", "diffOn", "texOn" };
inline const char* const vol[4] = { "charVol", "moveVol", "diffVol", "texVol" };
inline const char* const group[4] = { "charGroup", "moveGroup", "diffGroup", "texGroup" };
inline constexpr auto driftM = "driftMove";
inline constexpr auto driftD = "driftDiff";

inline constexpr auto order = "order";
inline constexpr auto mix = "mix";
inline constexpr auto output = "output";
inline constexpr auto sens = "sensitivity";
inline constexpr auto calLevel = "calibration";
inline constexpr auto filterStyle = "filterStyle";
inline constexpr auto inputMode = "inputMode";
inline constexpr auto sync = "sync";
inline constexpr auto bpm = "bpm";
inline constexpr auto bypass = "bypass";
inline constexpr auto trails = "trails";
inline constexpr auto trueDry = "trueDry";
inline constexpr auto capturePre = "capturePre";
inline constexpr auto captureOverdub = "captureOverdub";
inline constexpr auto captureQuantise = "captureQuantise";
inline constexpr auto gestQuant = "gestureQuantise";
inline constexpr auto gestSpeed = "gestureSpeed";
inline constexpr auto spareFx = "spareFx";
inline constexpr auto spareAmount = "spareAmount";
inline constexpr auto spareKnob = "spareKnob";
inline constexpr auto expTarget = "expTarget";
inline constexpr auto rateSync = "rateSync";
inline constexpr auto timeSync = "timeSync";
} // namespace ParamIDs

namespace names
{
inline const juce::StringArray modules { "Character", "Movement", "Diffusion", "Texture" };
inline const juce::StringArray effects[4] = {
    { "Drive", "Sweeten", "Fuzz", "Howl", "Swell", "Off" },
    { "Doubler", "Vibrato", "Phaser", "Tremolo", "Pitch", "Off" },
    { "Cascade", "Reels", "Space", "Collage", "Reverse", "Off" },
    { "Filter", "Squash", "Cassette", "Broken", "Interference", "Off" },
};
inline const juce::StringArray knobNames { "Tilt", "Rate", "Time", "" };

// the 24 orders of the four modules; 0 is the pedal's default left-to-right order
inline const std::array<std::array<int, 4>, 24>& orders()
{
    static const auto table = []
    {
        std::array<std::array<int, 4>, 24> t {};
        std::array<int, 4> p { 0, 1, 2, 3 };
        int i = 0;
        do { t[(size_t) i++] = p; } while (std::next_permutation (p.begin(), p.end()));
        return t;
    }();
    return table;
}
inline int orderIndex (const std::array<int, 4>& p)
{
    for (int i = 0; i < 24; ++i) if (orders()[(size_t) i] == p) return i;
    return 0;
}

inline juce::StringArray spareChoices()
{
    juce::StringArray s { "Off" };
    for (int m = 0; m < 4; ++m)
        for (int e = 0; e < 5; ++e) s.add (effects[m][e]);
    return s;
}

// what an expression pedal can move: every primary and secondary knob
struct ExpTarget { const char* id; const char* label; };
inline const std::vector<ExpTarget>& expTargets()
{
    static const std::vector<ExpTarget> t {
        { "tilt", "Tilt" }, { "charAmount", "Character Amount" }, { "rate", "Rate" }, { "moveAmount", "Movement Amount" },
        { "time", "Time" }, { "diffAmount", "Diffusion Amount" }, { "texAmount", "Texture Amount" }, { "mix", "Mix" },
        { "sensitivity", "Sensitivity" }, { "driftMove", "Movement Drift" }, { "driftDiff", "Diffusion Drift" },
        { "spareAmount", "Spare Amount" }, { "output", "Output" },
    };
    return t;
}
inline juce::StringArray expTargetNames()
{
    juce::StringArray s;
    for (const auto& t : expTargets()) s.add (t.label);
    return s;
}

// what the Rate and Time knobs really do, for the panel's readouts
inline juce::String noteName (float beats)
{
    struct N { float b; const char* n; };
    static const N table[] = { { 8, "2 BARS" }, { 4, "1 BAR" }, { 3, "1/2 DOT" }, { 2, "1/2" }, { 1.5f, "1/4 DOT" }, { 1, "1/4" },
                               { 0.75f, "1/8 DOT" }, { 2.0f / 3.0f, "1/4 TRIP" }, { 0.5f, "1/8" }, { 1.0f / 3.0f, "1/8 TRIP" },
                               { 0.25f, "1/16" }, { 0.125f, "1/32" }, { 0.0625f, "1/64" } };
    for (const auto& t : table) if (std::abs (t.b - beats) < 0.01f) return t.n;
    return juce::String (beats, 2) + " BEATS";
}
inline juce::String ms (float seconds)
{
    return seconds < 1.0f ? juce::String (juce::roundToInt (seconds * 1000.0f)) + " MS" : juce::String (seconds, 2) + " S";
}
inline juce::String rateText (float knob, bool synced)
{
    if (synced) return noteName (pp::Sync::beats (knob));
    const float hz = pp::freeRateHz (knob);
    return hz < 1.0f ? juce::String (hz, 2) + " HZ" : juce::String (hz, 1) + " HZ";
}
inline juce::String timeText (int effect, float knob, bool synced)
{
    switch (effect)
    {
        case 0: case 1: // Cascade, Reels
        {
            static const float sub[6] = { 0.25f, 1.0f / 3.0f, 0.5f, 2.0f / 3.0f, 0.75f, 1.0f };
            return synced ? noteName (sub[std::min (5, (int) (knob * 6.0f))]) : ms (0.04f * std::pow (12.5f, knob));
        }
        case 2:
        {
            static const char* const rooms[5] = { "CHAMBER", "ROOM", "PLATE", "HALL", "CLOUD" };
            return rooms[juce::jlimit (0, 4, juce::roundToInt (knob * 4.0f))];
        }
        case 3:
        {
            static const float sub[7] = { 0.0625f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
            return synced ? noteName (sub[std::min (6, (int) (knob * 7.0f))]) : ms (0.03f * std::pow (50.0f, knob));
        }
        case 4:
        {
            const float speed = std::pow (2.0f, (knob - 0.5f) * 2.0f);
            return std::abs (speed - 1.0f) < 0.02f ? juce::String ("NORMAL SPEED") : juce::String (speed, 2) + "X SPEED";
        }
        default: return "-";
    }
}

inline const juce::StringArray gestQuant { "Free", "1 Bar", "2 Bars", "4 Bars" };
inline const juce::StringArray gestSpeeds { "1/4x", "1/2x", "1x", "2x", "4x" };
inline constexpr float gestSpeedValue[5] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
} // namespace names

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using P = AudioParameterFloat;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pct = [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; };
    auto knob = [&] (const char* id, const char* name, float def, std::function<String (float, int)> text = {})
    {
        layout.add (std::make_unique<P> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def,
                                         AudioParameterFloatAttributes().withStringFromValueFunction (text ? std::move (text) : pct)));
    };
    auto decibels = [&] (const char* id, const char* name, float lo, float hi)
    {
        layout.add (std::make_unique<P> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi), 0.0f,
                                         AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                         {
                                             return std::abs (v) < 0.05f ? String ("0 dB") : (v > 0 ? "+" : "") + String (v, 1) + " dB";
                                         })));
    };
    auto toggle = [&] (const char* id, const char* name, bool def) { layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
    auto choice = [&] (const char* id, const char* name, StringArray items, int def) { layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };

    // ---- the four modules ----
    const int defaultFx[4] = { 0, 1, 2, 2 }; // Drive, Vibrato, Space, Cassette
    for (int m = 0; m < 4; ++m)
    {
        const String mod = names::modules[m];
        choice (ParamIDs::fx[m], (mod + " Effect").toRawUTF8(), names::effects[m], defaultFx[m]);
        knob (ParamIDs::amount[m], (mod + " Amount").toRawUTF8(), m == 3 ? 0.35f : 0.45f);
        toggle (ParamIDs::on[m], (mod + " On").toRawUTF8(), true);
        decibels (ParamIDs::vol[m], (mod + " Effect Vol").toRawUTF8(), -12.0f, 12.0f);
        toggle (ParamIDs::group[m], (mod + " In Bypass Group").toRawUTF8(), true);
    }
    knob (ParamIDs::knob[0], "Tilt", 0.5f, [] (float v, int)
    {
        if (std::abs (v - 0.5f) < 0.03f) return String ("Neutral");
        return String (v < 0.5f ? "Dark " : "Bright ") + String (roundToInt (std::abs (v - 0.5f) * 200.0f));
    });
    knob (ParamIDs::knob[1], "Rate", 0.5f, [] (float v, int)
    {
        const float hz = pp::freeRateHz (v);
        return hz < 1.0f ? String (hz, 2) + " Hz" : String (hz, 1) + " Hz";
    });
    knob (ParamIDs::knob[2], "Time", 0.5f);
    knob (ParamIDs::driftM, "Movement Drift", 0.0f);
    knob (ParamIDs::driftD, "Diffusion Drift", 0.0f);

    {
        StringArray orderNames;
        for (const auto& o : names::orders())
        {
            String s;
            for (int k = 0; k < 4; ++k) s << (k ? " > " : "") << names::modules[o[(size_t) k]].substring (0, 4);
            orderNames.add (s);
        }
        choice (ParamIDs::order, "Module Order", orderNames, 0);
    }

    // ---- shared ----
    knob (ParamIDs::mix, "Mix", 1.0f);
    decibels (ParamIDs::output, "Output Level", -12.0f, 12.0f);
    knob (ParamIDs::sens, "Sensitivity", 0.5f, [] (float v, int)
    {
        if (std::abs (v - 0.5f) < 0.03f) return String ("Neutral");
        return String (v < 0.5f ? "Cleaner " : "Hotter ") + String (roundToInt (std::abs (v - 0.5f) * 200.0f));
    });
    choice (ParamIDs::calLevel, "Calibration", { "Low", "Medium", "High", "Very High" }, 1);
    choice (ParamIDs::filterStyle, "Filter Style", { "Tilt", "Low-pass", "High-pass" }, 0);
    choice (ParamIDs::inputMode, "Input", { "Guitar", "Synth", "Bass" }, 0);
    toggle (ParamIDs::sync, "Sync to Ableton", true);
    layout.add (std::make_unique<P> (ParameterID { ParamIDs::bpm, 1 }, "Tempo", NormalisableRange<float> (40.0f, 240.0f, 0.1f), 120.0f,
                                     AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " BPM"; })));
    toggle (ParamIDs::bypass, "Bypass", false);
    toggle (ParamIDs::trails, "Trails", true);
    toggle (ParamIDs::trueDry, "True Dry at Mix Zero", false);

    // ---- Capture and Gesture ----
    choice (ParamIDs::capturePre, "Capture Routing", { "Post FX", "Pre FX" }, 0);
    toggle (ParamIDs::captureOverdub, "Capture Overdub", false);
    toggle (ParamIDs::captureQuantise, "Capture Quantise", false);
    choice (ParamIDs::gestQuant, "Gesture Quantise", names::gestQuant, 0);
    choice (ParamIDs::gestSpeed, "Gesture Speed", names::gestSpeeds, 2);

    // ---- spare slot ----
    choice (ParamIDs::spareFx, "Spare Slot", names::spareChoices(), 0);
    knob (ParamIDs::spareAmount, "Spare Amount", 0.5f);
    knob (ParamIDs::spareKnob, "Spare Tilt/Rate/Time", 0.5f);

    // ---- tempo sync for Rate and Time, off by default as on the pedal until you tap ----
    toggle (ParamIDs::rateSync, "Rate Follows Tempo", false);
    toggle (ParamIDs::timeSync, "Time Follows Tempo", false);

    // ---- expression ----
    choice (ParamIDs::expTarget, "Expression Target", names::expTargetNames(), 4);

    return layout;
}
