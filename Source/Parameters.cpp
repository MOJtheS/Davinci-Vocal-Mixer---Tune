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

    // ================= v0.3 additions (appended; version 2) =================
    constexpr int v2 = 2;
    const auto logRange = [] (float lo, float hi, float skew = 0.4f) { return juce::NormalisableRange<float> (lo, hi, 0.1f, skew); };
    const auto dbLab = juce::AudioParameterFloatAttributes().withLabel ("dB");
    const auto hzLab = juce::AudioParameterFloatAttributes().withLabel ("Hz");
    const auto msLab = juce::AudioParameterFloatAttributes().withLabel ("ms");
    const auto autoIfLow = [] (float lim, const char* text, const char* unit)
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction ([=] (float x, int) { return x <= lim ? juce::String (text) : juce::String (x, 1) + " " + unit; });
    };

    // master / limiter
    l.add (std::make_unique<B> (PID (ID::limOn, v2), "Limiter", false));
    l.add (std::make_unique<P> (PID (ID::limCeil, v2), "Limiter Ceiling", fullRange (-6, 0), -1.f, dbLab));
    l.add (std::make_unique<P> (PID (ID::limRelease, v2), "Limiter Release", logRange (20, 500, 0.5f), 100.f, msLab));
    l.add (std::make_unique<P> (PID (ID::limGain, v2), "Limiter Gain", fullRange (0, 12), 0.f, dbLab));
    // eq
    l.add (std::make_unique<B> (PID (ID::eqManual, v2), "EQ Manual Frequencies", false));
    l.add (std::make_unique<P> (PID (ID::eqHpfFreq, v2), "HPF Frequency", logRange (40, 200), 110.f, hzLab));
    l.add (std::make_unique<C> (PID (ID::eqHpfSlope, v2), "HPF Slope", juce::StringArray { "12 dB", "24 dB" }, 0));
    l.add (std::make_unique<P> (PID (ID::eqLowFreq, v2), "Low Shelf Frequency", logRange (80, 400), 240.f, hzLab));
    l.add (std::make_unique<P> (PID (ID::eqMudFreq, v2), "Mud Frequency", logRange (150, 500), 300.f, hzLab));
    l.add (std::make_unique<P> (PID (ID::eqMudGain, v2), "Mud Gain", fullRange (-9, 3), 0.f, dbLab));
    l.add (std::make_unique<P> (PID (ID::eqMudQ, v2), "Mud Q", fullRange (0.4f, 4), 1.0f));
    l.add (std::make_unique<P> (PID (ID::eqBoxFreq, v2), "Box Frequency", logRange (500, 1200), 800.f, hzLab));
    l.add (std::make_unique<P> (PID (ID::eqBoxGain, v2), "Box Gain", fullRange (-9, 3), 0.f, dbLab));
    l.add (std::make_unique<P> (PID (ID::eqBoxQ, v2), "Box Q", fullRange (0.4f, 4), 1.5f));
    l.add (std::make_unique<P> (PID (ID::eqPresFreq, v2), "Presence Frequency", logRange (2000, 6000), 3800.f, hzLab));
    l.add (std::make_unique<P> (PID (ID::eqPresQ, v2), "Presence Q", fullRange (0.4f, 3), 0.9f));
    l.add (std::make_unique<P> (PID (ID::eqAirFreq, v2), "Air Frequency", logRange (6000, 16000), 11000.f, hzLab));
    l.add (std::make_unique<B> (PID (ID::eqLpfOn, v2), "Low-pass On", false));
    l.add (std::make_unique<P> (PID (ID::eqLpfFreq, v2), "Low-pass Frequency", logRange (8000, 20000), 16000.f, hzLab));
    // compressor
    l.add (std::make_unique<P> (PID (ID::compRatio, v2), "Comp Ratio", fullRange (1, 10), 1.f,
                                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return x <= 1.05f ? juce::String ("Auto") : juce::String (x, 1) + ":1"; })));
    l.add (std::make_unique<P> (PID (ID::compAttack, v2), "Comp Attack", logRange (0.5f, 50, 0.5f), 8.f, msLab));
    l.add (std::make_unique<P> (PID (ID::compRelease, v2), "Comp Release", logRange (30, 500, 0.5f), 120.f, msLab));
    l.add (std::make_unique<P> (PID (ID::compKnee, v2), "Comp Knee", fullRange (0, 12), 6.f, dbLab));
    l.add (std::make_unique<P> (PID (ID::compMix, v2), "Comp Mix", fullRange (0, 100), 100.f, pctAttr));
    l.add (std::make_unique<C> (PID (ID::compDet, v2), "Comp Detector", juce::StringArray { "Peak", "RMS" }, 0));
    l.add (std::make_unique<P> (PID (ID::compSc, v2), "Comp Sidechain HPF", logRange (20, 500, 0.5f), 20.f, autoIfLow (20.5f, "Off", "Hz")));
    l.add (std::make_unique<P> (PID (ID::compThr, v2), "Comp Threshold Trim", fullRange (-12, 12), 0.f, dbLab));
    l.add (std::make_unique<P> (PID (ID::compMk, v2), "Comp Make-up Trim", fullRange (-12, 12), 0.f, dbLab));
    // de-esser
    l.add (std::make_unique<P> (PID (ID::dsFreq, v2), "De-Ess Frequency", logRange (4000, 12000), 5500.f, hzLab));
    l.add (std::make_unique<C> (PID (ID::dsMode, v2), "De-Ess Mode", juce::StringArray { "Band", "Split (v0.2)", "Wide" }, 0));
    l.add (std::make_unique<P> (PID (ID::dsRange, v2), "De-Ess Range", fullRange (0, 15), 0.f, autoIfLow (0.05f, "Auto", "dB")));
    l.add (std::make_unique<P> (PID (ID::dsThr, v2), "De-Ess Threshold Trim", fullRange (-12, 12), 0.f, dbLab));
    l.add (std::make_unique<B> (PID (ID::dsListen, v2), "De-Ess Listen", false));
    // colour
    l.add (std::make_unique<C> (PID (ID::colType, v2), "Saturation Type", juce::StringArray { "Soft", "Tube", "Tape" }, 0));
    l.add (std::make_unique<P> (PID (ID::colTone, v2), "Saturation Tone", fullRange (-100, 100), 0.f, pctAttr));
    l.add (std::make_unique<P> (PID (ID::colMix, v2), "Saturation Mix", fullRange (0, 100), 100.f, pctAttr));
    l.add (std::make_unique<P> (PID (ID::excFreq, v2), "Exciter Frequency", logRange (3000, 8000), 5000.f, hzLab));
    l.add (std::make_unique<P> (PID (ID::excAmt, v2), "Exciter Amount", fullRange (0, 100), 0.f, pctAttr));
    // space
    l.add (std::make_unique<P> (PID (ID::revPre, v2), "Reverb Pre-delay", fullRange (0, 120), 0.f, msLab));
    l.add (std::make_unique<B> (PID (ID::revFilt, v2), "Reverb Filters On", false));
    l.add (std::make_unique<P> (PID (ID::revLo, v2), "Reverb Low Cut", logRange (60, 600), 200.f, hzLab));
    l.add (std::make_unique<P> (PID (ID::revHi, v2), "Reverb High Cut", logRange (3000, 20000), 9000.f, hzLab));
    l.add (std::make_unique<P> (PID (ID::duck, v2), "Ducking", fullRange (0, 100), 0.f, pctAttr));
    {
        juce::StringArray ex; for (auto* n : kDelayTimeExNames) ex.add (n);
        l.add (std::make_unique<C> (PID (ID::dlyTimeEx, v2), "Delay Note", ex, 0));
    }
    l.add (std::make_unique<P> (PID (ID::dlyFree, v2), "Delay Free Time", logRange (20, 1000, 0.5f), 350.f, msLab));
    l.add (std::make_unique<B> (PID (ID::dlyFilt, v2), "Delay Feedback Filters On", false));
    l.add (std::make_unique<P> (PID (ID::dlyHpf, v2), "Delay High-pass", logRange (60, 600), 200.f, hzLab));
    l.add (std::make_unique<P> (PID (ID::dlyLpf, v2), "Delay Low-pass", logRange (1500, 12000), 5000.f, hzLab));
    // stereo
    l.add (std::make_unique<P> (PID (ID::dblMix, v2), "Double Mix", fullRange (0, 50), 0.f, pctAttr));
    l.add (std::make_unique<P> (PID (ID::dblDelay, v2), "Double Delay", fullRange (8, 30), 15.f, msLab));
    l.add (std::make_unique<P> (PID (ID::dblDetune, v2), "Double Detune", fullRange (0, 15), 8.f, juce::AudioParameterFloatAttributes().withLabel ("ct")));
    l.add (std::make_unique<P> (PID (ID::dblHpf, v2), "Double High-pass", logRange (100, 400), 200.f, hzLab));
    l.add (std::make_unique<P> (PID (ID::width, v2), "Width", fullRange (0, 150), 100.f, pctAttr));
    l.add (std::make_unique<B> (PID (ID::monoCheck, v2), "Mono Check", false));
    // clean
    l.add (std::make_unique<B> (PID (ID::expOn, v2), "Expander On", false));
    l.add (std::make_unique<P> (PID (ID::expThr, v2), "Expander Threshold", fullRange (-70, -20), -50.f, dbLab));
    l.add (std::make_unique<P> (PID (ID::expRange, v2), "Expander Range", fullRange (0, 24), 12.f, dbLab));
    l.add (std::make_unique<P> (PID (ID::expHold, v2), "Expander Hold", fullRange (0, 300), 100.f, msLab));
    l.add (std::make_unique<P> (PID (ID::expRel, v2), "Expander Release", logRange (50, 600, 0.5f), 250.f, msLab));
    return l;
}



