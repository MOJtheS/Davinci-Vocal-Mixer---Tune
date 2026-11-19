#pragma once
#include <cmath>
#include "Biquad.h"

namespace humrig
{
// Everything that defines the tone-shaping section. Used by the audio processor AND by the EQ curve display, so the picture is always the real filter.
struct EqValues
{
    bool  female = true, manual = false, lpfOn = false, hpf24 = false;
    float warmth = 50.f, presence = 50.f, air = 50.f;                      // macro knobs 0..100 (50 = flat)
    float hpfFreq = 110.f, lowFreq = 240.f;
    float mudFreq = 300.f, mudGain = 0.f, mudQ = 1.f;
    float boxFreq = 800.f, boxGain = 0.f, boxQ = 1.5f;
    float presFreq = 3800.f, presQ = 0.9f, airFreq = 11000.f, lpfFreq = 16000.f;
};

struct EqBands
{
    Biquad hpf, hpf2, mud, box, low, pres, air, lpf;
    bool hpf24 = false, mudOn = false, boxOn = false, lpfOn = false;

    void design (double sr, const EqValues& v)
    {
        // "manual" off = the voice-dependent frequencies of HumRig v0.2
        const double hpfHz  = v.manual ? v.hpfFreq  : (v.female ? 110.0 : 75.0);
        const double lowHz  = v.manual ? v.lowFreq  : (v.female ? 240.0 : 180.0);
        const double presHz = v.manual ? v.presFreq : (v.female ? 3800.0 : 3000.0);
        const double presQ  = v.manual ? v.presQ    : 0.9;
        const double airHz  = v.manual ? v.airFreq  : (v.female ? 11000.0 : 10000.0);
        hpf.makeHighPass (sr, hpfHz, 0.707);
        hpf2.makeHighPass (sr, hpfHz, 0.707);
        hpf24 = v.manual && v.hpf24;
        mudOn = std::fabs (v.mudGain) > 0.01f;   if (mudOn) mud.makePeak (sr, v.mudFreq, v.mudQ, v.mudGain);
        boxOn = std::fabs (v.boxGain) > 0.01f;   if (boxOn) box.makePeak (sr, v.boxFreq, v.boxQ, v.boxGain);
        low.makeLowShelf (sr, lowHz, (v.warmth - 50.0) / 50.0 * 6.0);
        pres.makePeak (sr, presHz, presQ, (v.presence - 50.0) / 50.0 * 5.0);
        air.makeHighShelf (sr, airHz, (v.air - 50.0) / 50.0 * 6.0);
        lpfOn = v.lpfOn;                          if (lpfOn) lpf.makeLowPass (sr, v.lpfFreq, 0.707);
    }

    void reset() { for (auto* f : { &hpf, &hpf2, &mud, &box, &low, &pres, &air, &lpf }) f->reset(); }

    float process (float x) noexcept
    {
        x = hpf.process (x);
        if (hpf24) x = hpf2.process (x);
        if (mudOn) x = mud.process (x);
        if (boxOn) x = box.process (x);
        x = low.process (x);
        x = pres.process (x);
        x = air.process (x);
        if (lpfOn) x = lpf.process (x);
        return x;
    }

    // magnitude response in dB at `f` (for drawing)
    double magnitudeDb (double f, double sr) const
    {
        double db = mag (hpf, f, sr) + (hpf24 ? mag (hpf2, f, sr) : 0.0) + (mudOn ? mag (mud, f, sr) : 0.0) + (boxOn ? mag (box, f, sr) : 0.0)
                  + mag (low, f, sr) + mag (pres, f, sr) + mag (air, f, sr) + (lpfOn ? mag (lpf, f, sr) : 0.0);
        return db;
    }

    static double mag (const Biquad& b, double f, double sr)
    {
        const double w = 2.0 * Biquad::pi * f / sr;
        const double c1 = std::cos (w), s1 = std::sin (w), c2 = std::cos (2 * w), s2 = std::sin (2 * w);
        const double nr = b.b0 + b.b1 * c1 + b.b2 * c2, ni = -(b.b1 * s1 + b.b2 * s2);
        const double dr = 1.0 + b.a1 * c1 + b.a2 * c2, di = -(b.a1 * s1 + b.a2 * s2);
        return 10.0 * std::log10 (std::max (1.0e-20, (nr * nr + ni * ni) / std::max (1.0e-20, dr * dr + di * di)));
    }
};
} // namespace humrig
