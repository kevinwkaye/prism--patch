#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

// Several Prism Patch windows can be open at once. Footswitches drive one of them at a
// time: the one whose panel was last touched, or the next one along when C+D are pressed.
namespace
{
SpinLock focusLock;
Array<PrismProcessor*> instances;
std::atomic<PrismProcessor*> focused { nullptr };

bool hasFocus (PrismProcessor* p)
{
    auto* f = focused.load();
    return f == nullptr || f == p;
}
} // namespace

static const char* const kLaneParams[pp::Gesture::kLanes] = { "tilt", "rate", "time", "mix", "charAmount", "moveAmount", "diffAmount", "texAmount" };
const char* PrismProcessor::laneParam (int lane) { return kLaneParams[lane]; }
int PrismProcessor::laneFor (const String& id)
{
    for (int i = 0; i < pp::Gesture::kLanes; ++i)
        if (id == kLaneParams[i]) return i;
    return -1;
}

PrismProcessor::PrismProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PrismPatchState", createParameterLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            raw[rp->getParameterID()] = apvts.getRawParameterValue (rp->getParameterID());
    settings.load();
    footswitches.onEvent = [this] (int sw, FootswitchLogic::Event e) { onFootEvent (sw, e); };
    {
        const SpinLock::ScopedLockType l (focusLock);
        instances.add (this);
    }
    focused.store (this);
    startTimerHz (100);
}

PrismProcessor::~PrismProcessor()
{
    stopTimer();
    const SpinLock::ScopedLockType l (focusLock);
    instances.removeAllInstancesOf (this);
    if (focused.load() == this) focused.store (instances.isEmpty() ? nullptr : instances.getLast());
}

void PrismProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    for (int c = 0; c < 2; ++c)
    {
        for (auto* b : { &bassLp[c][0], &bassLp[c][1] }) { b->lowpass (120.0f, 0.7071f, (float) sampleRate); b->reset(); }
        for (auto* b : { &bassHp[c][0], &bassHp[c][1] }) { b->highpass (120.0f, 0.7071f, (float) sampleRate); b->reset(); }
    }
}

bool PrismProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != AudioChannelSet::stereo()) return false;
    return in == AudioChannelSet::stereo() || in == AudioChannelSet::mono();
}