// ---------------------------------------------------------------------------------------------------------
// "Pro Chains": presets that use the detailed controls. Starting points from the post-production research report
// (expander ~10 dB, mud cut 2-3 dB around 300 Hz, serial gentle compression 3-6 dB, pre-delay 20-40 ms, ducked and filtered returns, -1 dB ceiling).
// They are starting points, not rules: adjust by ear.
static const Extra kPhoneRescue[] = {
    { ID::expOn, 1 }, { ID::expThr, -52 }, { ID::expRange, 10 }, { ID::expHold, 120 },
    { ID::eqManual, 1 }, { ID::eqHpfFreq, 100 }, { ID::eqHpfSlope, 1 }, { ID::eqMudFreq, 300 }, { ID::eqMudGain, -3 }, { ID::eqMudQ, 1.0f },
    { ID::eqBoxFreq, 800 }, { ID::eqBoxGain, -1.5f }, { ID::eqBoxQ, 1.5f },
    { ID::compRatio, 3.f }, { ID::compAttack, 10 }, { ID::compRelease, 110 }, { ID::compSc, 120 }, { ID::compKnee, 8 },
    { ID::dsFreq, 6500 }, { ID::revPre, 30 }, { ID::revFilt, 1 }, { ID::revLo, 200 }, { ID::revHi, 9000 }, { ID::duck, 40 },
    { ID::limOn, 1 }, { ID::limCeil, -1 } };
