#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "Biquad.h"
#include "PitchDetector.h"

namespace humrig
{
// "Listening": gathers statistics from incoming (raw, pre-gain) mono audio and derives suggested settings:
//   key + major/minor (Krumhansl-Schmuckler), Male/Female + Range, input gain + compression amount,
//   Warmth/Presence/Air and De-Esser amount.
// The spectral / dynamics mappings are heuristics (see constants) meant as a good starting point, not a measurement.
class ListeningAnalyzer
{
public:
    struct Result
    {
        bool  valid = false;
        int   key = 0;               // 0 = C ... 11 = B
        bool  minor = false;
        float keyConfidence = 0.f;   // correlation of the winning profile (0..1)
        float medianF0 = 0.f;
        bool  female = false;
        int   range = 0;             // 0 Auto, 1 Soprano, 2 Alto/Tenor, 3 Bass
        float inGainDb = 0.f;
        float comp = 40.f, warmth = 50.f, presence = 50.f, air = 50.f, deess = 40.f;
    };

    static constexpr double neededVoicedSeconds = 6.0;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        decim = std::max (1, (int) std::lround (sr / 20000.0));
        detector.prepare (sr / decim);
        hop = std::max (decim, (int) std::lround (sr * 0.006));
        hist.assign ((size_t) PitchDetector::windowSize, 0.f);
        linear.assign ((size_t) PitchDetector::windowSize, 0.f);
        low.makeBandPass (sr, 250.0, 0.8);
        mid.makeBandPass (sr, 3000.0, 0.8);
        sib.makeBandPass (sr, 7000.0, 1.5);
        air.makeHighPass (sr, 9000.0, 0.707);
        f0s.reserve (4096); levelsDb.reserve (4096);
        reset();
    }

    void reset()
    {
        std::fill (hist.begin(), hist.end(), 0.f);
        histPos = 0; decimCount = 0; decimAcc = 0.f; untilHop = hop;
        low.reset(); mid.reset(); sib.reset(); air.reset();
        chroma.fill (0.0);
        f0s.clear(); levelsDb.clear();
        voicedSeconds = totalSeconds = 0.0;
        eLow = eMid = eSib = eAir = eTot = 0.0;
        hopEnergy = hopLow = hopMid = hopSib = hopAir = 0.0;
        finished = false;
    }

    void process (const float* x, int n)
    {
        if (finished) return;
        int done = 0;
        while (done < n)
        {
            const int chunk = std::min (n - done, untilHop);
            for (int i = 0; i < chunk; ++i)
            {
                const float v = x[done + i];
                hopEnergy += (double) v * v;
                const float l = low.process (v), m = mid.process (v), s = sib.process (v), a = air.process (v);
                hopLow += (double) l * l; hopMid += (double) m * m; hopSib += (double) s * s; hopAir += (double) a * a;
                decimAcc += v;
                if (++decimCount >= decim)
                {
                    hist[(size_t) histPos] = decimAcc / (float) decim;
                    histPos = (histPos + 1) % PitchDetector::windowSize;
                    decimAcc = 0.f; decimCount = 0;
                }
            }
            done += chunk;
            untilHop -= chunk;
            if (untilHop <= 0) { endOfHop(); untilHop = hop; if (finished) return; }
        }
    }

    float progress() const noexcept
    {
        return finished ? 1.f : (float) std::min (1.0, voicedSeconds / neededVoicedSeconds);
    }
    bool isDone() const noexcept { return finished; }