pp::EngineParams PrismProcessor::readParams (int numSamples)
{
    pp::EngineParams p;

    // tempo: Ableton's when synced, otherwise a tapped tempo
    float bpm = f (ParamIDs::bpm);
    bool synced = false;
    if (on (ParamIDs::sync))
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm())
                    if (*b > 20.0) { bpm = (float) *b; synced = true; }
    uiBpm.store (bpm);
    uiSynced.store (synced);

    // Gesture runs at 100 points a second on top of each knob's own value
    {
        const int cmd = gestCmd.exchange (0);
        const SpinLock::ScopedTryLockType lock (gestureLock);
        if (lock.isLocked())
        {
            if (gestLoadReady.load())
            {
                for (int i = 0; i < pp::Gesture::kLanes && i < (int) gestLoad.size(); ++i) gesture.lane (i) = gestLoad[(size_t) i];
                gestLoadReady.store (false);
            }
            if (cmd & GestClear) gesture.clear();
            if (cmd & GestStart) gesture.startRecording();
            static const int bars[4] = { 0, 1, 2, 4 };
            if (cmd & GestStop) gesture.stopRecording (bpm, bars[jlimit (0, 3, idx (ParamIDs::gestQuant))]);
            const uint32_t erase = gestErase.exchange (0);
            for (int i = 0; i < pp::Gesture::kLanes; ++i)
                if (erase & (1u << i)) gesture.erase (i);

            float knobs[pp::Gesture::kLanes];
            for (int i = 0; i < pp::Gesture::kLanes; ++i) knobs[i] = f (kLaneParams[i]);
            const float speed = names::gestSpeedValue[jlimit (0, 4, idx (ParamIDs::gestSpeed))];
            gestAcc += (float) numSamples;
            const float tickSamples = (float) (getSampleRate() > 0.0 ? getSampleRate() : 48000.0) / pp::Gesture::kRate;
            bool ticked = false;
            while (gestAcc >= tickSamples) { gesture.tick (knobs, laneEffective.data(), bpm, speed); gestAcc -= tickSamples; ticked = true; }
            if (! ticked && ! gesture.anyActive())
                for (int i = 0; i < pp::Gesture::kLanes; ++i) laneEffective[(size_t) i] = knobs[i];
            for (int i = 0; i < pp::Gesture::kLanes; ++i)
            {
                uiLaneActive[(size_t) i].store (gesture.lane (i).active);
                uiLaneValue[(size_t) i].store (laneEffective[(size_t) i]);
            }
            uiGestRec.store (gesture.isRecording());
            uiGestLoops.store (gesture.activeCount());
        }
        else
            for (int i = 0; i < pp::Gesture::kLanes; ++i) laneEffective[(size_t) i] = f (kLaneParams[i]);
    }

    const auto& ord = names::orders()[(size_t) jlimit (0, 23, idx (ParamIDs::order))];
    for (int s = 0; s < 4; ++s) p.order[s] = ord[(size_t) s];
    for (int m = 0; m < 4; ++m)
    {
        p.fx[m] = jlimit (0, pp::kOff, idx (ParamIDs::fx[m]));
        p.on[m] = on (ParamIDs::on[m]);
        auto& c = p.ctl[m];
        c.knob = m < 3 ? laneEffective[(size_t) m] : 0.5f;
        c.amount = laneEffective[(size_t) (4 + m)];
        c.drift = m == pp::ModMovement ? f (ParamIDs::driftM) : (m == pp::ModDiffusion ? f (ParamIDs::driftD) : 0.0f);
        c.volDb = f (ParamIDs::vol[m]);
    }

    p.spareFx = idx (ParamIDs::spareFx) - 1;
    p.spareCtl.knob = f (ParamIDs::spareKnob);
    p.spareCtl.amount = f (ParamIDs::spareAmount);
    if (p.spareFx >= 0)
    {
        const int fam = p.spareFx / pp::kEffectsPerModule;
        p.spareCtl.drift = fam == pp::ModMovement ? f (ParamIDs::driftM) : (fam == pp::ModDiffusion ? f (ParamIDs::driftD) : 0.0f);
    }

    p.mix = laneEffective[3];
    p.trueDry = on (ParamIDs::trueDry);
    p.outDb = f (ParamIDs::output);
    p.calLevel = idx (ParamIDs::calLevel);
    p.bypass = on (ParamIDs::bypass);
    p.trails = on (ParamIDs::trails);
    p.capturePre = idx (ParamIDs::capturePre) == 1;
    p.captureOverdub = on (ParamIDs::captureOverdub);
    p.captureQuantise = on (ParamIDs::captureQuantise);
    p.shared.fs = (float) engine.sampleRate();
    p.shared.bpm = bpm;
    p.shared.tempoLocked = synced || tapLocked;
    p.shared.sens = f (ParamIDs::sens);
    p.shared.filterStyle = idx (ParamIDs::filterStyle);
    p.shared.inputMode = idx (ParamIDs::inputMode);
    return p;
}

void PrismProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || getTotalNumOutputChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);

    if (captureLoadReady.load())
    {
        engine.getCapture().load (capLoadL, capLoadR, capLoadPad);
        captureLoadReady.store (false);
    }

    for (const auto meta : midi) handleMidi (meta.getMessage());

    const auto p = readParams (n);
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    // Bass input: everything under 120 Hz stays dry and tight, the effects work above it
    const bool bass = p.shared.inputMode == 2;
    if (bass)
    {
        if ((int) lowL.size() < n) { lowL.resize ((size_t) n); lowR.resize ((size_t) n); }
        for (int i = 0; i < n; ++i)
        {
            lowL[(size_t) i] = bassLp[0][1].process (bassLp[0][0].process (L[i]));
            lowR[(size_t) i] = bassLp[1][1].process (bassLp[1][0].process (R[i]));
            L[i] = bassHp[0][1].process (bassHp[0][0].process (L[i]));
            R[i] = bassHp[1][1].process (bassHp[1][0].process (R[i]));
        }
    }

    engine.process (L, R, n, p);

    if (bass)
        for (int i = 0; i < n; ++i) { L[i] += lowL[(size_t) i]; R[i] += lowR[(size_t) i]; }
}

// =====================================================================================
// MIDI: footswitches, expression, presets and the original pedal's own CC map
// =====================================================================================
void PrismProcessor::queue (int kind, int a, float v)
{
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0) pending[(size_t) scope.startIndex1] = { kind, a, v, nullptr };
}

void PrismProcessor::queueParam (int kind, const char* id, float v)
{
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0) pending[(size_t) scope.startIndex1] = { kind, 0, v, id };
}

