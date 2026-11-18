#pragma once
#include <algorithm>
#include <cmath>
#include "Biquad.h"

namespace humrig
{
inline float dbToLin (float db) noexcept { return std::pow (10.f, db * 0.05f); }
inline float linToDb (float lin) noexcept { return 20.f * std::log10 (std::max (lin, 1.0e-9f)); }

// Feed-forward soft-knee compressor with a gain-reduction read-out for the meter.
class Compressor
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        atkCoef = std::exp (-1.f / (float) (0.008 * sr));
        relCoef = std::exp (-1.f / (float) (0.120 * sr));
        reset();
    }
    void reset() { grDb = 0.f; smoothedGr = 0.f; }

    // amount 0..100  (0 = off)
    void setAmount (float amount)
    {
        amount = std::clamp (amount, 0.f, 100.f);
        active = amount > 0.5f;
        thresholdDb = -4.f - 0.32f * amount;          // -4 .. -36 dB
        ratio = 1.5f + 0.045f * amount;               // 1.5 .. 6
        makeupDb = -thresholdDb * (1.f - 1.f / ratio) * 0.45f;
        makeupLin = dbToLin (makeupDb);
    }

    float process (float x) noexcept
    {
        if (! active) { smoothedGr *= 0.999f; grDb = smoothedGr; return x; }
        const float levelDb = linToDb (std::fabs (x));
        const float over = levelDb - thresholdDb;
        constexpr float knee = 6.f;
        float targetGr; // <= 0
        if (over <= -knee * 0.5f) targetGr = 0.f;
        else if (over >= knee * 0.5f) targetGr = -over * (1.f - 1.f / ratio);
        else { const float t = over + knee * 0.5f; targetGr = -(1.f - 1.f / ratio) * t * t / (2.f * knee); }

        const float coef = (targetGr < smoothedGr) ? atkCoef : relCoef;
        smoothedGr = targetGr + coef * (smoothedGr - targetGr);
        grDb = smoothedGr;
        return x * dbToLin (smoothedGr) * makeupLin;
    }

    float grDb = 0.f;     // current gain reduction (<= 0)

private:
    double sr = 44100.0;
    float atkCoef = 0.f, relCoef = 0.f, smoothedGr = 0.f;
    float thresholdDb = -20.f, ratio = 2.f, makeupDb = 0.f, makeupLin = 1.f;
    bool active = false;
};

// Split-band de-esser: reduces only the >5.5 kHz band when it gets too loud.
class DeEsser
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        hp.makeHighPass (sr, 5500.0, 0.707);
        atk = std::exp (-1.f / (float) (0.0007 * sr));
        rel = std::exp (-1.f / (float) (0.045 * sr));
        reset();
    }
    void reset() { hp.reset(); env = 0.f; gainDb = 0.f; }

    void setAmount (float amount)   // 0..100
    {
        amount = std::clamp (amount, 0.f, 100.f);
        active = amount > 0.5f;
        thresholdDb = -22.f - 0.20f * amount;         // -22 .. -42 dB (band level)
        maxReductionDb = 3.f + 0.12f * amount;        // 3 .. 15 dB
    }

    float process (float x) noexcept
    {
        const float band = hp.process (x);
        if (! active) return x;
        const float a = std::fabs (band);
        env = a + (a > env ? atk : rel) * (env - a);
        const float envDb = linToDb (env);
        float targetDb = 0.f;
        if (envDb > thresholdDb) targetDb = -std::min (maxReductionDb, (envDb - thresholdDb) * 0.8f);
        gainDb = targetDb + (targetDb < gainDb ? atk : rel) * (gainDb - targetDb);
        return x - band * (1.f - dbToLin (gainDb));
    }

private:
    double sr = 44100.0;
    Biquad hp;
    float atk = 0.f, rel = 0.f, env = 0.f, gainDb = 0.f;
    float thresholdDb = -30.f, maxReductionDb = 6.f;
    bool active = false;
};

// Level-compensated tanh saturation.
class Drive
{
public:
    void setAmount (float amount)   // 0..100
    {
        amount = std::clamp (amount, 0.f, 100.f);
        wet = amount / 100.f;
        k = 1.f + 0.10f * amount;
        comp = 1.f / std::sqrt (k);
    }
    float process (float x) noexcept
    {
        if (wet <= 0.f) return x;
        const float y = std::tanh (k * x) * comp;
        return x + wet * (y - x);
    }
private:
    float wet = 0.f, k = 1.f, comp = 1.f;
};
} // namespace humrig
