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
    // ---- v0.3 additions (append-only: AU hosts may restore automation by position) ----
    // master / limiter
    inline constexpr const char* limOn      = "limOn";
    inline constexpr const char* limCeil    = "limCeil";
    inline constexpr const char* limRelease = "limRelease";
    inline constexpr const char* limGain    = "limGain";
    // eq (manual = use these frequencies instead of the voice-dependent v0.2 ones)
    inline constexpr const char* eqManual   = "eqManual";
    inline constexpr const char* eqHpfFreq  = "eqHpfFreq";
    inline constexpr const char* eqHpfSlope = "eqHpfSlope";
    inline constexpr const char* eqLowFreq  = "eqLowFreq";
    inline constexpr const char* eqMudFreq  = "eqMudFreq";
    inline constexpr const char* eqMudGain  = "eqMudGain";
    inline constexpr const char* eqMudQ     = "eqMudQ";
    inline constexpr const char* eqBoxFreq  = "eqBoxFreq";
    inline constexpr const char* eqBoxGain  = "eqBoxGain";
    inline constexpr const char* eqBoxQ     = "eqBoxQ";
    inline constexpr const char* eqPresFreq = "eqPresFreq";
    inline constexpr const char* eqPresQ    = "eqPresQ";
    inline constexpr const char* eqAirFreq  = "eqAirFreq";
    inline constexpr const char* eqLpfOn    = "eqLpfOn";
    inline constexpr const char* eqLpfFreq  = "eqLpfFreq";
    // compressor
    inline constexpr const char* compRatio   = "compRatio";
    inline constexpr const char* compAttack  = "compAttack";
    inline constexpr const char* compRelease = "compRelease";
    inline constexpr const char* compKnee    = "compKnee";
    inline constexpr const char* compMix     = "compMix";
    inline constexpr const char* compDet     = "compDet";
    inline constexpr const char* compSc      = "compSc";
    inline constexpr const char* compThr     = "compThr";
    inline constexpr const char* compMk      = "compMk";
    // de-esser
    inline constexpr const char* dsFreq     = "dsFreq";
    inline constexpr const char* dsMode     = "dsMode";
    inline constexpr const char* dsRange    = "dsRange";
    inline constexpr const char* dsThr      = "dsThr";
    inline constexpr const char* dsListen   = "dsListen";
    // colour
    inline constexpr const char* colType    = "colType";
    inline constexpr const char* colTone    = "colTone";
    inline constexpr const char* colMix     = "colMix";
    inline constexpr const char* excFreq    = "excFreq";
    inline constexpr const char* excAmt     = "excAmt";
    // space
    inline constexpr const char* revPre     = "revPre";
    inline constexpr const char* revFilt    = "revFilt";
    inline constexpr const char* revLo      = "revLo";
    inline constexpr const char* revHi      = "revHi";
    inline constexpr const char* duck       = "duck";
    inline constexpr const char* dlyTimeEx  = "dlyTimeEx";
    inline constexpr const char* dlyFree    = "dlyFree";
    inline constexpr const char* dlyFilt    = "dlyFilt";
    inline constexpr const char* dlyHpf     = "dlyHpf";
    inline constexpr const char* dlyLpf     = "dlyLpf";
    // stereo
    inline constexpr const char* dblMix     = "dblMix";
    inline constexpr const char* dblDelay   = "dblDelay";
    inline constexpr const char* dblDetune  = "dblDetune";
    inline constexpr const char* dblHpf     = "dblHpf";
    inline constexpr const char* width      = "width";
    inline constexpr const char* monoCheck  = "monoCheck";
    // clean (expander)
    inline constexpr const char* expOn      = "expOn";
    inline constexpr const char* expThr     = "expThr";
    inline constexpr const char* expRange   = "expRange";
    inline constexpr const char* expHold    = "expHold";
    inline constexpr const char* expRel     = "expRel";
    // leveler (rider) + peak catcher
    inline constexpr const char* ridOn      = "ridOn";
    inline constexpr const char* ridTarget  = "ridTarget";
    inline constexpr const char* ridRange   = "ridRange";
    inline constexpr const char* ridSpeed   = "ridSpeed";
    inline constexpr const char* ridPause   = "ridPause";
    inline constexpr const char* pkAmt      = "pkAmt";
    inline constexpr const char* dnAmt      = "dnAmt";
}

constexpr float kMixOffDb = -60.f;

// choice lists shared by the parameter layout, the processor and the UI
inline const char* const kDelayTimeExNames[] = { "As selected", "1/2", "1/4", "1/4 dotted", "1/4 triplet", "1/8", "1/8 dotted", "1/8 triplet",
                                                  "1/16", "1/16 dotted", "1/16 triplet", "Free (ms)" };
// length in quarter-note beats of entries 1..10 of kDelayTimeExNames
inline constexpr double kDelayTimeExBeats[] = { 0.0, 2.0, 1.0, 1.5, 2.0 / 3.0, 0.5, 0.75, 1.0 / 3.0, 0.25, 0.375, 1.0 / 6.0 };   // reverb/delay "Mix" at its minimum means off

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// A value for one of the detailed (v0.3) controls. Presets list only what they change; every other detailed control is reset to its default.
struct Extra { const char* id; float value; };

struct Preset
{
    const char* genre;   // menu section: General / Pop / Rap / Rock / Classic Rock / Metal / Pro Chains
    const char* name;
    float warmth, presence, air, comp, deess, drive;
    float revMix, revSize, revDecay;
    float dlyMix, dlyFb; int dlyTime; bool dlyPing;
    float speed, vibrato, flex;
    const Extra* extras = nullptr;   // detailed controls (EQ bands, compressor timing, limiter ...)
    int numExtras = 0;
};

// ids of all detailed controls added in v0.3 (reset to default whenever a preset is applied)
const char* const* detailedParamIds (int& count);

int getNumPresets();
const Preset& getPreset (int index);
} // namespace humrig
