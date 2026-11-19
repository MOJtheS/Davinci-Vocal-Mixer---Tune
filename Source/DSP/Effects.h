#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace humrig
{
// Stereo delay with optional ping-pong. Mono in, stereo wet out.
class StereoDelay
{
public:
    void prepare (double sampleRate, double maxSeconds = 4.0)
    {
        sr = sampleRate;
        size = (int) std::ceil (maxSeconds * sr) + 4;
        bufL.assign ((size_t) size, 0.f);
        bufR.assign ((size_t) size, 0.f);
        firstSet = true;                 // delay times from an old sample rate are meaningless now
        targetDelay = curDelay = 1000.f;
        reset();
    }
    void reset()
    {
        std::fill (bufL.begin(), bufL.end(), 0.f);
        std::fill (bufR.begin(), bufR.end(), 0.f);
        pos = 0; lpL = lpR = hpL = hpR = 0.f;
        targetDelay = std::clamp (targetDelay, 16.f, (float) (size - 4));
        curDelay = targetDelay;
    }
    // Feedback-path filters. Default (off): the fixed gentle one-pole low-pass of v0.2 and no high-pass.
    void setFilters (bool custom, float lowpassHz, float highpassHz)
    {
        lpCoef = custom ? 1.f - std::exp (-2.f * 3.14159265f * std::clamp (lowpassHz, 500.f, 0.45f * (float) sr) / (float) sr) : 0.35f;
        hpCoef = (custom && highpassHz > 21.f) ? 1.f - std::exp (-2.f * 3.14159265f * highpassHz / (float) sr) : 0.f;
    }

    void set (double timeSeconds, float feedback, bool pingPong, float wetGain)
    {
        targetDelay = (float) std::clamp (timeSeconds * sr, 16.0, (double) (size - 4));
        fb = std::clamp (feedback, 0.f, 0.92f);
        ping = pingPong;
        wetLin = wetGain;
        if (firstSet) { curDelay = targetDelay; firstSet = false; }
    }

    void process (const float* in, float* outL, float* outR, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            curDelay += (targetDelay - curDelay) * 0.0008f;
            curDelay = std::clamp (curDelay, 1.f, (float) (size - 4));      // never read outside the ring
            const float dl = read (bufL, curDelay), dr = read (bufR, curDelay);
            lpL += lpCoef * (dl - lpL);
            lpR += lpCoef * (dr - lpR);
            float fl = lpL, fr = lpR;
            if (hpCoef > 0.f)
            {
                hpL += hpCoef * (lpL - hpL); hpR += hpCoef * (lpR - hpR);
                fl = lpL - hpL; fr = lpR - hpR;
            }
            if (ping)
            {
                bufL[(size_t) pos] = in[i] + fb * fr;
                bufR[(size_t) pos] = fl;
            }
            else
            {
                bufL[(size_t) pos] = in[i] + fb * fl;
                bufR[(size_t) pos] = in[i] + fb * fr;
            }
            outL[i] = dl * wetLin;
            outR[i] = dr * wetLin;
            if (++pos >= size) pos = 0;
        }
    }

private:
    float read (const std::vector<float>& b, float delay) const noexcept
    {
        float rp = (float) pos - delay;
        if (rp < 0.f) rp += (float) size;
        if (rp >= (float) size) rp -= (float) size;        // float rounding can land exactly on `size`
        const int i0 = std::clamp ((int) rp, 0, size - 1);
        const float f = rp - (float) i0;
        const int i1 = (i0 + 1 >= size) ? 0 : i0 + 1;
        return b[(size_t) i0] * (1.f - f) + b[(size_t) i1] * f;
    }

    double sr = 44100.0;
    int size = 1, pos = 0;
    std::vector<float> bufL, bufR;
    float curDelay = 1000.f, targetDelay = 1000.f, fb = 0.3f, wetLin = 0.f, lpL = 0.f, lpR = 0.f, hpL = 0.f, hpR = 0.f, lpCoef = 0.35f, hpCoef = 0.f;
    bool ping = true, firstSet = true;
};

// Mono pre-delay (0..maxMs) for the reverb send.
class PreDelay
{
public:
    void prepare (double sampleRate, double maxSeconds = 0.13)
    {
        sr = sampleRate;
        size = (int) std::ceil (maxSeconds * sr) + 4;
        buf.assign ((size_t) size, 0.f);
        pos = 0; delay = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.f); pos = 0; }
    void setMs (float ms) { delay = std::clamp ((int) std::lround (ms * 0.001 * sr), 0, size - 2); }
    float process (float x) noexcept
    {
        if (delay == 0) { buf[(size_t) pos] = x; if (++pos >= size) pos = 0; return x; }   // keep the ring fresh while bypassed
        buf[(size_t) pos] = x;
        int rp = pos - delay; if (rp < 0) rp += size;
        if (++pos >= size) pos = 0;
        return buf[(size_t) rp];
    }
