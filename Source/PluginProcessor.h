#pragma once
#include <atomic>
#include <array>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "DSP/Biquad.h"
#include "DSP/Dynamics.h"
#include "DSP/Effects.h"
#include "DSP/PitchCorrector.h"

class HumRigProcessor : public juce::AudioProcessor
{
public:
    HumRigProcessor();
    ~HumRigProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "HumRig"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    void applyPreset (int index);

    juce::AudioProcessorValueTreeState apvts;

    // meters / read-outs for the editor
    std::atomic<float> grDbMeter { 0.f };
    std::atomic<float> inLevelMeter { 0.f };
    std::atomic<float> outLevelMeter { 0.f };
    float detectedMidi() const { return corrector.detectedMidi.load(); }
    float targetMidi() const   { return corrector.targetMidi.load(); }

private:
    struct Params
    {
        std::atomic<float> *tuneOn, *key, *scale, *speed, *vibrato, *flex, *range, *voice,
            *warmth, *presence, *air, *comp, *deess, *drive,
            *revMix, *revSize, *revDecay, *dlyMix, *dlyFb, *dlyTime, *dlyPing,
            *inGain, *outGain, *mix;
    } p {};

    void updateEq();
    static float mixDbToLin (float db) { return db <= humrig::kMixOffDb + 0.05f ? 0.f : juce::Decibels::decibelsToGain (db); }

    double sr = 44100.0;
    int maxBlock = 512;

    humrig::PitchCorrector corrector;
    humrig::Biquad hpf, lowShelf, peak, highShelf;
    humrig::Compressor compressor;
    humrig::DeEsser deesser;
    humrig::Drive drive;
    humrig::StereoDelay delay;
    juce::Reverb reverb;

    std::array<std::vector<float>, 2> dryRing;   // latency-aligned dry for the Dry/Wet control
    int dryPos = 0;

    std::vector<float> monoIn, monoTuned, voice, dlyL, dlyR, revL, revR;
    juce::SmoothedValue<float> inGainSm, outGainSm, mixSm;

    float lastVoice = -1.f, lastWarmth = -1.f, lastPresence = -1.f, lastAir = -1.f;
    int currentPreset = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HumRigProcessor)
};
