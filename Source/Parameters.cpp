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

//                  genre name        warm pres air  comp ds  drv | revMix size decay | dlyMix fb  time ping | spd vib flex
static const Preset kPresets[] = {
    { "General", "Default",        50, 50, 50, 40, 40, 15,  -30.f, 0.53f, 2.1f,  -34.f, 45, 0, true,   60,  0, 20 },
    { "General", "Pop",            45, 62, 65, 55, 50, 20,  -26.f, 0.50f, 1.6f,  -34.f, 30, 1, true,   75, 10, 10 },
    { "General", "Bright & Airy",  40, 70, 78, 45, 55, 10,  -22.f, 0.65f, 2.4f,  -36.f, 35, 1, true,   70, 15, 25 },
    { "General", "Ballad / Soft",  62, 48, 52, 35, 35,  8,  -20.f, 0.80f, 3.8f,  -38.f, 40, 0, false,  40, 25, 45 },
    { "General", "K-Pop",          42, 66, 72, 62, 55, 22,  -27.f, 0.55f, 1.8f,  -32.f, 35, 1, true,   85, 10,  8 },
    { "General", "Pop Intimate",   66, 52, 48, 50, 45, 12,  -34.f, 0.35f, 1.0f,  kMixOffDb, 30, 1, false, 55, 12, 35 },
    { "General", "Hard Tune",      48, 58, 58, 60, 45, 25,  -28.f, 0.45f, 1.5f,  -34.f, 30, 1, true,  100,  0,  0 },
    { "General", "Hum Cleanup",    55, 55, 45, 50, 20, 10,  -40.f, 0.30f, 1.2f,  kMixOffDb, 30, 1, false, 65,  0, 30 },
    { "General", "Dreamy Space",   55, 45, 70, 40, 40, 12,  -14.f, 0.90f, 5.5f,  -26.f, 55, 0, true,   50, 30, 30 },
    // ---------------------------------------------------------------------------------------------------
    // Artist-inspired presets. Source: deep-research report on publicly documented vocal chains of Billboard
    // artists. The DIRECTION of each setting follows documented practice (what is dry/wet, compressed hard or
    // gently, tuned hard or not at all); the exact knob values are interpretation, not measurements.
    // Delay times are limited to 1/4, 1/8, 1/16: half-note and 30-80 ms slapback are approximated by 1/4 and 1/16.
    // Artists with thin documentation are flagged "low confidence" in INSTALL_FA.md.
    //  genre  name                       warm pres air  comp ds drv |  revMix size  decay | dlyMix fb  time ping | spd vib flex
    { "Pop", "Olivia Rodrigo",            40, 55, 60,  55, 60, 18,  -24.f, 0.55f, 2.0f,  -30.f, 30, 1, true,   70, 12, 25 },
    { "Pop", "Billie Eilish",             40, 45, 50,  35, 55, 22,  -48.f, 0.20f, 0.6f,  kMixOffDb, 20, 1, false, 20,  0, 80 },
    { "Pop", "Taylor Swift",              45, 55, 70,  45, 55, 30,  -22.f, 0.60f, 1.8f,  -32.f, 30, 0, false,  65, 10, 30 },
    { "Pop", "Ariana Grande",             45, 55, 72,  50, 50, 12,  -26.f, 0.55f, 1.7f,  -36.f, 30, 1, true,   75, 25, 20 },
    { "Pop", "The Weeknd",                50, 55, 62,  15, 65, 20,  -16.f, 0.75f, 3.2f,  -26.f, 40, 0, true,   85, 15, 15 },

    { "Rap", "Future",                    35, 70, 60,  75, 70, 40,  -16.f, 0.35f, 0.75f, -24.f, 30, 0, false, 100,  0,  0 },
    { "Rap", "Lil Baby",                  30, 72, 62,  70, 75, 35,  -22.f, 0.80f, 2.2f,  -28.f, 25, 0, false,  95,  0,  5 },
    { "Rap", "Kendrick Lamar",            40, 60, 65,  60, 60, 40,  -28.f, 0.40f, 1.0f,  -26.f, 30, 0, false,  90,  8, 10 },
    { "Rap", "21 Savage",                 32, 66, 50,  78, 75, 42,  -40.f, 0.15f, 0.5f,  kMixOffDb, 20, 1, false, 100, 0,  0 },
    { "Rap", "Eminem",                    40, 72, 50,  82, 55, 45,  -28.f, 0.30f, 0.7f,  -22.f, 20, 1, false,  20,  0, 80 },

    { "Rock", "Panic! At The Disco",      52, 62, 66,  70, 55, 35,  -22.f, 0.60f, 1.8f,  -26.f, 40, 0, true,   70, 25, 25 },
    { "Rock", "Paramore",                 50, 60, 60,  62, 50, 35,  -22.f, 0.55f, 1.6f,  -30.f, 30, 1, false,  70, 20, 25 },
    { "Rock", "Foo Fighters",             58, 58, 45,  55, 40, 40,  -34.f, 0.30f, 0.8f,  -36.f, 20, 2, false,  25,  0, 70 },
    { "Rock", "Twenty One Pilots",        50, 62, 58,  65, 55, 35,  -26.f, 0.50f, 1.4f,  -30.f, 30, 1, false,  85, 15, 15 },
    { "Rock", "Imagine Dragons",          50, 58, 65,  55, 45, 22,  -20.f, 0.75f, 2.8f,  -30.f, 35, 0, true,   75, 20, 25 },

    { "Classic Rock", "Stevie Nicks / Fleetwood Mac", 72, 48, 38, 30, 20, 35, -30.f, 0.40f, 1.3f, kMixOffDb, 20, 1, false, 15, 0, 90 },
    { "Classic Rock", "Don Henley / Eagles",          62, 52, 40, 32, 20, 35, -32.f, 0.40f, 1.2f, -34.f, 20, 2, true,  15, 0, 90 },
    { "Classic Rock", "Freddie Mercury / Queen",      55, 62, 52, 40, 25, 55, -20.f, 0.70f, 2.2f, -34.f, 25, 2, true,  15, 0, 90 },
    { "Classic Rock", "Brian Johnson / AC-DC",        58, 66, 42, 50, 20, 42, -50.f, 0.20f, 0.6f, kMixOffDb, 20, 1, false, 15, 0, 90 },
    { "Classic Rock", "Robert Plant / Led Zeppelin",  58, 62, 45, 38, 20, 40, -30.f, 0.45f, 1.4f, -26.f, 25, 2, false, 15, 0, 90 },

    { "Metal", "James Hetfield / Metallica",  40, 66, 60,  80, 60, 60,  -38.f, 0.30f, 0.8f,  -34.f, 25, 2, false,  30,  0, 60 },
    { "Metal", "August Burns Red",            30, 62, 72,  85, 65, 70,  -36.f, 0.25f, 0.7f,  -34.f, 20, 2, false,  40,  0, 50 },
    { "Metal", "Bring Me The Horizon",        40, 64, 62,  88, 65, 62,  -20.f, 0.80f, 3.0f,  -24.f, 40, 1, true,   60, 20, 30 },
    { "Metal", "Lamb of God",                 38, 66, 52,  78, 55, 72,  -42.f, 0.20f, 0.6f,  kMixOffDb, 20, 1, false, 20,  0, 70 },
    { "Metal", "David Draiman / Disturbed",   46, 62, 55,  75, 55, 55,  -30.f, 0.50f, 1.4f,  -32.f, 25, 1, false,  45, 25, 40 },
};

int getNumPresets() { return (int) (sizeof (kPresets) / sizeof (kPresets[0])); }
const Preset& getPreset (int i) { return kPresets[juce::jlimit (0, getNumPresets() - 1, i)]; }
} // namespace humrig