static const Extra kHumPolish[] = {
    { ID::expOn, 1 }, { ID::expThr, -55 }, { ID::expRange, 8 }, { ID::expHold, 150 },
    { ID::eqManual, 1 }, { ID::eqHpfFreq, 80 }, { ID::eqMudFreq, 250 }, { ID::eqMudGain, -2 }, { ID::eqMudQ, 0.9f },
    { ID::compRatio, 2.5f }, { ID::compAttack, 15 }, { ID::compRelease, 160 }, { ID::compDet, 1 },
    { ID::colType, 1 }, { ID::colMix, 35 }, { ID::excAmt, 12 }, { ID::excFreq, 6000 },
    { ID::revPre, 25 }, { ID::revFilt, 1 }, { ID::duck, 35 }, { ID::dblMix, 12 }, { ID::dblDelay, 14 }, { ID::width, 115 },
    { ID::limOn, 1 }, { ID::limCeil, -1 } };
static const Extra kBroadcast[] = {
    { ID::eqManual, 1 }, { ID::eqHpfFreq, 90 }, { ID::eqHpfSlope, 1 }, { ID::eqMudFreq, 280 }, { ID::eqMudGain, -2.5f },
    { ID::eqPresFreq, 3500 }, { ID::eqPresQ, 1.0f },
    { ID::compRatio, 4.f }, { ID::compAttack, 12 }, { ID::compRelease, 90 }, { ID::compKnee, 6 }, { ID::compMk, 1.5f },
    { ID::dsFreq, 6000 }, { ID::dsRange, 8 }, { ID::colType, 1 }, { ID::colMix, 40 },
    { ID::limOn, 1 }, { ID::limCeil, -1 }, { ID::limGain, 2.f } };
static const Extra kWidePop[] = {
    { ID::eqManual, 1 }, { ID::eqHpfFreq, 90 }, { ID::eqMudFreq, 300 }, { ID::eqMudGain, -2 }, { ID::eqAirFreq, 12000 },
    { ID::compRatio, 3.f }, { ID::compAttack, 8 }, { ID::compRelease, 100 }, { ID::compSc, 100 },
    { ID::dsFreq, 7000 }, { ID::excAmt, 15 }, { ID::excFreq, 6500 },
    { ID::dlyTimeEx, 6 }, { ID::dlyFilt, 1 }, { ID::dlyHpf, 250 }, { ID::dlyLpf, 5000 },
    { ID::revPre, 35 }, { ID::revFilt, 1 }, { ID::revLo, 220 }, { ID::revHi, 9500 }, { ID::duck, 55 },
    { ID::dblMix, 28 }, { ID::dblDelay, 16 }, { ID::dblDetune, 9 }, { ID::width, 130 },
    { ID::limOn, 1 }, { ID::limCeil, -1 } };