void PrismProcessor::handleMidi (const MidiMessage& m)
{
    const int ch = m.getChannel();
    if (m.isProgramChange())
    {
        if ((ch == settings.pedalMapChannel || ch == settings.fsChannel[0]) && hasFocus (this))
            queue (LoadPreset, m.getProgramChangeNumber(), 0.0f);
        return;
    }
    if (! m.isController()) return;
    const int cc = m.getControllerNumber(), val = m.getControllerValue();

    if (learnTarget.load() != LearnNone)
    {
        if (val > 0) queue (Learned, ch * 128 + cc, 0.0f);
        return;
    }

    if (hasFocus (this))
    {
        for (int k = 0; k < 4; ++k)
            if (ch == settings.fsChannel[(size_t) k] && cc == settings.fsCC[(size_t) k])
            {
                queue (val >= 64 ? FootPress : FootRelease, k, 0.0f);
                return;
            }
        if (ch == settings.expChannel && cc == settings.expCC)
        {
            queue (Expression, val, 0.0f);
            return;
        }
    }

    if (settings.pedalMapChannel == 0 || ch != settings.pedalMapChannel) return;

    // the original pedal's MIDI chart, so templates made for it work here
    const float v = (float) val / 127.0f;
    using namespace ParamIDs;
    auto fxFromCc = [&] (int module)
    {
        const int e = val >= 110 ? pp::kOff : val / 22;
        queueParam (SetPlain, ParamIDs::fx[module], (float) jlimit (0, pp::kOff, e));
    };
    switch (cc)
    {
        case 64: queueParam (SetNorm, ParamIDs::knob[0], v); break;
        case 66: queueParam (SetNorm, ParamIDs::knob[1], v); break;
        case 68: queueParam (SetNorm, ParamIDs::knob[2], v); break;
        case 70: queueParam (SetNorm, mix, v); break;
        case 65: queueParam (SetNorm, ParamIDs::amount[0], v); break;
        case 67: queueParam (SetNorm, ParamIDs::amount[1], v); break;
        case 69: queueParam (SetNorm, ParamIDs::amount[2], v); break;
        case 71: queueParam (SetNorm, ParamIDs::amount[3], v); break;
        case 72: queueParam (SetNorm, sens, v); break;
        case 74: queueParam (SetNorm, driftM, v); break;
        case 76: queueParam (SetNorm, driftD, v); break;
        case 78: queueParam (SetNorm, output, v); break;
        case 73: queueParam (SetNorm, ParamIDs::vol[0], v); break;
        case 75: queueParam (SetNorm, ParamIDs::vol[1], v); break;
        case 77: queueParam (SetNorm, ParamIDs::vol[2], v); break;
        case 79: queueParam (SetNorm, ParamIDs::vol[3], v); break;
        case 16: fxFromCc (0); break;
        case 17: fxFromCc (1); break;
        case 18: fxFromCc (2); break;
        case 19: fxFromCc (3); break;
        case 91: queueParam (SetPlain, bypass, val < 64 ? 1.0f : 0.0f); break;
        case 92: queueParam (SetPlain, bypass, val < 32 ? 1.0f : 0.0f); break;
        case 103: queueParam (SetPlain, ParamIDs::on[0], val >= 64 ? 1.0f : 0.0f); break;
        case 104: queueParam (SetPlain, ParamIDs::on[1], val >= 64 ? 1.0f : 0.0f); break;
        case 105: queueParam (SetPlain, ParamIDs::on[2], val >= 64 ? 1.0f : 0.0f); break;
        case 106: queueParam (SetPlain, ParamIDs::on[3], val >= 64 ? 1.0f : 0.0f); break;
        case 80: queue (GestureCC, val >= 64 ? 1 : 0, 0.0f); break;
        case 81: queue (GestureCC, 2, 0.0f); break;
        case 82: queue (CaptureCC, val < 44 ? 0 : (val < 88 ? 1 : 2), 0.0f); break;
        case 83: queueParam (SetPlain, capturePre, val >= 64 ? 1.0f : 0.0f); break;
        case 84: queueParam (SetPlain, filterStyle, val < 44 ? 1.0f : (val < 88 ? 0.0f : 2.0f)); break;
        case 93: queue (Tap, 0, 0.0f); break;
        case 94: queueParam (SetPlain, calLevel, (float) jlimit (0, 3, val / 32)); break;
        default: break;
    }
}

