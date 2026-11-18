#pragma once
#include <atomic>
#include <cstdint>

namespace humrig
{
// Process-wide mailbox shared by every HumRig instance in the host process.
//
// Why: many hosts (DaVinci Resolve on the Edit/Cut pages included) create the plugin's editor on one
// instance while the audio engine processes audio on another. Meters and the "Listening" analysis therefore
// have to travel between instances. Everything here is lock-free atomics, safe to touch from the audio thread.
struct SharedBus
{
    static SharedBus& get() { static SharedBus bus; return bus; }

    // ---- meters (written by whichever instance is actively processing audio) ----
    std::atomic<float>   grDb { 0.f };
    std::atomic<float>   inPeak { 0.f };
    std::atomic<float>   outPeak { 0.f };
    std::atomic<float>   detectedMidi { -1.f };
    std::atomic<float>   targetMidi { -1.f };
    std::atomic<int64_t> lastActiveMs { 0 };
    std::atomic<int>     activeInstance { 0 };

    // ---- "Listening" ----
    enum ListenState { idle = 0, listening = 1, done = 2, writing = 3 };
    std::atomic<int>   listenState { idle };
    std::atomic<int>   requestGen { 0 };        // bumped by the editor to start a new analysis
    std::atomic<int>   resultGen { -1 };        // generation the stored result belongs to
    std::atomic<float> listenProgress { 0.f };  // 0..1
    struct Result
    {
        std::atomic<int>   key { 0 };
        std::atomic<int>   minor { 0 };
        std::atomic<int>   female { 0 };
        std::atomic<int>   range { 0 };         // 0 Auto, 1 Soprano, 2 Alto/Tenor, 3 Bass
        std::atomic<float> medianF0 { 0.f };
        std::atomic<float> keyConfidence { 0.f };
        std::atomic<float> inGainDb { 0.f };
        std::atomic<float> comp { 40.f };
        std::atomic<float> warmth { 50.f };
        std::atomic<float> presence { 50.f };
        std::atomic<float> air { 50.f };
        std::atomic<float> deess { 40.f };
    } result;

    int nextInstanceId() { return ++instanceCounter; }

    // raises `listenProgress` to at least v (several instances may be listening at once)
    void raiseProgress (float v) noexcept
    {
        float cur = listenProgress.load();
        while (v > cur && ! listenProgress.compare_exchange_weak (cur, v)) {}
    }

private:
    std::atomic<int> instanceCounter { 0 };
};
} // namespace humrig