private:
    double sr = 44100.0;
    int size = 1, pos = 0, delay = 0;
    std::vector<float> buf;
};

// Follows the loudness of the dry voice; reverb/delay returns are turned down by it ("ducking") so the voice stays in front.
class Ducker
{
public:
    void prepare (double sampleRate)
    {
        atk = std::exp (-1.f / (float) (0.005 * sampleRate));
        rel = std::exp (-1.f / (float) (0.180 * sampleRate));
        env = 0.f;
    }
    void reset() { env = 0.f; }
    void setAmount (float amount0to100) { amount = std::clamp (amount0to100, 0.f, 100.f) * 0.01f; }
    // returns the gain (0..1) to apply to the effect returns
    float process (float voice) noexcept
    {
        if (amount <= 0.f) return 1.f;
        const float a = std::fabs (voice);
        env = a + (a > env ? atk : rel) * (env - a);
        const float db = 20.f * std::log10 (std::max (env, 1.0e-6f));
        const float depth = std::clamp ((db + 45.f) / 30.f, 0.f, 1.f);      // -45 dB .. -15 dB
        return 1.f - amount * 0.85f * depth;
    }
private:
    float atk = 0.f, rel = 0.f, env = 0.f, amount = 0.f;
};

// Automatic-double-tracking style doubler: two slowly modulated delay taps (one per side), high-passed so the low end stays tight.
// Mono in, stereo out.
class Doubler
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        size = (int) std::ceil (0.06 * sr) + 8;
        buf.assign ((size_t) size, 0.f);
        pos = 0; phase1 = 0.0; phase2 = 0.37;
        hpIn = 0.f;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.f); pos = 0; hpIn = 0.f; hpLp = 0.f; }

    void set (float delayMs, float detuneCents, float hpfHz)
    {
        baseL = delayMs * 0.001f * (float) sr;
        baseR = baseL * 1.37f;
        // vibrato depth that produces +-cents of pitch deviation at the LFO rate
        const float ratio = std::pow (2.f, detuneCents / 1200.f) - 1.f;
        depth1 = ratio * (float) sr / (2.f * 3.14159265f * 0.43f);
        depth2 = ratio * (float) sr / (2.f * 3.14159265f * 0.61f);
        hpCoef = 1.f - std::exp (-2.f * 3.14159265f * hpfHz / (float) sr);
        const float lim = (float) (size - 6);
        baseL = std::clamp (baseL, 4.f, lim - 0.5f * lim); baseR = std::clamp (baseR, 4.f, lim - 0.5f * lim);
        depth1 = std::clamp (depth1, 0.f, std::min (baseL - 2.f, 0.4f * lim));
        depth2 = std::clamp (depth2, 0.f, std::min (baseR - 2.f, 0.4f * lim));
    }

    void process (float in, float& outL, float& outR) noexcept
    {
        hpLp += hpCoef * (in - hpLp);
        const float x = in - hpLp;
        buf[(size_t) pos] = x;
        phase1 += 0.43 / sr; if (phase1 >= 1.0) phase1 -= 1.0;
        phase2 += 0.61 / sr; if (phase2 >= 1.0) phase2 -= 1.0;
        const float dl = baseL + depth1 * (float) std::sin (2.0 * 3.14159265358979 * phase1);
        const float dr = baseR + depth2 * (float) std::sin (2.0 * 3.14159265358979 * phase2);
        outL = read (dl); outR = read (dr);
        if (++pos >= size) pos = 0;
    }

private:
    float read (float delay) const noexcept
    {
        delay = std::clamp (delay, 1.f, (float) (size - 3));
        float rp = (float) pos - delay;
        if (rp < 0.f) rp += (float) size;
        if (rp >= (float) size) rp -= (float) size;        // float rounding can land exactly on `size`
        const int i0 = std::clamp ((int) rp, 0, size - 1);
        const float f = rp - (float) i0;
        const int i1 = (i0 + 1 >= size) ? 0 : i0 + 1;
        return buf[(size_t) i0] * (1.f - f) + buf[(size_t) i1] * f;
    }
    double sr = 44100.0, phase1 = 0.0, phase2 = 0.37;
    int size = 1, pos = 0;
    std::vector<float> buf;
    float baseL = 600.f, baseR = 800.f, depth1 = 0.f, depth2 = 0.f, hpCoef = 0.01f, hpLp = 0.f, hpIn = 0.f;
};
} // namespace humrig
