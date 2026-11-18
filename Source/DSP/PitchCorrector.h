#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include <signalsmith-stretch/signalsmith-stretch.h>
#include "PitchDetector.h"
#include "Scale.h"

namespace humrig
{
struct TuneSettings
{
    bool  enabled = true;
    int   key = 0;            // 0..11
    int   scale = 1;          // see kScaleNames
    float speed = 60.f;       // 0..100  (100 = instant / hard-tune)
    float vibrato = 0.f;      // 0..100  added vibrato depth
    float flex = 0.f;         // 0..100  tolerance that preserves natural pitch movement
    float minHz = 65.f;       // detection range
    float maxHz = 800.f;
};

// Monophonic pitch corrector: YIN detection -> scale snapping -> Signalsmith Stretch transposition.
// Latency is constant (getLatencySamples) whether or not correction is enabled.
class PitchCorrector
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    int  getLatencySamples() const noexcept { return latency; }

    // in and out must NOT alias. Processes n mono samples.
    void process (const float* in, float* out, int n, const TuneSettings& s);

    // UI feedback (message-thread safe)
    std::atomic<float> detectedMidi { -1.f };   // -1 = unvoiced
    std::atomic<float> targetMidi   { -1.f };

private:
    void runDetection (const TuneSettings& s);

    using Stretch = signalsmith::stretch::SignalsmithStretch<float>;
    Stretch stretch { 20240601L };      // fixed seed => reproducible output

    double sr = 44100.0;
    int latency = 0;
    int decim = 2;
    int hop = 256;                      // detection hop in input samples
    int untilHop = 256;
    double hopSeconds = 0.006;

    // decimated history for detection
    std::vector<float> hist;            // ring, size histSize
    int histPos = 0;
    static constexpr int histSize = PitchDetector::windowSize;
    std::vector<float> linear;          // scratch (windowSize)
    int decimCount = 0;
    float decimAcc = 0.f;
    PitchDetector detector;

    // tracking state
    float shift = 0.f;                  // current smoothed transposition (semitones)
    float targetShift = 0.f;
    int   heldNote = -1;
    int   unvoicedHops = 0;
    std::array<float, 3> medBuf { { 0.f, 0.f, 0.f } };
    int   medCount = 0;
    float lastAccepted = -1.f;
    int   pendingJumpHops = 0;
    float vibPhase = 0.f;
    float vibDepth = 0.f;

    // dry delay + crossfade so the latency is identical in bypass
    std::vector<float> dryRing;
    int dryPos = 0;
    float blend = 1.f;                  // 1 = corrected, 0 = delayed dry
    std::vector<float> scratch;
};
} // namespace humrig
