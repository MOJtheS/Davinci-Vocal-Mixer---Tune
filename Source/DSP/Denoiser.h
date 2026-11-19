#pragma once
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include <juce_dsp/juce_dsp.h>

namespace humrig
{
// Gentle real-time noise reducer for steady noise (room, hiss, hum): short-time spectrum, per-bin noise floor tracked with minimum statistics
// over about 1.7 s (no "learn" step needed), reduction capped at `reductionDb` (0 = bypass, 12 = maximum) so the voice never turns robotic.
//
// Always delays the audio by latency() samples (even when off) so the latency reported to the host is constant. Sine analysis/synthesis windows with
// 50 % overlap reconstruct the input exactly when all gains are 1.
class Denoiser
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        N = sr <= 60000.0 ? 512 : (sr <= 120000.0 ? 1024 : 2048);
        H = N / 2; bins = N / 2 + 1;
        int order = 0; while ((1 << order) < N) ++order;
        fft = std::make_unique<juce::dsp::FFT> (order);
        window.resize ((size_t) N);
        for (int i = 0; i < N; ++i) window[(size_t) i] = std::sin (3.14159265358979f * ((float) i + 0.5f) / (float) N);
        spec.assign ((size_t) N * 2, 0.f);
        inRing.assign ((size_t) N, 0.f);
        outRing.assign ((size_t) N, 0.f);
        smooth.assign ((size_t) bins, 0.f); gain.assign ((size_t) bins, 1.f); gainS.assign ((size_t) bins, 1.f);
        curMin.assign ((size_t) bins, 1.0e30f);
        subMin.assign ((size_t) kSubs * (size_t) bins, 1.0e30f);
        noise.assign ((size_t) bins, 0.f);
        reset();
    }

    void reset()
    {
        std::fill (inRing.begin(), inRing.end(), 0.f);
        std::fill (outRing.begin(), outRing.end(), 0.f);
        std::fill (smooth.begin(), smooth.end(), 0.f);
        std::fill (gain.begin(), gain.end(), 1.f);
        std::fill (curMin.begin(), curMin.end(), 1.0e30f);
        std::fill (subMin.begin(), subMin.end(), 1.0e30f);
        std::fill (noise.begin(), noise.end(), 0.f);
        wp = rp = 0; hopCount = 0; frames = 0; subFrames = 0; subIndex = 0; warm = 0;
    }

    int latency() const { return N; }
    void setReduction (float db) { reduction = std::clamp (db, 0.f, 24.f); floorGain = std::pow (10.f, -reduction * 0.05f); }

    float process (float x) noexcept
    {
        inRing[(size_t) wp] = x;
        wp = (wp + 1) % N;
        const float y = outRing[(size_t) rp];
        outRing[(size_t) rp] = 0.f;
        rp = (rp + 1) % N;
        if (++hopCount >= H) { hopCount = 0; frame(); }
        return y;
    }

private:
    static constexpr int kSubs = 8, kSubFrames = 40;

    void frame() noexcept
    {
        // newest N samples in time order, windowed
        for (int i = 0; i < N; ++i) spec[(size_t) i] = inRing[(size_t) ((wp + i) % N)] * window[(size_t) i];
        std::fill (spec.begin() + N, spec.end(), 0.f);
        fft->performRealOnlyForwardTransform (spec.data(), false);

        // power spectrum, smoothing, minimum statistics
        const bool active = reduction > 0.05f;
        for (int k = 0; k < bins; ++k)
        {
            const float re = spec[(size_t) (2 * k)], im = spec[(size_t) (2 * k + 1)];
            const float pw = re * re + im * im;
            float& sm = smooth[(size_t) k];
            sm = frames == 0 ? pw : 0.7f * sm + 0.3f * pw;
            if (sm < curMin[(size_t) k]) curMin[(size_t) k] = sm;
        }
        if (++subFrames >= kSubFrames)
        {
            subFrames = 0;
            std::copy (curMin.begin(), curMin.end(), subMin.begin() + (long) subIndex * bins);
            subIndex = (subIndex + 1) % kSubs;
            std::fill (curMin.begin(), curMin.end(), 1.0e30f);
            if (warm < kSubs) ++warm;
        }
        ++frames;
        for (int k = 0; k < bins; ++k)
        {
            float m = curMin[(size_t) k];
            for (int s = 0; s < warm; ++s) m = std::min (m, subMin[(size_t) s * (size_t) bins + (size_t) k]);
            noise[(size_t) k] = m >= 1.0e29f ? 0.f : m * 2.0f;          // minimum underestimates the mean noise power: bias factor 2
        }

        // gains
        for (int k = 0; k < bins; ++k)
        {
            float g = 1.f;
            if (active && smooth[(size_t) k] > 1.0e-20f && noise[(size_t) k] > 0.f)
                g = std::clamp (1.f - noise[(size_t) k] / smooth[(size_t) k], floorGain, 1.f);
            if (! active) g = 1.f;
            gain[(size_t) k] = 0.5f * gain[(size_t) k] + 0.5f * g;
        }
        for (int k = 0; k < bins; ++k)
        {
            const float a = gain[(size_t) std::max (0, k - 1)], b = gain[(size_t) k], c = gain[(size_t) std::min (bins - 1, k + 1)];
            gainS[(size_t) k] = active ? 0.25f * a + 0.5f * b + 0.25f * c : 1.f;
        }
        for (int k = 0; k < bins; ++k)
        {
            const float g = gainS[(size_t) k];
            spec[(size_t) (2 * k)] *= g; spec[(size_t) (2 * k + 1)] *= g;
            if (k > 0 && k < N / 2) { spec[(size_t) (2 * (N - k))] *= g; spec[(size_t) (2 * (N - k) + 1)] *= g; }
        }
        fft->performRealOnlyInverseTransform (spec.data());

        // overlap-add: this frame's first sample is the next one to be output
        for (int i = 0; i < N; ++i)
            outRing[(size_t) ((rp + i) % N)] += spec[(size_t) i] * window[(size_t) i];
    }

    double sr = 48000.0;
    int N = 512, H = 256, bins = 257, wp = 0, rp = 0, hopCount = 0, frames = 0, subFrames = 0, subIndex = 0, warm = 0;
    float reduction = 0.f, floorGain = 1.f;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, spec, inRing, outRing, smooth, gain, gainS, curMin, subMin, noise;
};
} // namespace humrig
