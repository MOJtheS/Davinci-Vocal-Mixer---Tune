#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../PluginProcessor.h"

namespace humrig
{
namespace colours
{
    inline const juce::Colour bg        { 0xffeceef7 };
    inline const juce::Colour card      { 0xfff7f8fd };
    inline const juce::Colour cardEdge  { 0xffdfe2f0 };
    inline const juce::Colour ink       { 0xff2a2c42 };
    inline const juce::Colour inkSoft   { 0xff8a8fa8 };
    inline const juce::Colour accentA   { 0xff8a4dff };
    inline const juce::Colour accentB   { 0xffec4899 };
    inline const juce::Colour dark      { 0xff1d1e2b };
}

class HumRigLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HumRigLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
};

// Rotary knob + title + value read-out, bound to one float parameter.
class KnobBox : public juce::Component
{
public:
    KnobBox (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& title);
    void resized() override;
    void paint (juce::Graphics&) override {}
private:
    void refreshText();
    juce::AudioProcessorValueTreeState& apvts;
    juce::String id;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label title, value;
    juce::AudioProcessorValueTreeState::SliderAttachment attach;
};

// Segmented selector bound to a choice parameter.
class ChoiceSegment : public juce::Component
{
public:
    ChoiceSegment (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::StringArray labels, bool dark = false);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    juce::RangedAudioParameter* param;
    juce::StringArray labels;
    int current = 0;
    bool darkStyle;
    juce::ParameterAttachment attachment;
};

class GainReductionMeter : public juce::Component, private juce::Timer
{
public:
    explicit GainReductionMeter (HumRigProcessor&);
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    HumRigProcessor& proc;
    float gr = 0.f, shown = 0.f, level = 0.f;
};

class LevelBar : public juce::Component, private juce::Timer
{
public:
    LevelBar (std::atomic<float>& source, juce::String label);
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    std::atomic<float>& src;
    juce::String text;
    float shown = 0.f;
};

class PitchReadout : public juce::Component, private juce::Timer
{
public:
    explicit PitchReadout (HumRigProcessor&);
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    HumRigProcessor& proc;
    float detected = -1.f, target = -1.f;
};

class HumRigEditor : public juce::AudioProcessorEditor
{
public:
    explicit HumRigEditor (HumRigProcessor&);
    ~HumRigEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 900, kHeight = 600;

private:
    void drawCard (juce::Graphics&, juce::Rectangle<int>, const juce::String& title);

    HumRigProcessor& proc;
    HumRigLookAndFeel laf;

    juce::ComboBox presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" };
    ChoiceSegment voiceSeg;

    GainReductionMeter grMeter;
    PitchReadout pitchOut;
    LevelBar inBar, outBar;

    KnobBox warmth, presence, air, comp, deess, drive;
    KnobBox speed, vibrato, flex;
    KnobBox revMix, revSize, revDecay;
    KnobBox dlyMix, dlyFb;
    KnobBox inGain, outGain, mixKnob;

    juce::ToggleButton tuneOn { "On" }, ping { "Ping-pong" };
    juce::ComboBox keyBox, scaleBox;
    ChoiceSegment rangeSeg, dlyTimeSeg;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt, scaleAtt;   // created after the boxes are populated
    juce::AudioProcessorValueTreeState::ButtonAttachment tuneAtt, pingAtt;

    juce::Rectangle<int> cardMix, cardPitch, cardReverb, cardDelay, cardOut;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HumRigEditor)
};
} // namespace humrig

using HumRigEditor = humrig::HumRigEditor;