static const Extra kDarkIntimate[] = {
    { ID::expOn, 1 }, { ID::expThr, -54 }, { ID::expRange, 9 },
    { ID::eqManual, 1 }, { ID::eqHpfFreq, 85 }, { ID::eqLowFreq, 200 }, { ID::eqMudFreq, 320 }, { ID::eqMudGain, -1.5f },
    { ID::eqLpfOn, 1 }, { ID::eqLpfFreq, 14000 },
    { ID::compRatio, 2.f }, { ID::compAttack, 20 }, { ID::compRelease, 200 }, { ID::compDet, 1 }, { ID::compMix, 70 },
    { ID::colType, 2 }, { ID::colMix, 50 }, { ID::revPre, 20 }, { ID::revFilt, 1 }, { ID::revHi, 7000 }, { ID::duck, 30 },
    { ID::limOn, 1 }, { ID::limCeil, -1 } };

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

    // Pro Chains (use the detailed tabs)
    { "Pro Chains", "Phone Vocal Rescue",    50, 58, 55,  55, 45, 12,  -26.f, 0.45f, 1.6f,  kMixOffDb, 30, 1, false,  60,  0, 30, kPhoneRescue,  (int) (sizeof (kPhoneRescue)  / sizeof (Extra)) },
    { "Pro Chains", "Hum Polish",            52, 56, 62,  42, 30,  8,  -22.f, 0.55f, 2.0f,  -36.f, 30, 1, true,   55, 10, 30, kHumPolish,    (int) (sizeof (kHumPolish)    / sizeof (Extra)) },
    { "Pro Chains", "Broadcast Voice",       55, 60, 50,  65, 50, 10,  -40.f, 0.25f, 0.8f,  kMixOffDb, 30, 1, false,  70,  0, 20, kBroadcast,   (int) (sizeof (kBroadcast)   / sizeof (Extra)) },
    { "Pro Chains", "Wide Pop Lead",         45, 60, 70,  55, 50, 15,  -22.f, 0.60f, 1.9f,  -30.f, 30, 1, true,   80,  8, 15, kWidePop,      (int) (sizeof (kWidePop)      / sizeof (Extra)) },
    { "Pro Chains", "Dark Intimate",         60, 48, 40,  38, 30, 12,  -26.f, 0.50f, 1.8f,  kMixOffDb, 30, 1, false,  45, 12, 40, kDarkIntimate, (int) (sizeof (kDarkIntimate) / sizeof (Extra)) },
};

const char* const* detailedParamIds (int& count)
{
    static const char* const ids[] = {
        ID::limOn, ID::limCeil, ID::limRelease, ID::limGain, ID::eqManual, ID::eqHpfFreq, ID::eqHpfSlope, ID::eqLowFreq, ID::eqMudFreq, ID::eqMudGain,
        ID::eqMudQ, ID::eqBoxFreq, ID::eqBoxGain, ID::eqBoxQ, ID::eqPresFreq, ID::eqPresQ, ID::eqAirFreq, ID::eqLpfOn, ID::eqLpfFreq,
        ID::compRatio, ID::compAttack, ID::compRelease, ID::compKnee, ID::compMix, ID::compDet, ID::compSc, ID::compThr, ID::compMk,
        ID::dsFreq, ID::dsMode, ID::dsRange, ID::dsThr, ID::dsListen, ID::colType, ID::colTone, ID::colMix, ID::excFreq, ID::excAmt,
        ID::revPre, ID::revFilt, ID::revLo, ID::revHi, ID::duck, ID::dlyTimeEx, ID::dlyFree, ID::dlyFilt, ID::dlyHpf, ID::dlyLpf,
        ID::dblMix, ID::dblDelay, ID::dblDetune, ID::dblHpf, ID::width, ID::monoCheck, ID::expOn, ID::expThr, ID::expRange, ID::expHold, ID::expRel };
    count = (int) (sizeof (ids) / sizeof (ids[0]));
    return ids;
}

int getNumPresets() { return (int) (sizeof (kPresets) / sizeof (kPresets[0])); }
const Preset& getPreset (int i) { return kPresets[juce::jlimit (0, getNumPresets() - 1, i)]; }
} // namespace humrig
