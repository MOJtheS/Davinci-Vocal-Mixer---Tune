#pragma once
#include <algorithm>
#include <cmath>
#include "Biquad.h"

namespace humrig
{
inline float dbToLin (float db) noexcept { return std::pow (10.f, db * 0.05f); }
inline float linToDb (float lin) noexcept { return 20.f * std::log10 (std::max (lin, 1.0e-9f)); }

// Feed-forward soft-knee compressor with a gain-reduction read-out for the meter.
// With the default Settings it behaves exactly like HumRig v0.2 (attack 8 ms, release 120 ms, knee 6 dB, peak detector, 100 % wet).
class Compressor
{
public:
    struct Settings
    {
        float amount = 40.f;        // 0..100 macro (0 = off): threshold, ratio and make-up follow it
        float ratioOverride = 1.f;  // <= 1.05: use the macro ratio, otherwise 1.1..10
        float attackMs = 8.f, releaseMs = 120.f, kneeDb = 6.f;
        float mix = 1.f;            // parallel mix 0..1
        bool  rms = false;          // detector: false = peak, true = RMS
        float scHpfHz = 20.f;       // side-chain high-pass (<= 20.5: off)
        float thresholdTrimDb = 0.f, makeupTrimDb = 0.f;
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        firstSet = true;                          // force re-derivation of the coefficients for the new sample rate
        set (Settings {});
        reset();
    }
    void reset() { grDb = 0.f; smoothedGr = 0.f; rmsEnv = 0.f; scHp.reset(); }

    void set (const Settings& s)
    {
        const bool timing = s.attackMs != applied.attackMs || s.releaseMs != applied.releaseMs;
        const bool sc = s.scHpfHz != applied.scHpfHz;
        applied = s;
        if (timing || firstSet)
        {
            atkCoef = std::exp (-1.f / (float) (std::max (0.05f, s.attackMs) * 0.001 * sr));
            relCoef = std::exp (-1.f / (float) (std::max (1.0f, s.releaseMs) * 0.001 * sr));
            rmsCoef = std::exp (-1.f / (float) (0.010 * sr));
        }
        if (sc || firstSet)
        {
            scOn = s.scHpfHz > 20.5f;
            if (scOn) scHp.makeHighPass (sr, s.scHpfHz, 0.707);
        }
        firstSet = false;

        const float amount = std::clamp (s.amount, 0.f, 100.f);
        active = amount > 0.5f;
        thresholdDb = -4.f - 0.32f * amount + s.thresholdTrimDb;      // -4 .. -36 dB
        ratio = s.ratioOverride > 1.05f ? std::clamp (s.ratioOverride, 1.1f, 10.f) : 1.5f + 0.045f * amount;   // 1.5 .. 6
        const float macroThreshold = -4.f - 0.32f * amount;
        makeupLin = dbToLin (-macroThreshold * (1.f - 1.f / (1.5f + 0.045f * amount)) * 0.45f + s.makeupTrimDb);
        knee = std::max (0.f, s.kneeDb);
        mix = std::clamp (s.mix, 0.f, 1.f);
        useRms = s.rms;
    }

    float process (float x) noexcept
    {
        if (! active) { smoothedGr *= 0.999f; grDb = smoothedGr; return x; }
        float det = x;
        if (scOn) det = scHp.process (x);
        float level;
        if (useRms) { rmsEnv = det * det + rmsCoef * (rmsEnv - det * det); level = std::sqrt (rmsEnv); }
        else level = std::fabs (det);
        const float levelDb = linToDb (level);
        const float over = levelDb - thresholdDb;
        float targetGr; // <= 0
        if (knee <= 0.001f) targetGr = over > 0.f ? -over * (1.f - 1.f / ratio) : 0.f;
        else if (over <= -knee * 0.5f) targetGr = 0.f;
        else if (over >= knee * 0.5f) targetGr = -over * (1.f - 1.f / ratio);
        else { const float t = over + knee * 0.5f; targetGr = -(1.f - 1.f / ratio) * t * t / (2.f * knee); }

        const float coef = (targetGr < smoothedGr) ? atkCoef : relCoef;
        smoothedGr = targetGr + coef * (smoothedGr - targetGr);
        grDb = smoothedGr;
        const float wet = x * dbToLin (smoothedGr) * makeupLin;
        return mix >= 0.9999f ? wet : x + mix * (wet - x);
    }