    Result result() const
    {
        Result r;
        if (voicedSeconds < 1.0) return r;
        r.valid = true;

        // ---- key (Krumhansl-Schmuckler) ----
        static constexpr double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static constexpr double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        double best = -2.0;
        for (int k = 0; k < 12; ++k)
            for (int m = 0; m < 2; ++m)
            {
                const double c = correlation (k, m ? mnr : maj);
                if (c > best) { best = c; r.key = k; r.minor = m == 1; }
            }
        r.keyConfidence = (float) std::clamp (best, 0.0, 1.0);

        // ---- voice type / range ----
        std::vector<float> f = f0s;
        std::nth_element (f.begin(), f.begin() + (long) f.size() / 2, f.end());
        r.medianF0 = f[f.size() / 2];
        r.female = r.medianF0 >= 165.f;
        r.range = r.medianF0 < 100.f ? 3 : (r.medianF0 >= 300.f ? 1 : 2);

        // ---- level / dynamics ----
        std::vector<float> lv = levelsDb;
        std::sort (lv.begin(), lv.end());
        const float med = lv[lv.size() / 2];
        const float p10 = lv[lv.size() / 10], p90 = lv[std::min (lv.size() - 1, lv.size() * 9 / 10)];
        r.inGainDb = std::round (std::clamp (-18.f - med, -12.f, 18.f));
        const float spread = p90 - p10;
        r.comp = std::round (std::clamp (20.f + (spread - 6.f) * 4.5f, 20.f, 80.f));

        // ---- spectral balance (heuristic references, dB relative to total energy) ----
        auto share = [this] (double e) { return 10.0 * std::log10 (std::max (e, 1e-20) / std::max (eTot, 1e-20)); };
        const double L = share (eLow), M = share (eMid), A = share (eAir);
        constexpr double refL = -5.0, refM = -14.0, refA = -32.0;
        r.warmth   = (float) std::round (50.0 + std::clamp ((refL - L) * 2.0, -20.0, 20.0));
        r.presence = (float) std::round (50.0 + std::clamp ((refM - M) * 1.5, -20.0, 20.0));
        r.air      = (float) std::round (50.0 + std::clamp ((refA - A) * 1.0, -20.0, 20.0));
        const double S = std::max (share (eSib), -60.0);          // 7 kHz band energy re total (typical sung vocal: about -40..-30 dB)
        r.deess = (float) std::round (std::clamp (30.0 + (S + 40.0) * 2.0, 20.0, 85.0));
        return r;
    }

private:
    double correlation (int key, const double* profile) const
    {
        double mx = 0, mp = 0;
        for (int i = 0; i < 12; ++i) { mx += chroma[(size_t) i]; mp += profile[i]; }
        mx /= 12.0; mp /= 12.0;
        double num = 0, dx = 0, dp = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = chroma[(size_t) ((key + i) % 12)] - mx, b = profile[i] - mp;
            num += a * b; dx += a * a; dp += b * b;
        }
        return (dx > 0 && dp > 0) ? num / std::sqrt (dx * dp) : 0.0;
    }

    void endOfHop()
    {
        const double hopSec = (double) hop / sr;
        totalSeconds += hopSec;
        const double rms = std::sqrt (hopEnergy / (double) hop);
        const bool loud = rms > 0.001;                         // > about -60 dBFS (quiet phone recordings still count)
        if (loud)
        {
            eTot += hopEnergy; eLow += hopLow; eMid += hopMid; eSib += hopSib; eAir += hopAir;

            for (int i = 0; i < PitchDetector::windowSize; ++i)
                linear[(size_t) i] = hist[(size_t) ((histPos + i) % PitchDetector::windowSize)];
            const auto r = detector.detect (linear.data(), 60.f, 1200.f);
            if (r.clarity > 0.85f)
            {
                const double midi = 69.0 + 12.0 * std::log2 ((double) r.freqHz / 440.0);
                int pc = (int) std::lround (midi) % 12; if (pc < 0) pc += 12;
                chroma[(size_t) pc] += (double) r.clarity * hopSec;
                voicedSeconds += hopSec;
                if (f0s.size() < f0s.capacity()) f0s.push_back (r.freqHz);
                if (levelsDb.size() < levelsDb.capacity()) levelsDb.push_back ((float) (20.0 * std::log10 (std::max (rms, 1e-9))));
            }
        }
        hopEnergy = hopLow = hopMid = hopSib = hopAir = 0.0;
        if (voicedSeconds >= neededVoicedSeconds || (totalSeconds >= 45.0 && voicedSeconds >= 2.0) || f0s.size() >= f0s.capacity())
            finished = true;
    }

    double sr = 44100.0;
    int decim = 2, hop = 256, untilHop = 256, histPos = 0, decimCount = 0;
    float decimAcc = 0.f;
    PitchDetector detector;
    std::vector<float> hist, linear, f0s, levelsDb;
    Biquad low, mid, sib, air;
    std::array<double, 12> chroma {};
    double voicedSeconds = 0, totalSeconds = 0;
    double eLow = 0, eMid = 0, eSib = 0, eAir = 0, eTot = 0;
    double hopEnergy = 0, hopLow = 0, hopMid = 0, hopSib = 0, hopAir = 0;
    bool finished = false;
};
} // namespace humrig
