#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include "Biquad.h"

namespace humrig
{
// ITU-R BS.1770 / EBU R128 loudness: K-weighting, momentary (400 ms), short-term (3 s), gated integrated, true-peak read-out.
// Runs on the audio thread (no allocation after prepare); the numbers are plain floats read by the UI through the shared bus.
class LoudnessMeter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        designK (shelf, hp);
        subLen = std::max (1, (int) std::lround (0.1 * sr));           // 100 ms sub-blocks
        resetAll();
    }

    void resetAll()
    {
        for (auto& f : shelf) f.reset();
        for (auto& f : hp) f.reset();
        subAcc = 0.0; subCount = 0; ringPos = 0; filled = 0;
        subRing.fill (0.0);
        hist.fill (0); histEnergy.fill (0.0);
        momentary = shortTerm = integrated = -100.f;
        truePeakDb = -100.f;
    }

    // call once per stereo sample (post everything)
    void push (float l, float r) noexcept
    {
        const float kl = hp[0].process (shelf[0].process (l));
        const float kr = hp[1].process (shelf[1].process (r));
        subAcc += (double) kl * kl + (double) kr * kr;
        const float a = std::max (std::fabs (l), std::fabs (r));
        if (a > peakAcc) peakAcc = a;
        if (++subCount >= subLen) closeSubBlock();
    }

    void resetPeak() { truePeakDb = -100.f; }

    float momentary = -100.f, shortTerm = -100.f, integrated = -100.f, truePeakDb = -100.f;

private:
    static constexpr int kBins = 801;          // -70 .. +10 LUFS in 0.1 LU steps
    static constexpr int kRing = 30;           // 3 s of 100 ms sub-blocks

    static float toLufs (double meanSquareSum) { return meanSquareSum > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (meanSquareSum)) : -100.f; }

    void designK (std::array<Biquad, 2>& s, std::array<Biquad, 2>& h) const
    {
        // BS.1770-4 pre-filter (high shelf) and RLB high-pass, derived for any sample rate
        const double G = 3.999843853973347, Q1 = 0.7071752369554196, f1 = 1681.974450955533;
        double K = std::tan (Biquad::pi * f1 / sr);
        const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        double a0 = 1.0 + K / Q1 + K * K;
        for (auto& f : s)
            f.setRaw ((Vh + Vb * K / Q1 + K * K), 2.0 * (K * K - Vh), (Vh - Vb * K / Q1 + K * K), a0, 2.0 * (K * K - 1.0), (1.0 - K / Q1 + K * K));
        const double Q2 = 0.5003270373238773, f2 = 38.13547087602444;
        K = std::tan (Biquad::pi * f2 / sr);
        a0 = 1.0 + K / Q2 + K * K;
        for (auto& f : h) f.setRaw (1.0, -2.0, 1.0, a0, 2.0 * (K * K - 1.0), 1.0 - K / Q2 + K * K);
    }

    void closeSubBlock() noexcept
    {
        const double ms = subAcc / (double) subLen;           // sum over channels of mean squares
        subAcc = 0.0; subCount = 0;
        subRing[(size_t) ringPos] = ms;
        ringPos = (ringPos + 1) % kRing;
        filled = std::min (filled + 1, kRing);

        auto meanOf = [this] (int count)
        {
            double s = 0.0;
            for (int i = 0; i < count; ++i) s += subRing[(size_t) ((ringPos - 1 - i + kRing * 2) % kRing)];
            return s / (double) count;
        };
        if (filled >= 4)
        {
            const double m400 = meanOf (4);
            momentary = toLufs (m400);
            if (momentary > -70.f)
            {
                const int bin = std::clamp ((int) std::lround ((momentary + 70.f) * 10.f), 0, kBins - 1);
                ++hist[(size_t) bin]; histEnergy[(size_t) bin] += m400;
            }
        }
        if (filled >= kRing) shortTerm = toLufs (meanOf (kRing));
        else if (filled >= 4) shortTerm = toLufs (meanOf (filled));

        // gated integrated loudness from the histogram
        double sumE = 0.0; long long n = 0;
        for (int b = 0; b < kBins; ++b) { sumE += histEnergy[(size_t) b]; n += hist[(size_t) b]; }
        if (n > 0)
        {
            const float relGate = toLufs (sumE / (double) n) - 10.f;
            double sumG = 0.0; long long nG = 0;
            for (int b = 0; b < kBins; ++b)
                if ((float) b * 0.1f - 70.f >= relGate) { sumG += histEnergy[(size_t) b]; nG += hist[(size_t) b]; }
            integrated = nG > 0 ? toLufs (sumG / (double) nG) : -100.f;
        }

        const float pk = 20.f * std::log10 (std::max (peakAcc, 1.0e-6f));
        if (pk > truePeakDb) truePeakDb = pk;
        peakAcc = 0.f;
    }

    double sr = 44100.0, subAcc = 0.0;
    int subLen = 4800, subCount = 0, ringPos = 0, filled = 0;
    float peakAcc = 0.f;
    std::array<Biquad, 2> shelf, hp;
    std::array<double, kRing> subRing {};
    std::array<long long, kBins> hist {};
    std::array<double, kBins> histEnergy {};
};
} // namespace humrig
