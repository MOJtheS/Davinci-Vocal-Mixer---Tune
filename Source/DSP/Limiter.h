#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace humrig
{
// Stereo-linked look-ahead brick-wall limiter with inter-sample-peak (4x interpolated) detection.
//
// The audio is always delayed by `lookahead()` samples, whether the limiter is on or off, so the latency reported to the host never changes.
// Gain computation: required gain per input sample -> sliding-window minimum over the look-ahead -> release smoothing (instant attack)
// -> box-car average over the look-ahead. The box-car never lets the gain exceed the requirement of any sample still in the window,
// so the output cannot exceed the ceiling (up to the accuracy of the inter-sample estimate).
class Limiter
{
public:
    static constexpr double kLookaheadSeconds = 0.002;
    static int lookaheadFor (double sr) { return std::max (8, (int) std::lround (kLookaheadSeconds * sr)); }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        D = lookaheadFor (sr);
        ringL.assign ((size_t) D, 0.f);          // exactly D samples of delay
        ringR.assign ((size_t) D, 0.f);
        qIdx.assign ((size_t) (D + 2), 0);
        qVal.assign ((size_t) (D + 2), 1.f);
        boxBuf.assign ((size_t) (D + 1), 1.f);
        reset();
        setParams (false, -1.f, 100.f, 0.f);
    }

    void reset()
    {
        std::fill (ringL.begin(), ringL.end(), 0.f);
        std::fill (ringR.begin(), ringR.end(), 0.f);
        std::fill (boxBuf.begin(), boxBuf.end(), 1.f);
        boxSum = (double) boxBuf.size();
        qHead = qTail = qCount = 0;
        t = 0; pos = 0; rpos = 0; relState = 1.f; gainDb = 0.f;
        for (int i = 0; i < 4; ++i) hL[i] = hR[i] = 0.f;
    }

    int lookahead() const { return D; }

    void setParams (bool on, float ceilingDb, float releaseMs, float inGainDb)
    {
        enabled = on;
        ceilLin = std::pow (10.f, std::clamp (ceilingDb, -12.f, 0.f) * 0.05f);
        relCoef = 1.f - std::exp (-1.f / (float) (std::max (1.0f, releaseMs) * 0.001 * sr));
        inGain = std::pow (10.f, std::clamp (inGainDb, 0.f, 24.f) * 0.05f);
    }

    // one stereo sample in -> one (delayed, limited) stereo sample out
    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        const float g0 = enabled ? inGain : 1.f;
        const float xl = inL * g0, xr = inR * g0;

        push (hL, xl); push (hR, xr);
        float peak = std::max (std::fabs (xl), std::fabs (xr));
        if (enabled) peak = std::max ({ peak, ispPeak (hL), ispPeak (hR) });

        float req = 1.f;
        if (enabled && peak > ceilLin) req = ceilLin / peak;

        // sliding-window minimum (monotonic queue) over the last D+1 requirements
        while (qCount > 0 && qVal[(size_t) wrap (qTail - 1)] >= req) { qTail = wrap (qTail - 1); --qCount; }
        qIdx[(size_t) qTail] = t; qVal[(size_t) qTail] = req; qTail = wrap (qTail + 1); ++qCount;
        while (qCount > 0 && qIdx[(size_t) qHead] <= t - (D + 1)) { qHead = wrap (qHead + 1); --qCount; }
        const float m = qVal[(size_t) qHead];

        // release smoothing (instant attack)
        relState = (m < relState) ? m : relState + relCoef * (m - relState);

        // box-car average over D+1 samples
        boxSum += (double) relState - (double) boxBuf[(size_t) pos];
        boxBuf[(size_t) pos] = relState;
        if (++pos >= D + 1) pos = 0;
        const float g = (float) (boxSum / (double) boxBuf.size());

        // delayed audio (exactly D samples)
        const float dl = ringL[(size_t) rpos], dr = ringR[(size_t) rpos];
        ringL[(size_t) rpos] = xl;
        ringR[(size_t) rpos] = xr;
        if (++rpos >= D) rpos = 0;
        ++t;

        outL = dl * g; outR = dr * g;
        if (enabled)
        {
            outL = std::clamp (outL, -ceilLin, ceilLin);
            outR = std::clamp (outR, -ceilLin, ceilLin);
        }
        gainDb = 20.f * std::log10 (std::max (g, 1.0e-6f));
    }

    float grDb() const { return gainDb; }     // <= 0

private:
    int wrap (int i) const { const int n = D + 2; return i < 0 ? i + n : (i >= n ? i - n : i); }
    static void push (float* h, float x) { h[0] = h[1]; h[1] = h[2]; h[2] = h[3]; h[3] = x; }
    // largest |value| between the middle two of the last four samples (Catmull-Rom at 1/4, 1/2, 3/4)
    static float ispPeak (const float* h) noexcept
    {
        float best = 0.f;
        for (float f : { 0.25f, 0.5f, 0.75f })
        {
            const float f2 = f * f, f3 = f2 * f;
            const float v = 0.5f * ((2.f * h[1]) + (-h[0] + h[2]) * f + (2.f * h[0] - 5.f * h[1] + 4.f * h[2] - h[3]) * f2
                                    + (-h[0] + 3.f * h[1] - 3.f * h[2] + h[3]) * f3);
            best = std::max (best, std::fabs (v));
        }
        return best;
    }

    double sr = 44100.0;
    int D = 96, qHead = 0, qTail = 0, qCount = 0, pos = 0, rpos = 0;
    long long t = 0;
    std::vector<float> ringL, ringR, qVal, boxBuf;
    std::vector<long long> qIdx;
    double boxSum = 0.0;
    bool enabled = false;
    float ceilLin = 0.89f, relCoef = 0.001f, inGain = 1.f, relState = 1.f, gainDb = 0.f;
    float hL[4] = { 0, 0, 0, 0 }, hR[4] = { 0, 0, 0, 0 };
};
} // namespace humrig