void PrismProcessor::setNorm (const String& id, float v01)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (jlimit (0.0f, 1.0f, v01));
        p->endChangeGesture();
    }
}

void PrismProcessor::setPlain (const String& id, float plain)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

void PrismProcessor::say (const String& s)
{
    uiMessage = s;
    uiMessageAt = Time::getMillisecondCounterHiRes() * 0.001;
}

void PrismProcessor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const auto scope = fifo.read (fifo.getNumReady());
    auto apply = [&] (int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& q = pending[(size_t) i];
            switch (q.kind)
            {
                case FootPress: footswitches.press (q.a, now); break;
                case FootRelease: footswitches.release (q.a, now); break;
                case SetNorm: setNorm (q.id, q.v); break;
                case SetPlain: setPlain (q.id, q.v); break;
                case LoadPreset: if (q.a >= 0 && q.a < kPresets) loadPreset (q.a); break;
                case Tap: tap(); break;
                case GestureCC:
                    if (q.a == 2) clearGestures();
                    else if ((q.a == 1) != gesture.isRecording()) toggleGestureRecord();
                    break;
                case CaptureCC:
                    if (q.a == 0) engine.postCommand (pp::PrismEngine::CmdCaptureClear);
                    else if (q.a == 2) engine.postCommand (pp::PrismEngine::CmdCaptureHold);
                    else if (engine.getCapture().uiState.load() == pp::Capture::Recording) engine.postCommand (pp::PrismEngine::CmdCaptureRelease);
                    break;
                case Expression:
                {
                    const int cc = (int) q.a;
                    expMovedAt = now;
                    if (calibrating)
                    {
                        if (calFirst < 0) calFirst = cc;
                        calLo = jmin (calLo, cc);
                        calHi = jmax (calHi, cc);
                        uiExp.store ((float) cc / 127.0f);
                        break;
                    }
                    const float v = settings.expressionValue (cc);
                    uiExp.store (v);
                    const auto& targets = names::expTargets();
                    const int t = jlimit (0, (int) targets.size() - 1, idx (ParamIDs::expTarget));
                    setNorm (targets[(size_t) t].id, v);
                    break;
                }
                case Learned:
                {
                    const int ch = q.a / 128, cc = q.a % 128;
                    const int what = learnTarget.exchange (LearnNone);
                    if (what >= 0 && what < 4)
                    {
                        settings.fsChannel[(size_t) what] = ch;
                        settings.fsCC[(size_t) what] = cc;
                        say ("FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + what)) + " IS CC " + String (cc));
                    }
                    else if (what == LearnExpression)
                    {
                        settings.expChannel = ch;
                        settings.expCC = cc;
                        say ("EXPRESSION IS CC " + String (cc));
                    }
                    settings.save();
                    break;
                }
                default: break;
            }
        }
    };
    apply (scope.startIndex1, scope.blockSize1);
    apply (scope.startIndex2, scope.blockSize2);

    footswitches.tick (now);
    int mask = 0;
    for (int k = 0; k < 4; ++k) if (footswitches.isDown (k)) mask |= 1 << k;
    uiFootDown.store (mask);

    // the on-screen Capture button: hold to record, tap to clear
    if (capDownAt > 0.0 && ! capHeldFired && ! capStartedOnDown && now - capDownAt > FootswitchLogic::kHoldSeconds)
    {
        engine.postCommand (pp::PrismEngine::CmdCaptureHold);
        capHeldFired = true;
    }

    if (autoCalUntil > 0.0)
    {
        autoCalPeak = jmax (autoCalPeak, engine.uiInput.load());
        if (now > autoCalUntil)
        {
            autoCalUntil = 0.0;
            if (autoCalPeak < 0.003f) say ("NOTHING HEARD, PLAY AND TRY AGAIN");
            else
            {
                const int level = autoCalPeak < 0.12f ? 0 : (autoCalPeak < 0.35f ? 1 : (autoCalPeak < 0.7f ? 2 : 3));
                setPlain (ParamIDs::calLevel, (float) level);
                static const char* const levelNames[4] = { "LOW", "MEDIUM", "HIGH", "VERY HIGH" };
                say (String ("HEADROOM SET TO ") + levelNames[level]);
            }
        }
    }

    if (calibrating && now > calibrateUntil)
    {
        calibrating = false;
        if (calHi - calLo > 20)
        {
            // whichever end the pedal started at is the heel
            const bool startedLow = std::abs (calFirst - calLo) < std::abs (calFirst - calHi);
            settings.heel = startedLow ? calLo : calHi;
            settings.toe = startedLow ? calHi : calLo;
            settings.save();
            say ("EXPRESSION CALIBRATED");
        }
        else
            say ("NO MOVEMENT, TRY AGAIN");
    }
}

