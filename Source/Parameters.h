#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace humrig
{
namespace ID
{
    inline constexpr const char* tuneOn   = "tuneOn";
    inline constexpr const char* key      = "key";
    inline constexpr const char* scale    = "scale";
    inline constexpr const char* speed    = "speed";
    inline constexpr const char* vibrato  = "vibrato";
    inline constexpr const char* flex     = "flex";
    inline constexpr const char* range    = "range";
    inline constexpr const char* voice    = "voice";
    inline constexpr const char* warmth   = "warmth";
    inline constexpr const char* presence = "presence";
    inline constexpr const char* air      = "air";
    inline constexpr const char* comp     = "comp";
    inline constexpr const char* deess    = "deess";
    inline constexpr const char* drive    = "drive";
    inline constexpr const char* revMix   = "revMix";
    inline constexpr const char* revSize  = "revSize";
    inline constexpr const char* revDecay = "revDecay";
    inline constexpr const char* dlyMix   = "dlyMix";
    inline constexpr const char* dlyFb    = "dlyFb";
    inline constexpr const char* dlyTime  = "dlyTime";
    inline constexpr const char* dlyPing  = "dlyPing";
    inline constexpr const char* inGain   = "inGain";
    inline constexpr const char* outGain  = "outGain";
    inline constexpr const char* mix      = "mix";
}

constexpr float kMixOffDb = -60.f;   // reverb/delay "Mix" at its minimum means off

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

struct Preset
{
    const char* genre;   // menu section: General / Pop / Rap / Rock / Classic Rock / Metal
    const char* name;
    float warmth, presence, air, comp, deess, drive;
    float revMix, revSize, revDecay;
    float dlyMix, dlyFb; int dlyTime; bool dlyPing;
    float speed, vibrato, flex;
};

int getNumPresets();
const Preset& getPreset (int index);
} // namespace humrig
