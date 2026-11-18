#include "Parameters.h"
#include "DSP/Scale.h"

namespace humrig
{
using APVTS = juce::AudioProcessorValueTreeState;

static juce::String pct (float v, int) { return juce::String (juce::roundToInt (v)); }
static juce::String dbStr (float v, int) { return v <= kMixOffDb + 0.05f ? juce::String ("Off") : juce::String (v, 1) + " dB"; }

APVTS::ParameterLayout createParameterLayout()
{
    using P = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using C = juce::AudioParameterChoice;
    using PID = juce::ParameterID;
    constexpr int v = 1;

    juce::StringArray keys, scales;
    for (auto* k : kKeyNames) keys.add (k);
    for (auto* s : kScaleNames) scales.add (s);

    const auto pctAttr = juce::AudioParameterFloatAttributes().withStringFromValueFunction (pct);
    const auto dbAttr  = juce::AudioParameterFloatAttributes().withStringFromValueFunction (dbStr);
    const auto fullRange = [] (float lo, float hi) { return juce::NormalisableRange<float> (lo, hi, 0.1f); };

    APVTS::ParameterLayout l;
    l.add (std::make_unique<B> (PID (ID::tuneOn, v), "Pitch Correction", true));
    l.add (std::make_unique<C> (PID (ID::key, v), "Key", keys, 0));
    l.add (std::make_unique<C> (PID (ID::scale, v), "Scale", scales, 1));
    l.add (std::make_unique<P> (PID (ID::speed, v), "Speed", fullRange (0, 100), 60.f, pctAttr));
    l.add (std::make_unique<P> (PID (ID::vibrato, v), "Vibrato", fullRange (0, 100), 0.f, pctAttr));
    l.add (std::make_unique<P> (PID (ID::flex, v), "Flex", fullRange (0, 100), 20.f, pctAttr));
    l.add (std::make_unique<C> (PID (ID::range, v), "Range", juce::StringArray { "Auto", "Soprano", "Alto / Tenor", "Bass" }, 0));
    l.add (std::make_unique<C> (PID (ID::voice, v), "Voice", juce::StringArray { "Male", "Female" }, 1));

    l.add (std::make_unique<P> (PID (ID::warmth, v), "Warmth", fullRange (0, 100), 50.f, pctAttr));
    l.add (std::make_unique<P> (PID (ID::presence, v), "Presence", fullRange (0, 100), 50.f, pctAttr));
    l.add (std::make_unique<P> (PID (ID::air, v), "Air", fullRange (0, 100), 50.f, pctAttr));
    l.add (std::make_unique<P> (PID (ID::comp, v), "Comp", fullRange (0, 100), 40.f, pctAttr));
    l.add (std::make_unique<P> (PID (ID::deess, v), "De-Esser", fullRange (0, 100), 40.f, pctAttr));
    l.add (std::make_unique<P> (PID (ID::drive, v), "Drive", fullRange (0, 100), 15.f, pctAttr));

    l.add (std::make_unique<P> (PID (ID::revMix, v), "Reverb Mix", fullRange (kMixOffDb, 0), -30.f, dbAttr));
    l.add (std::make_unique<P> (PID (ID::revSize, v), "Reverb Size", juce::NormalisableRange<float> (0.f, 1.f, 0.01f), 0.53f));
    l.add (std::make_unique<P> (PID (ID::revDecay, v), "Reverb Decay",
                                juce::NormalisableRange<float> (0.2f, 8.f, 0.01f, 0.5f), 2.1f,
                                juce::AudioParameterFloatAttributes().withLabel ("s")));
    l.add (std::make_unique<P> (PID (ID::dlyMix, v), "Delay Mix", fullRange (kMixOffDb, 0), -30.f, dbAttr));
    l.add (std::make_unique<P> (PID (ID::dlyFb, v), "Delay Feedback", fullRange (0, 90), 60.f, pctAttr));
    l.add (std::make_unique<C> (PID (ID::dlyTime, v), "Delay Time", juce::StringArray { "1/4", "1/8", "1/16" }, 0));
    l.add (std::make_unique<B> (PID (ID::dlyPing, v), "Ping-pong", true));

    l.add (std::make_unique<P> (PID (ID::inGain, v), "Input", fullRange (-24, 24), 0.f,
                                juce::AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<P> (PID (ID::outGain, v), "Output", fullRange (-24, 12), 0.f,
                                juce::AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<P> (PID (ID::mix, v), "Dry/Wet", fullRange (0, 100), 100.f, pctAttr));
    return l;
}

//                  name              warm pres air  comp ds  drv | revMix size decay | dlyMix fb  time ping | spd vib flex
static const Preset kPresets[] = {
    { "Default",        50, 50, 50, 40, 40, 15,  -30.f, 0.53f, 2.1f,  -34.f, 45, 0, true,   60,  0, 20 },
    { "Pop",            45, 62, 65, 55, 50, 20,  -26.f, 0.50f, 1.6f,  -34.f, 30, 1, true,   75, 10, 10 },
    { "Bright & Airy",  40, 70, 78, 45, 55, 10,  -22.f, 0.65f, 2.4f,  -36.f, 35, 1, true,   70, 15, 25 },
    { "Ballad / Soft",  62, 48, 52, 35, 35,  8,  -20.f, 0.80f, 3.8f,  -38.f, 40, 0, false,  40, 25, 45 },
    { "K-Pop",          42, 66, 72, 62, 55, 22,  -27.f, 0.55f, 1.8f,  -32.f, 35, 1, true,   85, 10,  8 },
    { "Pop Intimate",   66, 52, 48, 50, 45, 12,  -34.f, 0.35f, 1.0f,  kMixOffDb, 30, 1, false, 55, 12, 35 },
    { "Hard Tune",      48, 58, 58, 60, 45, 25,  -28.f, 0.45f, 1.5f,  -34.f, 30, 1, true,  100,  0,  0 },
    { "Hum Cleanup",    55, 55, 45, 50, 20, 10,  -40.f, 0.30f, 1.2f,  kMixOffDb, 30, 1, false, 65,  0, 30 },
    { "Dreamy Space",   55, 45, 70, 40, 40, 12,  -14.f, 0.90f, 5.5f,  -26.f, 55, 0, true,   50, 30, 30 },
};

int getNumPresets() { return (int) (sizeof (kPresets) / sizeof (kPresets[0])); }
const Preset& getPreset (int i) { return kPresets[juce::jlimit (0, getNumPresets() - 1, i)]; }
} // namespace humrig