// =====================================================================================
// Footswitches. Defaults follow the pedal's own: A bypasses, B taps tempo and holds
// for Capture. C and D are extras: Gesture and presets.
// =====================================================================================
static bool captureIdle (int st) { return st == pp::Capture::Empty; }

void PrismProcessor::onFootEvent (int sw, FootswitchLogic::Event e)
{
    using E = FootswitchLogic;
    if (e == E::Down || e == E::ComboAB || e == E::ComboCD) uiFootPulse.fetch_add (1);
    const int capState = engine.getCapture().uiState.load();

    if (e == E::ComboAB)
    {
        if (captureStartedOnDown) { engine.postCommand (pp::PrismEngine::CmdCaptureClear); captureStartedOnDown = false; }
        uiDeepToggle.fetch_add (1);
        return;
    }
    if (e == E::ComboCD)
    {
        // hand the footswitches to the next Prism Patch in the set
        const SpinLock::ScopedLockType l (focusLock);
        const int i = instances.indexOf (this);
        auto* next = instances.isEmpty() ? nullptr : instances[(i + 1) % instances.size()];
        focused.store (next);
        if (next != nullptr) next->say ("PEDALS FOCUS HERE");
        return;
    }

    switch (sw)
    {
        case 0:
            if (e == E::Tap) dualBypass();
            if (e == E::HoldStart) { setPlain (ParamIDs::bypass, on (ParamIDs::bypass) ? 0.0f : 1.0f); say (on (ParamIDs::bypass) ? "ALL BYPASSED" : "ALL ON"); }
            break;
        case 1:
            if (e == E::Down && captureIdle (capState))
            {
                // start listening straight away, so a hold catches the first note
                engine.postCommand (pp::PrismEngine::CmdCaptureHold);
                captureStartedOnDown = true;
            }
            if (e == E::Tap)
            {
                if (captureStartedOnDown) { engine.postCommand (pp::PrismEngine::CmdCaptureClear); tap(); }
                else { engine.postCommand (pp::PrismEngine::CmdCaptureClear); say ("CAPTURE CLEARED"); }
                captureStartedOnDown = false;
            }
            if (e == E::HoldStart && ! captureStartedOnDown) engine.postCommand (pp::PrismEngine::CmdCaptureHold);
            if (e == E::HoldEnd) { engine.postCommand (pp::PrismEngine::CmdCaptureRelease); captureStartedOnDown = false; }
            break;
        case 2:
            if (e == E::Tap) toggleGestureRecord();
            if (e == E::HoldStart) clearGestures();
            break;
        case 3:
            if (e == E::Tap) stepPreset (1);
            if (e == E::HoldStart) stepPreset (-1);
            break;
        default: break;
    }
}

void PrismProcessor::footDown (int sw)
{
    focused.store (this);
    footswitches.press (sw, Time::getMillisecondCounterHiRes() * 0.001);
}

void PrismProcessor::footUp (int sw) { footswitches.release (sw, Time::getMillisecondCounterHiRes() * 0.001); }

void PrismProcessor::dualBypass()
{
    // Modules in the bypass group switch together. With all four in the group it is a plain bypass.
    bool all = true, anyOn = false;
    for (int m = 0; m < 4; ++m)
    {
        all = all && on (ParamIDs::group[m]);
        if (on (ParamIDs::group[m]) && on (ParamIDs::on[m])) anyOn = true;
    }
    if (all)
    {
        const bool nowBypassed = ! on (ParamIDs::bypass);
        setPlain (ParamIDs::bypass, nowBypassed ? 1.0f : 0.0f);
        say (nowBypassed ? "BYPASSED" : "ON");
        return;
    }
    if (on (ParamIDs::bypass)) setPlain (ParamIDs::bypass, 0.0f);
    for (int m = 0; m < 4; ++m)
        if (on (ParamIDs::group[m])) setPlain (ParamIDs::on[m], anyOn ? 0.0f : 1.0f);
    say (anyOn ? "GROUP OFF" : "GROUP ON");
}

void PrismProcessor::captureDown()
{
    focused.store (this);
    capDownAt = Time::getMillisecondCounterHiRes() * 0.001;
    capHeldFired = false;
    capStartedOnDown = captureIdle (engine.getCapture().uiState.load());
    if (capStartedOnDown) engine.postCommand (pp::PrismEngine::CmdCaptureHold);
}

