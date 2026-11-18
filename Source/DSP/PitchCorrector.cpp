#include "PitchCorrector.h"
#include <algorithm>

namespace humrig
{
void PitchCorrector::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    stretch.presetDefault (1, (float) sr);
    latency = stretch.inputLatency() + stretch.outputLatency();

    decim = std::max (1, (int) std::lround (sr / 20000.0));
    detector.prepare (sr / decim);
    hop = std::max (decim, (int) std::lround (sr * 0.006));
    hopSeconds = (double) hop / sr;

    hist.assign ((size_t) histSize, 0.f);
    linear.assign ((size_t) histSize, 0.f);
    dryRing.assign ((size_t) std::max (1, latency), 0.f);   // read-then-write ring => delay == ring size
    scratch.assign ((size_t) std::max (1, maxBlockSize) + 8, 0.f);
    reset();
}

void PitchCorrector::reset()
{
    stretch.reset();
    std::fill (hist.begin(), hist.end(), 0.f);
    std::fill (dryRing.begin(), dryRing.end(), 0.f);
    histPos = 0; dryPos = 0; decimCount = 0; decimAcc = 0.f;
    untilHop = hop;
    shift = targetShift = 0.f;
    heldNote = -1; unvoicedHops = 0; medCount = 0; lastAccepted = -1.f; pendingJumpHops = 0;
    vibPhase = 0.f; vibDepth = 0.f;
    blend = 1.f;
    detectedMidi.store (-1.f);
    targetMidi.store (-1.f);
}

void PitchCorrector::runDetection (const TuneSettings& s)
{
    // linearise ring (oldest first)
    for (int i = 0; i < histSize; ++i)
        linear[(size_t) i] = hist[(size_t) ((histPos + i) % histSize)];

    const auto r = detector.detect (linear.data(), s.minHz, s.maxHz);
    const bool voiced = r.clarity > 0.80f && r.freqHz >= s.minHz * 0.95f && r.freqHz <= s.maxHz * 1.05f;

    if (! voiced)
    {
        if (++unvoicedHops > 8) { heldNote = -1; medCount = 0; lastAccepted = -1.f; }
        targetShift = 0.f;
        detectedMidi.store (-1.f);
        targetMidi.store (-1.f);
        return;
    }

    float midi = 69.f + 12.f * std::log2 (r.freqHz / 440.f);

    // reject isolated octave jumps: a large leap must persist for 3 hops
    if (lastAccepted > 0.f && std::fabs (midi - lastAccepted) > 6.f)
    {
        if (++pendingJumpHops < 3) { return; }
        medCount = 0;
    }
    pendingJumpHops = 0;
    unvoicedHops = 0;
    lastAccepted = midi;

    // 3-point median to remove single-frame glitches
    medBuf[(size_t) (medCount % 3)] = midi;
    ++medCount;
    if (medCount >= 3)
    {
        auto b = medBuf;
        std::sort (b.begin(), b.end());
        midi = b[1];
    }

    const uint16_t mask = scaleMask (s.scale);
    const int cand = nearestInScale (midi, s.key, mask);
    if (heldNote < 0) heldNote = cand;
    else if (cand != heldNote && std::fabs (midi - (float) heldNote) > std::fabs (midi - (float) cand) + 0.15f)
        heldNote = cand;

    float err = (float) heldNote - midi;                    // semitones we need to move
    const float tol = 0.40f * (s.flex / 100.f);              // natural-movement dead zone
    if (std::fabs (err) <= tol) err = 0.f;
    else err -= std::copysign (tol, err);

    targetShift = std::clamp (err, -6.f, 6.f);
    detectedMidi.store (midi);
    targetMidi.store ((float) heldNote);

    stretch.setFormantBase (r.freqHz);
}

void PitchCorrector::process (const float* in, float* out, int n, const TuneSettings& s)
{
    if ((int) scratch.size() < n + 8) scratch.resize ((size_t) n + 8);
    const float blendTarget = s.enabled ? 1.f : 0.f;
    const float blendStep = (float) (1.0 / (0.03 * sr));     // 30 ms crossfade

    // speed -> time constant (ms): 100 => ~1.5 ms, 0 => ~400 ms (exponential mapping)
    const float tauMs = 400.f * std::pow (1.5f / 400.f, std::clamp (s.speed, 0.f, 100.f) / 100.f);
    const float alphaTune = 1.f - std::exp (-(float) hopSeconds * 1000.f / tauMs);
    const float alphaRelease = 1.f - std::exp (-(float) hopSeconds / 0.08f);

    int done = 0;
    while (done < n)
    {
        const int chunk = std::min (n - done, untilHop);
        const float* src = in + done;

        // keep a decimated history for the detector
        for (int i = 0; i < chunk; ++i)
        {
            decimAcc += src[i];
            if (++decimCount >= decim)
            {
                hist[(size_t) histPos] = decimAcc / (float) decim;
                histPos = (histPos + 1) % histSize;
                decimAcc = 0.f; decimCount = 0;
            }
        }

        // delayed dry (identical latency to the stretcher path)
        float* dryOut = scratch.data() + done;
        const int ringN = (int) dryRing.size();
        for (int i = 0; i < chunk; ++i)
        {
            dryOut[i] = dryRing[(size_t) dryPos];
            dryRing[(size_t) dryPos] = src[i];
            dryPos = (dryPos + 1) % ringN;
        }

        // vibrato LFO (cents -> semitones), only while singing
        const float vibTarget = (s.enabled && heldNote >= 0) ? 0.45f * (s.vibrato / 100.f) : 0.f;
        vibDepth += (vibTarget - vibDepth) * 0.02f;
        const float lfo = vibDepth * std::sin (vibPhase);
        vibPhase += (float) (2.0 * 3.14159265358979 * 5.5 * (double) chunk / sr);
        if (vibPhase > 6.2831853f) vibPhase -= 6.2831853f;

        const bool tuning = s.enabled && heldNote >= 0;
        const float a = tuning ? alphaTune : alphaRelease;
        shift += (targetShift - shift) * a;

        stretch.setTransposeSemitones (s.enabled ? shift + lfo : 0.f);

        const float* ins[1] = { src };
        float* outs[1] = { out + done };
        stretch.process (ins, chunk, outs, chunk);   // in and out must not alias

        for (int i = 0; i < chunk; ++i)
        {
            if (blend < blendTarget) blend = std::min (blendTarget, blend + blendStep);
            else if (blend > blendTarget) blend = std::max (blendTarget, blend - blendStep);
            outs[0][i] = outs[0][i] * blend + dryOut[i] * (1.f - blend);
        }

        done += chunk;
        untilHop -= chunk;
        if (untilHop <= 0) { runDetection (s); untilHop = hop; }
    }
}
} // namespace humrig
