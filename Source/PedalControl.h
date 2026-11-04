#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <functional>

// Footswitch and expression setup, shared by every Flip Fella plugin on this computer,
// so a footswitch set up once works in all of them. Nothing here names a brand:
// any MIDI footswitch with momentary switches and any expression input works.
//
// Defaults: footswitches A to D send momentary CC 20, 21, 22, 23 on channel 16,
// expression sends CC 11 on channel 16. Learn remaps any of them.
struct PedalSettings
{
    std::array<int, 4> fsChannel { 16, 16, 16, 16 };
    std::array<int, 4> fsCC { 20, 21, 22, 23 };
    int expChannel = 16, expCC = 11;
    int heel = 0, toe = 127;     // calibrated ends of the expression pedal; toe may be lower than heel
    int curve = 0;               // 0 linear, 1 slow start, 2 fast start
    int pedalMapChannel = 0;     // 0 off, else the channel that speaks the original pedal's own CC map
    int keeperChannel = 0, keeperCC = -1;  // a learned switch just for Keeper; off until learned
    bool keeperLive = false;     // Keepers are tagged Live or Studio

    void load();
    void save() const;
    void resetFootswitches();
    float expressionValue (int cc) const;    // 0..1 after calibration and curve
    static juce::File file();
};

// Turns plain press and release messages into taps, holds and two-switch combos.
// Runs on the message thread.
class FootswitchLogic
{
public:
    enum Event { Down, Tap, HoldStart, HoldEnd, ComboAB, ComboCD, ComboHoldAB, ComboHoldCD };
    std::function<void (int sw, Event)> onEvent;

    static constexpr double kHoldSeconds = 0.45, kComboWindow = 0.12, kComboHoldSeconds = 1.0;

    void press (int sw, double now);
    void release (int sw, double now);
    void tick (double now);
    bool isDown (int sw) const { return down[(size_t) sw]; }

private:
    std::array<bool, 4> down {}, held {}, combo {};
    std::array<double, 4> downAt {};
    std::array<double, 2> comboAt {};      // A+B and C+D
    std::array<bool, 2> comboHeld {};
};