    float grDb = 0.f;     // current gain reduction (<= 0)

private:
    double sr = 44100.0;
    Settings applied;
    bool firstSet = true, scOn = false, useRms = false, active = false;
    float atkCoef = 0.f, relCoef = 0.f, rmsCoef = 0.f, smoothedGr = 0.f, rmsEnv = 0.f;
    float thresholdDb = -20.f, ratio = 2.f, makeupLin = 1.f, knee = 6.f, mix = 1.f;
    Biquad scHp;
};

// De-esser: reduces the sibilance band when it gets too loud.
//   Band  (default): a band-pass around the chosen frequency is detected and turned down (no phase trouble near the corner).
//   Split : v0.2 behaviour - everything above the frequency is turned down (2nd-order high-pass, subtracted from the signal).
//   Wide  : the whole signal is turned down when the band is too loud.
// "Listen" outputs the detection band so the right frequency can be found by ear.
class DeEsser
{
public:
    enum Mode { band = 0, split = 1, wide = 2 };
    struct Settings
    {
        float amount = 40.f;        // 0..100 macro (0 = off)
        float freqHz = 5500.f;
        int   mode = band;
        float maxReductionDb = 0.f; // 0 = from macro (3..15 dB)
        float thresholdTrimDb = 0.f;
        bool  listen = false;
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        atk = std::exp (-1.f / (float) (0.0007 * sr));
        rel = std::exp (-1.f / (float) (0.045 * sr));
        designedFreq = -1.f; designedMode = -1;
        set (Settings {});
        reset();
    }
    void reset() { filt.reset(); env = 0.f; gainDb = 0.f; }

    void set (const Settings& s)
    {
        if (s.freqHz != designedFreq || s.mode != designedMode)
        {
            designedFreq = s.freqHz; designedMode = s.mode;
            if (s.mode == split) filt.makeHighPass (sr, s.freqHz, 0.707);
            else filt.makeBandPass (sr, s.freqHz, 1.5);
        }
        const float amount = std::clamp (s.amount, 0.f, 100.f);
        active = amount > 0.5f;
        thresholdDb = -22.f - 0.20f * amount + s.thresholdTrimDb;     // -22 .. -42 dB (band level)
        maxReductionDb = s.maxReductionDb > 0.05f ? s.maxReductionDb : 3.f + 0.12f * amount;   // 3 .. 15 dB
        mode = s.mode; listen = s.listen;
    }

    float process (float x) noexcept
    {
        const float b = filt.process (x);
        if (listen) return b;
        if (! active) return x;
        const float a = std::fabs (b);
        env = a + (a > env ? atk : rel) * (env - a);
        const float envDb = linToDb (env);
        float targetDb = 0.f;
        if (envDb > thresholdDb) targetDb = -std::min (maxReductionDb, (envDb - thresholdDb) * 0.8f);
        gainDb = targetDb + (targetDb < gainDb ? atk : rel) * (gainDb - targetDb);
        if (mode == wide) return x * dbToLin (gainDb);
        return x - b * (1.f - dbToLin (gainDb));
    }

    float reductionDb() const { return gainDb; }

private:
    double sr = 44100.0;
    Biquad filt;
    float atk = 0.f, rel = 0.f, env = 0.f, gainDb = 0.f, designedFreq = -1.f;
    float thresholdDb = -30.f, maxReductionDb = 6.f;
    int designedMode = -1, mode = band;
    bool active = false, listen = false;
};

// Saturation (soft / tube / tape flavours) with tone, parallel mix and an optional high-frequency exciter.
// Default Settings (type 0, tone 0, mix 1, exciter 0) are bit-identical to the v0.2 tanh drive.
class Drive
{
public:
    struct Settings { float amount = 15.f; int type = 0; float tone = 0.f; float mix = 1.f; float excFreq = 5000.f; float excAmount = 0.f; };

    void prepare (double sampleRate) { sr = sampleRate; toneOn = false; excDesigned = -1.f; toneDesigned = 1000.f; excHp.reset(); toneShelf.reset(); lpState = 0.f; }
    void reset() { excHp.reset(); toneShelf.reset(); lpState = 0.f; }

    void set (const Settings& s)
    {
        const float amount = std::clamp (s.amount, 0.f, 100.f);
        wet = amount / 100.f;
        k = 1.f + 0.10f * amount;
        comp = 1.f / std::sqrt (k);
        type = std::clamp (s.type, 0, 2);
        mix = std::clamp (s.mix, 0.f, 1.f);
        toneOn = std::fabs (s.tone) > 0.5f;
        if (toneOn && s.tone != toneDesigned) { toneDesigned = s.tone; toneShelf.makeHighShelf (sr, 3000.0, (double) s.tone * 0.06); }
        excAmt = std::clamp (s.excAmount, 0.f, 100.f) * 0.01f;
        if (excAmt > 0.f && s.excFreq != excDesigned) { excDesigned = s.excFreq; excHp.makeHighPass (sr, s.excFreq, 0.707); }
        lpCoef = 1.f - std::exp (-2.f * (float) Biquad::pi * 9000.f / (float) sr);
    }

