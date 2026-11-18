#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace humrig
{
// YIN fundamental-frequency estimator (de Cheveigne & Kawahara, 2002).
// Works on a decimated signal so the cost stays low at 96 kHz.
class PitchDetector
{
public:
    struct Result { float freqHz = 0.f; float clarity = 0.f; };

    static constexpr int integrationSize = 512;                // samples (decimated)
    static constexpr int windowSize = 1024;                    // samples (decimated) the caller must supply

    void prepare (double decimatedSampleRate)
    {
        sr = decimatedSampleRate;
        diff.assign ((size_t) (integrationSize + 1), 0.f);
        cmnd.assign ((size_t) (integrationSize + 1), 0.f);
    }

    // x must point at windowSize contiguous samples (oldest first).
    Result detect (const float* x, float minHz, float maxHz, float threshold = 0.15f)
    {
        const int tauMin = std::max (2, (int) std::floor (sr / maxHz));
        const int tauMax = std::min (integrationSize - 1, (int) std::ceil (sr / minHz));
        if (tauMax <= tauMin + 2) return {};

        // energy gate: silence -> unvoiced
        double e = 0.0;
        for (int j = 0; j < windowSize; ++j) e += (double) x[j] * x[j];
        if (e / windowSize < 1.0e-8) return {};

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float s = 0.f;
            const float* a = x;
            const float* b = x + tau;
            for (int j = 0; j < integrationSize; ++j) { const float d = a[j] - b[j]; s += d * d; }
            diff[(size_t) tau] = s;
        }

        float running = 0.f;
        cmnd[0] = 1.f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            running += diff[(size_t) tau];
            cmnd[(size_t) tau] = running > 0.f ? diff[(size_t) tau] * (float) tau / running : 1.f;
        }

        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            if (cmnd[(size_t) tau] < threshold)
            {
                while (tau + 1 <= tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            // no confident dip: take the global minimum and report its (low) clarity
            float m = 1e9f;
            for (int tau = tauMin; tau <= tauMax; ++tau)
                if (cmnd[(size_t) tau] < m) { m = cmnd[(size_t) tau]; best = tau; }
            if (best < 0) return {};
        }

        float tauF = (float) best;
        if (best > tauMin && best < tauMax)
        {
            const float s0 = cmnd[(size_t) best - 1], s1 = cmnd[(size_t) best], s2 = cmnd[(size_t) best + 1];
            const float denom = s0 + s2 - 2.f * s1;
            if (std::fabs (denom) > 1e-9f) tauF += 0.5f * (s0 - s2) / denom;
        }
        Result r;
        r.freqHz = (float) (sr / tauF);
        r.clarity = std::clamp (1.f - cmnd[(size_t) best], 0.f, 1.f);
        return r;
    }

private:
    double sr = 22050.0;
    std::vector<float> diff, cmnd;
};
} // namespace humrig
