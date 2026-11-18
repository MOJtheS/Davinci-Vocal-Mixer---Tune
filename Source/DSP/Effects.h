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
        reset();
    }
    void reset()
    {
        std::fill (bufL.begin(), bufL.end(), 0.f);
        std::fill (bufR.begin(), bufR.end(), 0.f);
        pos = 0; lpL = lpR = 0.f;
        curDelay = targetDelay;
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
        const float lpCoef = 0.35f;   // gentle high-cut in the feedback loop
        for (int i = 0; i < n; ++i)
        {
            curDelay += (targetDelay - curDelay) * 0.0008f;
            const float dl = read (bufL, curDelay), dr = read (bufR, curDelay);
            lpL += lpCoef * (dl - lpL);
            lpR += lpCoef * (dr - lpR);
            if (ping)
            {
                bufL[(size_t) pos] = in[i] + fb * lpR;
                bufR[(size_t) pos] = lpL;
            }
            else
            {
                bufL[(size_t) pos] = in[i] + fb * lpL;
                bufR[(size_t) pos] = in[i] + fb * lpR;
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
        const int i0 = (int) rp;
        const float f = rp - (float) i0;
        const int i1 = (i0 + 1 >= size) ? 0 : i0 + 1;
        return b[(size_t) i0] * (1.f - f) + b[(size_t) i1] * f;
    }

    double sr = 44100.0;
    int size = 1, pos = 0;
    std::vector<float> bufL, bufR;
    float curDelay = 1000.f, targetDelay = 1000.f, fb = 0.3f, wetLin = 0.f, lpL = 0.f, lpR = 0.f;
    bool ping = true, firstSet = true;
};
} // namespace humrig