    float process (float x) noexcept
    {
        float out = x;
        if (wet > 0.f)
        {
            float y;
            switch (type)
            {
                case 1:  y = (std::tanh (k * x + 0.3f) - 0.2913f) * comp * 1.05f; break;           // tube: asymmetric -> even harmonics (tanh(0.3) = 0.2913)
                case 2:  { const float t = (2.f / 3.14159265f) * std::atan (1.3f * k * x) * comp * 1.1f;   // tape: soft knee + high-cut
                           lpState += lpCoef * (t - lpState); y = lpState; break; }
                default: y = std::tanh (k * x) * comp; break;
            }
            out = x + wet * (y - x);
            if (mix < 0.9999f) out = x + mix * (out - x);
        }
        if (toneOn) out = toneShelf.process (out);
        if (excAmt > 0.f) out += excAmt * 0.6f * std::tanh (3.f * excHp.process (x));
        return out;
    }

private:
    double sr = 44100.0;
    float wet = 0.f, k = 1.f, comp = 1.f, mix = 1.f, excAmt = 0.f, excDesigned = -1.f, toneDesigned = 1000.f, lpState = 0.f, lpCoef = 0.5f;
    int type = 0;
    bool toneOn = false;
    Biquad excHp, toneShelf;
};

// Downward expander: gently turns the noise between phrases down (not a hard gate), with hold and smooth release.
class Expander
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        rmsCoef = std::exp (-1.f / (float) (0.015 * sr));       // 15 ms RMS window: follows the noise floor, ignores single peaks
        openCoef = std::exp (-1.f / (float) (0.003 * sr));
        reset();
    }
    void reset() { env = 0.f; gainDb = 0.f; holdLeft = 0; }

    void set (bool on, float thresholdDb, float rangeDb, float holdMs, float releaseMs)
    {
        enabled = on;
        thr = thresholdDb;
        range = std::max (0.f, rangeDb);
        holdSamples = (int) (holdMs * 0.001 * sr);
        closeCoef = std::exp (-1.f / (float) (std::max (5.f, releaseMs) * 0.001 * sr));
    }

    float process (float x) noexcept
    {
        if (! enabled) { gainDb = 0.f; return x; }
        env = x * x + rmsCoef * (env - x * x);
        const float levelDb = 10.f * std::log10 (std::max (env, 1.0e-18f));
        float target = 0.f;                                   // desired gain in dB (<= 0)
        if (levelDb < thr) target = -std::min (range, 0.8f * (thr - levelDb));   // about 1.8:1 downward expansion
        if (target >= gainDb) { holdLeft = holdSamples; }     // opening: no hold needed, but re-arm
        float coef;
        if (target < gainDb) { if (holdLeft > 0) { --holdLeft; target = gainDb; coef = closeCoef; } else coef = closeCoef; }
        else coef = openCoef;
        gainDb = target + coef * (gainDb - target);
        return x * dbToLin (gainDb);
    }

    float reductionDb() const { return gainDb; }

private:
    double sr = 44100.0;
    bool enabled = false;
    float thr = -50.f, range = 12.f, env = 0.f, gainDb = 0.f;
    float rmsCoef = 0.f, openCoef = 0.f, closeCoef = 0.f;
    int holdSamples = 0, holdLeft = 0;
};

// Level rider ("leveler"): a slow, gentle automatic fader that moves loud and quiet phrases towards one target level before the compressor.
// It holds its gain during pauses (below `pauseDb`) so that noise is never ridden up.
class Rider
{
public:
    void prepare (double sampleRate) { sr = sampleRate; rmsCoef = std::exp (-1.f / (float) (0.12 * sr)); reset(); }
    void reset() { e2 = 0.f; gainDb = 0.f; lin = 1.f; counter = 0; }

    // speed 0..100: 0 = very slow (about 2 s), 100 = fast (about 0.15 s)
    void set (bool on, float targetDb, float rangeDb, float speed, float pauseDbfs)
    {
        enabled = on; target = targetDb; range = std::max (0.f, rangeDb); pauseDb = pauseDbfs;
        const float tau = 2.0f * std::pow (0.15f / 2.0f, std::clamp (speed, 0.f, 100.f) * 0.01f);
        coef = std::exp (-16.f / (float) (tau * sr));            // the gain is updated every 16 samples
    }

    float process (float x) noexcept
    {
        e2 = x * x + rmsCoef * (e2 - x * x);
        if ((++counter & 15) == 0)
        {
            float desired = 0.f;
            if (! enabled) desired = 0.f;
            else
            {
                const float levelDb = 10.f * std::log10 (std::max (e2, 1.0e-12f));
                desired = levelDb < pauseDb ? gainDb : std::clamp (target - levelDb, -range, range);   // pause: hold
            }
            gainDb = desired + coef * (gainDb - desired);
            lin = dbToLin (gainDb);
        }
        return x * lin;
    }

    float currentDb() const { return gainDb; }

private:
    double sr = 44100.0;
    bool enabled = false;
    float target = -20.f, range = 6.f, pauseDb = -55.f, coef = 0.999f, rmsCoef = 0.999f, e2 = 0.f, gainDb = 0.f, lin = 1.f;
    int counter = 0;
};
} // namespace humrig
