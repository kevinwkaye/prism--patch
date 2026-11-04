#include "PedalControl.h"

using namespace juce;

File PedalSettings::file()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
        .getChildFile ("Kevin Kaye").getChildFile ("Flip Fella Pedals.xml");
}

void PedalSettings::load()
{
    auto xml = parseXML (file());
    if (xml == nullptr || ! xml->hasTagName ("Pedals")) return;
    for (int i = 0; i < 4; ++i)
    {
        fsChannel[(size_t) i] = jlimit (1, 16, xml->getIntAttribute ("fsChannel" + String (i), fsChannel[(size_t) i]));
        fsCC[(size_t) i] = jlimit (0, 127, xml->getIntAttribute ("fsCC" + String (i), fsCC[(size_t) i]));
    }
    expChannel = jlimit (1, 16, xml->getIntAttribute ("expChannel", expChannel));
    expCC = jlimit (0, 127, xml->getIntAttribute ("expCC", expCC));
    heel = jlimit (0, 127, xml->getIntAttribute ("heel", heel));
    toe = jlimit (0, 127, xml->getIntAttribute ("toe", toe));
    curve = jlimit (0, 2, xml->getIntAttribute ("curve", curve));
    pedalMapChannel = jlimit (0, 16, xml->getIntAttribute ("pedalMapChannel", pedalMapChannel));
    keeperChannel = jlimit (0, 16, xml->getIntAttribute ("keeperChannel", keeperChannel));
    keeperCC = jlimit (-1, 127, xml->getIntAttribute ("keeperCC", keeperCC));
    keeperLive = xml->getBoolAttribute ("keeperLive", keeperLive);
}

void PedalSettings::save() const
{
    XmlElement xml ("Pedals");
    for (int i = 0; i < 4; ++i)
    {
        xml.setAttribute ("fsChannel" + String (i), fsChannel[(size_t) i]);
        xml.setAttribute ("fsCC" + String (i), fsCC[(size_t) i]);
    }
    xml.setAttribute ("expChannel", expChannel);
    xml.setAttribute ("expCC", expCC);
    xml.setAttribute ("heel", heel);
    xml.setAttribute ("toe", toe);
    xml.setAttribute ("curve", curve);
    xml.setAttribute ("pedalMapChannel", pedalMapChannel);
    xml.setAttribute ("keeperChannel", keeperChannel);
    xml.setAttribute ("keeperCC", keeperCC);
    xml.setAttribute ("keeperLive", keeperLive);
    file().getParentDirectory().createDirectory();
    xml.writeTo (file());
}

void PedalSettings::resetFootswitches()
{
    fsChannel = { 16, 16, 16, 16 };
    fsCC = { 20, 21, 22, 23 };
}

float PedalSettings::expressionValue (int cc) const
{
    // a reversed or short-range pedal is fixed by the calibrated heel and toe
    const float span = (float) (toe - heel);
    float v = std::abs (span) < 4.0f ? (float) cc / 127.0f : ((float) cc - (float) heel) / span;
    v = jlimit (0.0f, 1.0f, v);
    if (curve == 1) v = v * v;
    else if (curve == 2) v = std::sqrt (v);
    return v;
}

// ---------------------------------------------------------------------------------

void FootswitchLogic::press (int sw, double now)
{
    if (sw < 0 || sw > 3 || down[(size_t) sw]) return;
    down[(size_t) sw] = true;
    held[(size_t) sw] = false;
    combo[(size_t) sw] = false;
    downAt[(size_t) sw] = now;

    // A with B, or C with D, pressed together
    const int partner = sw ^ 1;
    if (down[(size_t) partner] && ! held[(size_t) partner] && now - downAt[(size_t) partner] < kComboWindow)
    {
        combo[(size_t) sw] = combo[(size_t) partner] = true;
        comboAt[(size_t) (sw / 2)] = now;
        comboHeld[(size_t) (sw / 2)] = false;
        if (onEvent) onEvent (sw < 2 ? 0 : 2, sw < 2 ? ComboAB : ComboCD);
        return;
    }
    if (onEvent) onEvent (sw, Down);
}

void FootswitchLogic::release (int sw, double now)
{
    if (sw < 0 || sw > 3 || ! down[(size_t) sw]) return;
    tick (now);
    down[(size_t) sw] = false;
    if (combo[(size_t) sw]) return;
    if (onEvent) onEvent (sw, held[(size_t) sw] ? HoldEnd : Tap);
}

void FootswitchLogic::tick (double now)
{
    // both switches of a combo still down after a second: the combo hold
    for (int pair = 0; pair < 2; ++pair)
    {
        const int a = pair * 2, b = a + 1;
        if (down[(size_t) a] && down[(size_t) b] && combo[(size_t) a] && combo[(size_t) b]
            && ! comboHeld[(size_t) pair] && now - comboAt[(size_t) pair] >= kComboHoldSeconds)
        {
            comboHeld[(size_t) pair] = true;
            if (onEvent) onEvent (a, pair == 0 ? ComboHoldAB : ComboHoldCD);
        }
    }
    for (int i = 0; i < 4; ++i)
        if (down[(size_t) i] && ! held[(size_t) i] && ! combo[(size_t) i] && now - downAt[(size_t) i] >= kHoldSeconds)
        {
            held[(size_t) i] = true;
            if (onEvent) onEvent (i, HoldStart);
        }
}
