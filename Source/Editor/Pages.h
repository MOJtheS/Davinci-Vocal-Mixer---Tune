#pragma once
#include <memory>
#include <vector>
#include "PluginEditor.h"

namespace humrig
{
// ---- generic page: a row of "cards", each with knobs / switches / selectors bound to parameters ----
struct PageItem
{
    enum Kind { Knob, Toggle, Choice };
    Kind kind;
    const char* id;
    const char* title;
};
struct PageGroup
{
    const char* title;
    std::vector<PageItem> items;
    float weight = 1.f;
};

class ControlPage : public juce::Component
{
public:
    ControlPage (HumRigProcessor&, std::vector<PageGroup>, std::unique_ptr<juce::Component> display = nullptr, int displayHeight = 0, juce::String note = {});
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct GroupUI { PageGroup def; juce::Rectangle<int> bounds; std::vector<juce::Component*> knobs, others; };
    HumRigProcessor& proc;
    std::vector<GroupUI> groups;
    std::unique_ptr<juce::Component> display;
    int displayHeight;
    juce::String note;

    std::vector<std::unique_ptr<KnobBox>> knobBoxes;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> toggleAtt;
    std::vector<std::unique_ptr<ChoiceSegment>> segments;
    std::vector<std::unique_ptr<juce::ComboBox>> combos;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtt;
};

// ---- displays ----
// EQ response curve drawn from the real filter coefficients.
class EqCurveDisplay : public juce::Component, private juce::Timer
{
public:
    explicit EqCurveDisplay (HumRigProcessor&);
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    HumRigProcessor& proc;
    EqValues shown;
    bool first = true;
};

// Scrolling gain-reduction history of the compressor.
class GrHistoryDisplay : public juce::Component, private juce::Timer
{
public:
    explicit GrHistoryDisplay (HumRigProcessor&);
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override;
    HumRigProcessor& proc;
    std::vector<float> history;
    int pos = 0;
};

// Loudness read-outs (LUFS) + peak + limiter reduction.
class MasterDisplay : public juce::Component, private juce::Timer
{
public:
    explicit MasterDisplay (HumRigProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override { repaint(); }
    HumRigProcessor& proc;
    juce::TextButton resetBtn { "Reset" };
};

// Row of tabs with arrows at both ends.
class PageNav : public juce::Component
{
public:
    PageNav (juce::StringArray names);
    void setCurrent (int);
    int current() const { return cur; }
    std::function<void (int)> onSelect;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    juce::StringArray names;
    int cur = 0;
};

std::vector<std::pair<juce::String, std::unique_ptr<juce::Component>>> createPages (HumRigProcessor&);
} // namespace humrig