void PrismProcessor::captureUp()
{
    if (capDownAt <= 0.0) return;
    if (capStartedOnDown || capHeldFired) engine.postCommand (pp::PrismEngine::CmdCaptureRelease);
    else engine.postCommand (pp::PrismEngine::CmdCaptureClear);
    capDownAt = -1.0;
    capHeldFired = capStartedOnDown = false;
}

void PrismProcessor::tap()
{
    const double t = Time::getMillisecondCounterHiRes() * 0.001;
    if (! taps.empty() && t - taps.back() > 2.0) taps.clear();
    taps.push_back (t);
    if (taps.size() > 5) taps.erase (taps.begin());
    if (taps.size() >= 2)
    {
        const double avg = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        setPlain (ParamIDs::bpm, (float) jlimit (40.0, 240.0, 60.0 / avg));
        setPlain (ParamIDs::sync, 0.0f); // tapping takes over from Ableton's tempo
        tapLocked = true;
        say ("TEMPO " + String (roundToInt (60.0 / avg)) + " BPM");
    }
    else say ("TAP AGAIN FOR TEMPO");
}

// =====================================================================================
// Gesture
// =====================================================================================
void PrismProcessor::toggleGestureRecord()
{
    const bool rec = uiGestRec.load();
    gestCmd.fetch_or (rec ? GestStop : GestStart);
    uiGestRec.store (! rec);
    say (rec ? "GESTURES LOOPING" : "MOVE ANY KNOB");
}

void PrismProcessor::clearGestures()
{
    gestCmd.fetch_or (GestClear);
    uiGestRec.store (false);
    say ("GESTURES ERASED");
}

void PrismProcessor::touchKnob (const String& paramId)
{
    focused.store (this);
    const int lane = laneFor (paramId);
    if (lane >= 0 && ! uiGestRec.load() && uiLaneActive[(size_t) lane].load())
        gestErase.fetch_or (1u << lane);
}

std::array<int, 4> PrismProcessor::currentOrder() const
{
    return names::orders()[(size_t) jlimit (0, 23, idx (ParamIDs::order))];
}

void PrismProcessor::moveModule (int slot, int direction)
{
    auto o = currentOrder();
    const int to = slot + direction;
    if (slot < 0 || slot > 3 || to < 0 || to > 3) return;
    std::swap (o[(size_t) slot], o[(size_t) to]);
    setPlain (ParamIDs::order, (float) names::orderIndex (o));
}

