#pragma once

#include "Params.h"
#include "PedalControl.h"
#include "Keeper.h"
#include "engine/Gesture.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

class PrismProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int kPresets = 80;          // four banks of twenty, as on the pedal
    static constexpr int kPerBank = 20;

    PrismProcessor();
    ~PrismProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Prism Patch"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    pp::PrismEngine& getEngine() { return engine; }

    // ---- called from the panel (message thread) ----
    void footDown (int sw);
    void footUp (int sw);
    void captureDown();
    void captureUp();
    void tap();
    void toggleGestureRecord();
    void clearGestures();
    void touchKnob (const juce::String& paramId); // grabbing a looping knob erases its Gesture
    void moveModule (int slot, int direction);
    std::array<int, 4> currentOrder() const;

    void savePreset (int slot);
    void loadPreset (int slot);
    void clearPreset (int slot);
    bool hasPreset (int slot) const;
    void stepPreset (int direction);
    int selectedPreset() const { return selected; }

    // footswitch and expression setup
    PedalSettings& pedals() { return settings; }
    void learnFootswitch (int sw);
    void learnExpression();
    void calibrateExpression();
    void resetFootswitches();
    void autoCalibrate();   // listens for three seconds, then picks the input headroom

    // Keeper: log this moment, the last 8 seconds of audio included
    void keepNow();
    void addKeeperNote (int number, const juce::String& note) { keeper.addNote (number, note); }
    juce::File exportKeepers();
    void openKeepersFolder();
    void learnKeeper();
    bool keeperLive() const { return settings.keeperLive; }
    void setKeeperLive (bool live);
    int lastKeeper() const { return keeper.lastNumber(); }
    std::atomic<int> uiKeepPulse { 0 };       // counts Keepers, Pebble nods
    bool isLearning() const { return learnTarget != LearnNone; }
    int learningWhat() const { return learnTarget; }
    bool isCalibrating() const { return calibrating; }
    bool captureLatched() const { return capLatched; }
    enum { LearnNone = -1, LearnExpression = 4, LearnKeeper = 5 };

    // ---- what the panel and Pebble show ----
    float currentBpm() const { return uiBpm.load(); }
    bool hostSynced() const { return uiSynced.load(); }
    bool gestureRecording() const { return uiGestRec.load(); }
    int gestureLoops() const { return uiGestLoops.load(); }
    bool laneActive (int lane) const { return uiLaneActive[(size_t) lane].load(); }
    float laneValue (int lane) const { return uiLaneValue[(size_t) lane].load(); }
    static int laneFor (const juce::String& paramId);
    float expressionLevel() const { return uiExp.load(); }
    double lastExpressionMove() const { return expMovedAt; }
    std::atomic<int> uiFootPulse { 0 };       // counts footswitch presses, Pebble taps its foot
    std::atomic<int> uiDeepToggle { 0 };      // A+B pressed: the panel shows or hides the Deep page
    std::atomic<int> uiFootDown { 0 };        // bitmask of footswitches held now
    juce::String uiMessage;                   // a short line for Pebble's status
    double uiMessageAt = 0.0;

private:
    void timerCallback() override;
    pp::EngineParams readParams (int numSamples);
    void handleMidi (const juce::MidiMessage&);
    void onFootEvent (int sw, FootswitchLogic::Event);
    void setNorm (const juce::String& id, float v01);
    void setPlain (const juce::String& id, float plain);
    void say (const juce::String& s);
    void dualBypass();

    juce::ValueTree snapshotOf();
    void applySnapshot (const juce::ValueTree&);
    static bool inSnapshot (const juce::String& id);
    juce::ValueTree gestureTree();
    void queueGestureLoad (const juce::ValueTree&);
    static juce::String encodeAudio (const std::vector<float>& l, const std::vector<float>& r);
    static bool decodeAudio (const juce::String&, std::vector<float>& l, std::vector<float>& r);

    pp::PrismEngine engine;
    Keeper keeper;
    pp::Gesture gesture;
    juce::SpinLock gestureLock;
    float gestAcc = 0.0f;
    std::array<float, pp::Gesture::kLanes> laneEffective {};
    static const char* laneParam (int lane);

    std::map<juce::String, std::atomic<float>*> raw;
    float f (const char* id) const { return raw.at (id)->load (std::memory_order_relaxed); }
    bool on (const char* id) const { return f (id) > 0.5f; }
    int idx (const char* id) const { return (int) std::lround (f (id)); }

    // panel and MIDI requests handed to the audio thread
    enum { GestStart = 1, GestStop = 2, GestClear = 4 };
    std::atomic<int> gestCmd { 0 };
    std::atomic<uint32_t> gestErase { 0 };
    std::atomic<bool> gestLoadReady { false };
    std::vector<pp::Gesture::Lane> gestLoad;

    // MIDI handed to the message thread
    struct Pending { int kind = 0; int a = 0; float v = 0; const char* id = nullptr; };
    enum PendingKind { FootPress, FootRelease, Expression, SetNorm, SetPlain, LoadPreset, Tap, Learned, CaptureCC, GestureCC, KeepCC };
    juce::AbstractFifo fifo { 512 };
    std::array<Pending, 512> pending;
    void queue (int kind, int a, float v);
    void queueParam (int kind, const char* id, float v);

    std::atomic<bool> captureLoadReady { false };
    std::vector<float> capLoadL, capLoadR;
    bool capLoadPad = false;

    struct Slot { juce::ValueTree params; };
    std::array<Slot, kPresets> slots;
    int selected = -1;

    PedalSettings settings;
    FootswitchLogic footswitches;
    std::atomic<int> learnTarget { LearnNone };
    bool calibrating = false;
    double calibrateUntil = 0.0;
    int calLo = 127, calHi = 0, calFirst = -1;
    double expMovedAt = 0.0;
    bool captureStartedOnDown = false, tapLocked = false;
    double autoCalUntil = 0.0;
    float autoCalPeak = 0.0f;
    double capDownAt = -1.0;
    bool capHeldFired = false, capStartedOnDown = false, capLatched = false;

    // bass input keeps its lows dry: Linkwitz-Riley split at 120 Hz
    pp::Biquad bassLp[2][2], bassHp[2][2];
    std::vector<float> lowL, lowR;

    std::atomic<float> uiBpm { 120.0f }, uiExp { 0.0f };
    std::atomic<bool> uiSynced { false }, uiGestRec { false };
    std::atomic<int> uiGestLoops { 0 };
    std::array<std::atomic<bool>, pp::Gesture::kLanes> uiLaneActive {};
    std::array<std::atomic<float>, pp::Gesture::kLanes> uiLaneValue {};
    std::vector<double> taps;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PrismProcessor)
};
