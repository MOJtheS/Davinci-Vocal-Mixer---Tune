#pragma once
#include <algorithm>
#include <cmath>

namespace humrig
{
// Allocation-free RBJ biquad (transposed direct form II), mono.
struct Biquad
{
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
    float z1 = 0.f, z2 = 0.f;

    void reset() noexcept { z1 = z2 = 0.f; }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setRaw (double B0, double B1, double B2, double A0, double A1, double A2) noexcept
    {
        const double inv = 1.0 / A0;
        b0 = (float) (B0 * inv); b1 = (float) (B1 * inv); b2 = (float) (B2 * inv);
        a1 = (float) (A1 * inv); a2 = (float) (A2 * inv);
    }

    static constexpr double pi = 3.14159265358979323846;

    // Filters designed at/above Nyquist are unstable (NaN/Inf). Keep every design frequency below 0.45 * sr.
    static double safeFreq (double sr, double f) noexcept { return std::min (f, 0.45 * sr); }

    void makeLowShelf (double sr, double f, double gainDb) noexcept
    {
        f = safeFreq (sr, f);
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * pi * f / sr;
        const double c = std::cos (w), s = std::sin (w), al = s / 2.0 * std::sqrt (2.0), t = 2.0 * std::sqrt (A) * al;
        setRaw (A * ((A + 1) - (A - 1) * c + t), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - t),
                (A + 1) + (A - 1) * c + t, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - t);
    }
    void makeHighShelf (double sr, double f, double gainDb) noexcept
    {
        f = safeFreq (sr, f);
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * pi * f / sr;
        const double c = std::cos (w), s = std::sin (w), al = s / 2.0 * std::sqrt (2.0), t = 2.0 * std::sqrt (A) * al;
        setRaw (A * ((A + 1) + (A - 1) * c + t), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - t),
                (A + 1) - (A - 1) * c + t, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - t);
    }
    void makePeak (double sr, double f, double q, double gainDb) noexcept
    {
        f = safeFreq (sr, f);
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * pi * f / sr;
        const double c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setRaw (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void makeHighPass (double sr, double f, double q) noexcept
    {
        f = safeFreq (sr, f);
        const double w = 2.0 * pi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setRaw ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void makeBandPass (double sr, double f, double q) noexcept   // constant 0 dB peak gain
    {
        f = safeFreq (sr, f);
        const double w = 2.0 * pi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setRaw (al, 0.0, -al, 1 + al, -2 * c, 1 - al);
    }
    void makeLowPass (double sr, double f, double q) noexcept
    {
        f = safeFreq (sr, f);
        const double w = 2.0 * pi * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setRaw ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
};
} // namespace humrig