// =====================================================================================
// Footswitch learn and expression calibration
// =====================================================================================
void PrismProcessor::learnFootswitch (int sw)
{
    learnTarget.store (sw);
    say ("PRESS FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + sw)));
}

void PrismProcessor::learnExpression()
{
    learnTarget.store (LearnExpression);
    say ("MOVE THE EXPRESSION PEDAL");
}

void PrismProcessor::calibrateExpression()
{
    calibrating = true;
    calibrateUntil = Time::getMillisecondCounterHiRes() * 0.001 + 6.0;
    calLo = 127; calHi = 0; calFirst = -1;
    say ("HEEL DOWN, THEN ROCK TO TOE");
}

void PrismProcessor::autoCalibrate()
{
    autoCalUntil = Time::getMillisecondCounterHiRes() * 0.001 + 3.0;
    autoCalPeak = 0.0f;
    say ("PLAY AT YOUR NORMAL VOLUME");
}

void PrismProcessor::resetFootswitches()
{
    settings.resetFootswitches();
    settings.save();
    say ("FOOTSWITCH DEFAULTS RESTORED");
}

// =====================================================================================
// Presets: 80 slots in banks A to D. Each saves the effects, routing, every knob,
// the Gestures, the bypass group, Capture routing, expression target and tempo.
// =====================================================================================
bool PrismProcessor::inSnapshot (const String& id)
{
    static const StringArray excluded { ParamIDs::bypass, ParamIDs::sync, ParamIDs::inputMode, ParamIDs::calLevel };
    return ! excluded.contains (id);
}

ValueTree PrismProcessor::gestureTree()
{
    ValueTree t ("Gesture");
    const SpinLock::ScopedLockType lock (gestureLock);
    for (int i = 0; i < pp::Gesture::kLanes; ++i)
    {
        const auto& l = gesture.lane (i);
        ValueTree lt ("Lane");
        lt.setProperty ("active", l.active, nullptr);
        if (l.active && ! l.points.empty())
        {
            MemoryBlock mb (l.points.data(), l.points.size() * sizeof (float));
            lt.setProperty ("points", mb.toBase64Encoding(), nullptr);
            lt.setProperty ("anchor", l.anchor, nullptr);
            lt.setProperty ("bpm", l.recordBpm, nullptr);
            lt.setProperty ("beats", l.lengthBeats, nullptr);
        }
        t.appendChild (lt, nullptr);
    }
    return t;
}

void PrismProcessor::queueGestureLoad (const ValueTree& t)
{
    std::vector<pp::Gesture::Lane> lanes ((size_t) pp::Gesture::kLanes);
    for (int i = 0; i < pp::Gesture::kLanes && t.isValid() && i < t.getNumChildren(); ++i)
    {
        const auto lt = t.getChild (i);
        auto& l = lanes[(size_t) i];
        MemoryBlock mb;
        if ((bool) lt.getProperty ("active", false) && mb.fromBase64Encoding (lt.getProperty ("points").toString()))
        {
            l.points.resize (mb.getSize() / sizeof (float));
            std::memcpy (l.points.data(), mb.getData(), l.points.size() * sizeof (float));
            l.anchor = (float) lt.getProperty ("anchor", 0.5f);
            l.recordBpm = (float) lt.getProperty ("bpm", 120.0f);
            l.lengthBeats = (float) lt.getProperty ("beats", 0.0f);
            l.active = ! l.points.empty();
        }
    }
    {
        const SpinLock::ScopedLockType lock (gestureLock);
        gestLoad.swap (lanes);
        gestLoadReady.store (true);
    }
}

ValueTree PrismProcessor::snapshotOf()
{
    ValueTree t ("Snapshot");
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            if (inSnapshot (rp->getParameterID()))
                t.setProperty (Identifier (rp->getParameterID()), rp->convertFrom0to1 (rp->getValue()), nullptr);
    t.setProperty ("tapLocked", tapLocked, nullptr);
    t.appendChild (gestureTree(), nullptr);
    return t;
}

void PrismProcessor::applySnapshot (const ValueTree& t)
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
        {
            const Identifier id (rp->getParameterID());
            if (inSnapshot (rp->getParameterID()) && t.hasProperty (id))
                setPlain (rp->getParameterID(), (float) t.getProperty (id));
        }
    tapLocked = (bool) t.getProperty ("tapLocked", tapLocked);
    queueGestureLoad (t.getChildWithName ("Gesture"));
}

void PrismProcessor::savePreset (int slot)
{
    if (slot < 0 || slot >= kPresets) return;
    slots[(size_t) slot].params = snapshotOf();
    selected = slot;
    say ("SAVED " + String::charToString ((juce_wchar) ('A' + slot / kPerBank)) + " " + String (slot % kPerBank + 1).paddedLeft ('0', 2));
}

void PrismProcessor::loadPreset (int slot)
{
    if (! hasPreset (slot)) return;
    applySnapshot (slots[(size_t) slot].params);
    selected = slot;
    say ("LOADED " + String::charToString ((juce_wchar) ('A' + slot / kPerBank)) + " " + String (slot % kPerBank + 1).paddedLeft ('0', 2));
}

void PrismProcessor::clearPreset (int slot)
{
    if (slot < 0 || slot >= kPresets) return;
    slots[(size_t) slot].params = {};
}

bool PrismProcessor::hasPreset (int slot) const
{
    return slot >= 0 && slot < kPresets && slots[(size_t) slot].params.isValid();
}

void PrismProcessor::stepPreset (int direction)
{
    for (int k = 1; k <= kPresets; ++k)
    {
        const int s = ((selected < 0 ? (direction > 0 ? -1 : 0) : selected) + direction * k + kPresets * 2) % kPresets;
        if (hasPreset (s)) { loadPreset (s); return; }
    }
    say ("NO PRESETS SAVED YET");
}

// =====================================================================================
// Saving with the set: parameters, presets, Gestures and the Capture loop
// =====================================================================================
String PrismProcessor::encodeAudio (const std::vector<float>& l, const std::vector<float>& r)
{
    if (l.empty()) return {};
    MemoryOutputStream packed;
    {
        GZIPCompressorOutputStream zip (packed, 6);
        for (size_t i = 0; i < l.size(); ++i)
        {
            zip.writeShort ((short) jlimit (-32767, 32767, roundToInt (l[i] * 32767.0f)));
            zip.writeShort ((short) jlimit (-32767, 32767, roundToInt (r[i] * 32767.0f)));
        }
    }
    return packed.getMemoryBlock().toBase64Encoding();
}

bool PrismProcessor::decodeAudio (const String& s, std::vector<float>& l, std::vector<float>& r)
{
    l.clear(); r.clear();
    if (s.isEmpty()) return false;
    MemoryBlock packed;
    if (! packed.fromBase64Encoding (s)) return false;
    MemoryInputStream in (packed, false);
    GZIPDecompressorInputStream unzip (in);
    MemoryBlock data;
    unzip.readIntoMemoryBlock (data);
    const size_t frames = data.getSize() / 4;
    MemoryInputStream pcm (data, false);
    l.resize (frames); r.resize (frames);
    for (size_t i = 0; i < frames; ++i)
    {
        l[i] = (float) pcm.readShort() / 32767.0f;
        r[i] = (float) pcm.readShort() / 32767.0f;
    }
    return frames > 0;
}

static void resampleTo (std::vector<float>& v, double from, double to)
{
    if (v.empty() || from <= 0.0 || std::abs (from - to) < 1.0) return;
    const double ratio = from / to;
    std::vector<float> out ((size_t) ((double) v.size() / ratio));
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double p = (double) i * ratio;
        const size_t a = (size_t) p;
        const float fr = (float) (p - (double) a);
        out[i] = v[a] + (v[std::min (a + 1, v.size() - 1)] - v[a]) * fr;
    }
    v.swap (out);
}

void PrismProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    for (int i = state.getNumChildren(); --i >= 0;)
    {
        const auto type = state.getChild (i).getType().toString();
        if (type == "Presets" || type == "Capture" || type == "Gesture") state.removeChild (i, nullptr);
    }

    ValueTree presets ("Presets");
    presets.setProperty ("selected", selected, nullptr);
    for (int i = 0; i < kPresets; ++i)
    {
        ValueTree slot ("Slot");
        if (slots[(size_t) i].params.isValid()) slot.appendChild (slots[(size_t) i].params.createCopy(), nullptr);
        presets.appendChild (slot, nullptr);
    }
    state.appendChild (presets, nullptr);
    state.appendChild (gestureTree(), nullptr);
    state.setProperty ("tapLocked", tapLocked, nullptr);

    // the Capture loop saves with the set, unlike on the pedal
    const auto& cap = engine.getCapture();
    if (cap.hasAudio())
    {
        ValueTree c ("Capture");
        std::vector<float> l, r;
        cap.copyOut (l, r);
        c.setProperty ("rate", engine.sampleRate(), nullptr);
        c.setProperty ("pad", cap.isPad(), nullptr);
        c.setProperty ("data", encodeAudio (l, r), nullptr);
        state.appendChild (c, nullptr);
    }

    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void PrismProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = ValueTree::fromXml (*xml);

    const auto presets = tree.getChildWithName ("Presets");
    if (presets.isValid())
    {
        selected = (int) presets.getProperty ("selected", -1);
        for (int i = 0; i < kPresets && i < presets.getNumChildren(); ++i)
        {
            const auto slot = presets.getChild (i);
            slots[(size_t) i].params = slot.getNumChildren() > 0 ? slot.getChild (0).createCopy() : ValueTree();
        }
        tree.removeChild (presets, nullptr);
    }

    const auto g = tree.getChildWithName ("Gesture");
    if (g.isValid()) { queueGestureLoad (g); tree.removeChild (g, nullptr); }
    tapLocked = (bool) tree.getProperty ("tapLocked", false);

    const auto c = tree.getChildWithName ("Capture");
    if (c.isValid())
    {
        std::vector<float> l, r;
        if (decodeAudio (c.getProperty ("data").toString(), l, r) && ! captureLoadReady.load())
        {
            const double rate = (double) c.getProperty ("rate", 48000.0);
            const double now = getSampleRate() > 0.0 ? getSampleRate() : rate;
            resampleTo (l, rate, now);
            resampleTo (r, rate, now);
            capLoadL.swap (l);
            capLoadR.swap (r);
            capLoadPad = (bool) c.getProperty ("pad", false);
            captureLoadReady.store (true);
        }
        tree.removeChild (c, nullptr);
    }

    apvts.replaceState (tree);
}

AudioProcessorEditor* PrismProcessor::createEditor()
{
    return new PrismEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PrismProcessor();
}
