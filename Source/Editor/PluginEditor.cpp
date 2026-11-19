#include "PluginEditor.h"
#include "Pages.h"
#include "../DSP/Scale.h"

namespace humrig
{
using namespace colours;
using juce::Colour; using juce::Graphics; using juce::Rectangle;

static juce::Font fontOf (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

// ------------------------------------------------------------ look & feel
HumRigLookAndFeel::HumRigLookAndFeel()
{
    setColour (juce::ComboBox::textColourId, ink);
    setColour (juce::PopupMenu::backgroundColourId, card);
    setColour (juce::PopupMenu::textColourId, ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentA);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

void HumRigLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.f);
    const float d = std::min (b.getWidth(), b.getHeight());
    const auto r = Rectangle<float> (d, d).withCentre (b.getCentre());
    const auto c = r.getCentre();

    // drop shadow + body
    g.setColour (juce::Colour (0x22505a8c));
    g.fillEllipse (r.translated (0.f, 3.f).expanded (1.f));
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, r.getX(), r.getY(), Colour (0xffdfe3f0), r.getRight(), r.getBottom(), false));
    g.fillEllipse (r);
    g.setColour (Colour (0xffffffff)); g.drawEllipse (r.reduced (0.5f), 1.f);

    // value arc
    const float ar = d * 0.5f + 1.f;
    juce::Path track, val;
    track.addCentredArc (c.x, c.y, ar, ar, 0.f, a0, a1, true);
    g.setColour (Colour (0xffc9cde0));
    g.strokePath (track, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float ang = a0 + pos * (a1 - a0);
    val.addCentredArc (c.x, c.y, ar, ar, 0.f, a0, ang, true);
    g.setGradientFill (juce::ColourGradient (accentA, r.getX(), r.getBottom(), accentB, r.getRight(), r.getY(), false));
    g.strokePath (val, juce::PathStrokeType (3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // pointer
    juce::Path p;
    const float len = d * 0.30f;
    p.addRoundedRectangle (-1.6f, -d * 0.5f + 7.f, 3.2f, len, 1.6f);
    g.setColour (s.isEnabled() ? ink : inkSoft);
    g.fillPath (p, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
}

void HumRigLookAndFeel::drawToggleButton (Graphics& g, juce::ToggleButton& t, bool hl, bool)
{
    auto b = t.getLocalBounds().toFloat().reduced (1.f);
    const bool on = t.getToggleState();
    g.setColour (on ? ink : Colour (0xffe3e6f3));
    g.fillRoundedRectangle (b, b.getHeight() / 2.f);
    if (hl) { g.setColour (juce::Colours::white.withAlpha (0.12f)); g.fillRoundedRectangle (b, b.getHeight() / 2.f); }
    const float dotR = 4.f;
    auto dot = juce::Rectangle<float> (dotR * 2, dotR * 2).withCentre ({ b.getX() + 14.f, b.getCentreY() });
    g.setColour (on ? accentB : inkSoft);
    g.fillEllipse (dot);
    g.setColour (on ? juce::Colours::white : ink);
    g.setFont (fontOf (12.5f, true));
    g.drawText (t.getButtonText(), b.withTrimmedLeft (24.f), juce::Justification::centredLeft);
}

void HumRigLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto b = juce::Rectangle<float> (0.f, 0.f, (float) w, (float) h).reduced (0.5f);
    g.setColour (Colour (0xffe3e6f3)); g.fillRoundedRectangle (b, 8.f);
    juce::Path arrow;
    const float cx = (float) w - 14.f, cy = (float) h * 0.5f;
    arrow.addTriangle (cx - 4.f, cy - 2.f, cx + 4.f, cy - 2.f, cx, cy + 3.f);
    g.setColour (inkSoft); g.fillPath (arrow);
}
juce::Font HumRigLookAndFeel::getComboBoxFont (juce::ComboBox&) { return fontOf (13.f, true); }
void HumRigLookAndFeel::positionComboBoxText (juce::ComboBox& c, juce::Label& l)
{
    l.setBounds (10, 1, c.getWidth() - 28, c.getHeight() - 2);
    l.setFont (getComboBoxFont (c));
}
void HumRigLookAndFeel::drawButtonBackground (Graphics& g, juce::Button& b, const Colour&, bool hl, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? Colour (0xffd3d7ea) : hl ? Colour (0xffeceef9) : Colour (0xffe3e6f3));
    g.fillRoundedRectangle (r, 8.f);
}

// ------------------------------------------------------------ KnobBox
KnobBox::KnobBox (juce::AudioProcessorValueTreeState& a, const juce::String& pid, const juce::String& t)
    : apvts (a), id (pid), attach (a, pid, slider)
{
    addAndMakeVisible (slider);
    title.setText (t, juce::dontSendNotification);
    title.setJustificationType (juce::Justification::centred);
    title.setFont (fontOf (12.5f, true));
    title.setColour (juce::Label::textColourId, ink);
    value.setJustificationType (juce::Justification::centred);
    value.setFont (fontOf (11.5f));
    value.setColour (juce::Label::textColourId, inkSoft);
    title.setInterceptsMouseClicks (false, false);
    value.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (title); addAndMakeVisible (value);
    slider.onValueChange = [this] { refreshText(); };
    slider.setDoubleClickReturnValue (true, apvts.getParameter (id)->convertFrom0to1 (apvts.getParameter (id)->getDefaultValue()));
    refreshText();
}
void KnobBox::refreshText()
{
    if (auto* p = apvts.getParameter (id))
    {
        auto txt = p->getText (p->getValue(), 12);
        const auto lab = p->getLabel();
        if (lab.isNotEmpty() && ! txt.endsWithIgnoreCase (lab)) txt << " " << lab;
        value.setText (txt, juce::dontSendNotification);
    }
}
void KnobBox::resized()
{
    auto r = getLocalBounds();
    value.setBounds (r.removeFromBottom (16));
    title.setBounds (r.removeFromBottom (17));
    const int d = std::min ({ r.getWidth(), r.getHeight(), 84 });
    slider.setBounds (r.withSizeKeepingCentre (d, d));
}

// ------------------------------------------------------------ ChoiceSegment
ChoiceSegment::ChoiceSegment (juce::AudioProcessorValueTreeState& a, const juce::String& pid, juce::StringArray l, bool d)
    : param (a.getParameter (pid)), labels (std::move (l)), darkStyle (d),
      attachment (*param, [this] (float v) { current = juce::roundToInt (param->convertFrom0to1 (param->convertTo0to1 (v))); repaint(); })
{
    attachment.sendInitialUpdate();
}
void ChoiceSegment::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (darkStyle ? dark : Colour (0xffe3e6f3));
    g.fillRoundedRectangle (b, b.getHeight() / 2.f);
    const float w = b.getWidth() / (float) labels.size();
    for (int i = 0; i < labels.size(); ++i)
    {
        auto seg = juce::Rectangle<float> (b.getX() + w * (float) i, b.getY(), w, b.getHeight()).reduced (2.f);
        const bool on = i == current;
        if (on)
        {
            g.setGradientFill (juce::ColourGradient (accentA, seg.getX(), seg.getY(), accentB, seg.getRight(), seg.getBottom(), false));
            g.fillRoundedRectangle (seg, seg.getHeight() / 2.f);
        }
        g.setColour (on ? juce::Colours::white : (darkStyle ? Colour (0xffb8bbd0) : ink));
        g.setFont (fontOf (12.f, true));
        g.drawText (labels[i], seg, juce::Justification::centred);
    }
}
void ChoiceSegment::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    const int idx = juce::jlimit (0, labels.size() - 1, (int) (e.position.x / ((float) getWidth() / (float) labels.size())));
    attachment.setValueAsCompleteGesture ((float) idx);
}

// ------------------------------------------------------------ meters
GainReductionMeter::GainReductionMeter (HumRigProcessor& p) : proc (p) { startTimerHz (30); }
void GainReductionMeter::timerCallback()
{
    const auto m = proc.getMeters();
    gr = m.grDb;
    const float target = std::clamp (-gr, 0.f, 20.f);
    shown += (target - shown) * 0.35f;
    level += (m.inPeak - level) * 0.4f;
    repaint();
}
void GainReductionMeter::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (dark); g.fillRoundedRectangle (b, 14.f);
    auto face = b.reduced (6.f);
    g.setGradientFill (juce::ColourGradient (Colour (0xfff4f0e4), face.getX(), face.getY(), Colour (0xffd9d3c0), face.getX(), face.getBottom(), false));
    g.fillRoundedRectangle (face, 10.f);

    const auto pivot = juce::Point<float> (face.getCentreX(), face.getBottom() + 6.f);
    const float radius = face.getHeight() * 0.80f;
    const float a0 = juce::degreesToRadians (-42.f), a1 = juce::degreesToRadians (42.f);

    g.saveState();
    g.reduceClipRegion (face.toNearestInt());
    g.setColour (Colour (0xff3a3a46));
    g.setFont (fontOf (10.f, true));
    for (int i = 0; i <= 10; ++i)
    {
        const float t = (float) i / 10.f, a = a0 + t * (a1 - a0);
        const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
        const float len = (i % 2 == 0) ? 9.f : 5.f;
        g.drawLine (juce::Line<float> (pivot + dir * (radius - len), pivot + dir * radius), (i % 2 == 0) ? 1.5f : 1.f);
        if (i % 2 == 0)
            g.drawText (juce::String (i * 2), juce::Rectangle<float> (24.f, 12.f).withCentre (pivot + dir * (radius - 20.f)), juce::Justification::centred);
    }
    g.setColour (Colour (0xffb91c5c));
    const float na = a0 + (shown / 20.f) * (a1 - a0);
    g.drawLine (juce::Line<float> (pivot, pivot + juce::Point<float> (std::sin (na), -std::cos (na)) * (radius - 4.f)), 2.f);
    g.restoreState();

    g.setFont (fontOf (10.f, true)); g.setColour (Colour (0xff6a6657));
    g.drawText ("GAIN REDUCTION", face.withHeight (20.f).translated (10.f, 2.f), juce::Justification::centredLeft);
    auto tag = juce::Rectangle<float> (62.f, 20.f).withCentre ({ face.getRight() - 42.f, face.getBottom() - 18.f });
    g.setColour (dark.withAlpha (0.9f));
    g.fillRoundedRectangle (tag, 5.f);
    g.setColour (juce::Colours::white); g.setFont (fontOf (11.f, true));
    g.drawText (juce::String (gr, 1) + " dB", tag, juce::Justification::centred);
}

LevelBar::LevelBar (std::function<float()> s, juce::String l) : src (std::move (s)), text (std::move (l)) { startTimerHz (30); }
void LevelBar::timerCallback()
{
    const float db = juce::Decibels::gainToDecibels (src(), -60.f);
    const float target = juce::jlimit (0.f, 1.f, (db + 60.f) / 60.f);
    shown = target > shown ? target : shown + (target - shown) * 0.15f;
    repaint();
}
void LevelBar::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (inkSoft); g.setFont (fontOf (10.f, true));
    g.drawText (text, b.removeFromTop (14.f), juce::Justification::centredLeft);
    g.setColour (dark); g.fillRoundedRectangle (b, b.getHeight() / 2.f);
    auto fill = b.reduced (2.f);
    fill = fill.withWidth (std::max (0.f, fill.getWidth() * shown));
    g.setGradientFill (juce::ColourGradient (accentA, b.getX(), 0.f, accentB, b.getRight(), 0.f, false));
    g.fillRoundedRectangle (fill, fill.getHeight() / 2.f);
}

PitchReadout::PitchReadout (HumRigProcessor& p) : proc (p) { startTimerHz (30); }
void PitchReadout::timerCallback()
{
    const auto m = proc.getMeters();
    detected = m.detectedMidi; target = m.targetMidi;
    repaint();
}
static juce::String noteName (int midi)
{
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (n[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
}
void PitchReadout::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (dark); g.fillRoundedRectangle (b, 12.f);
    const bool voiced = detected > 0.f && target > 0.f;
    g.setColour (voiced ? juce::Colours::white : Colour (0xff6c6f86));
    g.setFont (fontOf (b.getHeight() * 0.52f, true));
    g.drawText (voiced ? noteName ((int) std::lround (target)) : "--", b.withTrimmedRight (b.getWidth() * 0.38f), juce::Justification::centred);
    g.setFont (fontOf (11.f, true));
    g.setColour (accentB);
    const int cents = voiced ? juce::roundToInt ((detected - target) * 100.f) : 0;
    g.drawText (voiced ? ((cents > 0 ? "+" : "") + juce::String (cents) + " ct") : "no pitch",
                b.withLeft (b.getRight() - b.getWidth() * 0.40f), juce::Justification::centred);
}

// ------------------------------------------------------------ Listening button
static juce::String keyLabel (int key, bool minor)
{
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (n[juce::jlimit (0, 11, key)]) + (minor ? " minor" : " major");
}

ListenButton::ListenButton (HumRigProcessor& p) : proc (p) { startTimerHz (20); }

void ListenButton::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    auto& b = HumRigProcessor::bus();
    if (b.listenState.load() == SharedBus::listening) { proc.cancelListening(); pendingGen = -1; }
    else { applied = false; summary = {}; pendingGen = proc.requestListening(); }
    repaint();
}

void ListenButton::timerCallback()
{
    auto& b = HumRigProcessor::bus();
    if (pendingGen >= 0 && ! applied && b.listenState.load() == SharedBus::done && b.resultGen.load() == pendingGen)
    {
        if (proc.applyListeningResults (pendingGen))
        {
            applied = true;
            const auto& r = b.result;
            summary = keyLabel (r.key.load(), r.minor.load() != 0) + (r.female.load() ? "  |  female" : "  |  male");
        }
    }
    repaint();
}

void ListenButton::paint (Graphics& g)
{
    auto& b = HumRigProcessor::bus();
    const bool listening = b.listenState.load() == SharedBus::listening && pendingGen >= 0;
    auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (accentA, r.getX(), r.getY(), accentB, r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle (r, r.getHeight() / 2.f);
    if (listening)   // progress sheen
    {
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillRoundedRectangle (r.withWidth (std::max (r.getHeight(), r.getWidth() * b.listenProgress.load())), r.getHeight() / 2.f);
    }
    g.setColour (juce::Colours::white);
    g.setFont (fontOf (13.f, true));
    juce::String label = "Listening";
    if (listening)  label << " " << juce::roundToInt (b.listenProgress.load() * 100.f) << "%";
    else if (applied) label = "Listened  -  " + summary;
    g.drawText (label, r, juce::Justification::centred);
}

// ------------------------------------------------------------ editor
HumRigEditor::HumRigEditor (HumRigProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      voiceSeg (p.apvts, ID::voice, { "Male", "Female" }, true),
      grMeter (p), pitchOut (p), inBar ([&p] { return p.getMeters().inPeak; }, "INPUT"), outBar ([&p] { return p.getMeters().outPeak; }, "OUTPUT"), listenBtn (p),
      warmth (p.apvts, ID::warmth, "Warmth"), presence (p.apvts, ID::presence, "Presence"), air (p.apvts, ID::air, "Air"),
      comp (p.apvts, ID::comp, "Comp"), deess (p.apvts, ID::deess, "De-Esser"), drive (p.apvts, ID::drive, "Drive"),
      speed (p.apvts, ID::speed, "Speed"), vibrato (p.apvts, ID::vibrato, "Vibrato"), flex (p.apvts, ID::flex, "Flex"),
      revMix (p.apvts, ID::revMix, "Mix"), revSize (p.apvts, ID::revSize, "Size"), revDecay (p.apvts, ID::revDecay, "Decay"),
      dlyMix (p.apvts, ID::dlyMix, "Mix"), dlyFb (p.apvts, ID::dlyFb, "Feedback"),
      inGain (p.apvts, ID::inGain, "Input"), outGain (p.apvts, ID::outGain, "Output"), mixKnob (p.apvts, ID::mix, "Dry/Wet"),
      rangeSeg (p.apvts, ID::range, { "Auto", "Soprano", "Alto/Tenor", "Bass" }),
      dlyTimeSeg (p.apvts, ID::dlyTime, { "1/4", "1/8", "1/16" }),
      tuneAtt (p.apvts, ID::tuneOn, tuneOn), pingAtt (p.apvts, ID::dlyPing, ping)
{
    setLookAndFeel (&laf);
    setSize (kWidth, kHeight);

    for (int i = 0; i < 12; ++i) keyBox.addItem (kKeyNames[i], i + 1);
    for (int i = 0; i < kNumScales; ++i) scaleBox.addItem (kScaleNames[i], i + 1);
    keyAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ID::key, keyBox);
    scaleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, ID::scale, scaleBox);

    {
        juce::String lastGenre;
        for (int i = 0; i < getNumPresets(); ++i)
        {
            if (lastGenre != getPreset (i).genre) { lastGenre = getPreset (i).genre; presetBox.addSectionHeading (lastGenre); }
            presetBox.addItem (getPreset (i).name, i + 1);
        }
    }
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { proc.applyPreset (presetBox.getSelectedId() - 1); };
    prevBtn.onClick = [this] { presetBox.setSelectedId ((presetBox.getSelectedId() - 1 + getNumPresets() - 1) % getNumPresets() + 1, juce::sendNotification); };
    nextBtn.onClick = [this] { presetBox.setSelectedId ((presetBox.getSelectedId() - 1 + 1) % getNumPresets() + 1, juce::sendNotification); };

    for (auto* c : std::initializer_list<juce::Component*> {
             &presetBox, &prevBtn, &nextBtn, &voiceSeg, &grMeter, &pitchOut, &inBar, &outBar, &listenBtn,
             &warmth, &presence, &air, &comp, &deess, &drive, &speed, &vibrato, &flex,
             &revMix, &revSize, &revDecay, &dlyMix, &dlyFb, &inGain, &outGain, &mixKnob,
             &tuneOn, &ping, &keyBox, &scaleBox, &rangeSeg, &dlyTimeSeg })
        addAndMakeVisible (c);

    // everything in the v0.2 body becomes the "Home" tab (header widgets stay visible on every tab)
    homeWidgets = { &grMeter, &pitchOut, &inBar, &outBar, &warmth, &presence, &air, &comp, &deess, &drive, &speed, &vibrato, &flex,
                    &revMix, &revSize, &revDecay, &dlyMix, &dlyFb, &inGain, &outGain, &mixKnob, &tuneOn, &ping, &keyBox, &scaleBox, &rangeSeg, &dlyTimeSeg };

    juce::StringArray names { "Home" };
    auto made = createPages (p);
    for (auto& pr : made) { names.add (pr.first); addChildComponent (*pr.second); pages.push_back (std::move (pr.second)); }
    nav = std::make_unique<PageNav> (names);
    nav->onSelect = [this] (int i) { setPage (i); };
    addAndMakeVisible (*nav);
    setPage (juce::jlimit (0, (int) pages.size(), proc.uiPage.load()));
    resized();
}

HumRigEditor::~HumRigEditor() { setLookAndFeel (nullptr); }

void HumRigEditor::setPage (int i)
{
    page = juce::jlimit (0, (int) pages.size(), i);
    proc.uiPage = page;
    for (auto* c : homeWidgets) c->setVisible (page == 0);
    for (size_t k = 0; k < pages.size(); ++k) pages[k]->setVisible ((int) k + 1 == page);
    if (nav != nullptr) nav->setCurrent (page);
    repaint();
}

void drawCardAt (Graphics& g, Rectangle<int> r, const juce::String& title)
{
    auto f = r.toFloat();
    g.setColour (juce::Colour (0x14505a8c)); g.fillRoundedRectangle (f.translated (0.f, 3.f), 16.f);
    g.setColour (card); g.fillRoundedRectangle (f, 16.f);
    g.setColour (cardEdge); g.drawRoundedRectangle (f.reduced (0.5f), 16.f, 1.f);
    if (title.isNotEmpty())
    {
        g.setColour (inkSoft); g.setFont (fontOf (11.f, true));
        g.drawText (title, r.getX() + 16, r.getY() + 10, r.getWidth() - 24, 16, juce::Justification::centredLeft);
    }
}

void HumRigEditor::paint (Graphics& g)
{
    g.fillAll (bg);
    g.setColour (ink); g.setFont (fontOf (26.f, true));
    g.drawText ("HUMRIG", 24, 14, 160, 34, juce::Justification::centredLeft);
    g.setColour (inkSoft); g.setFont (fontOf (10.5f));
    g.drawText ("vocal tune & mix for Fairlight", 26, 44, 220, 14, juce::Justification::centredLeft);

    if (page != 0) return;
    drawCard (g, cardMix, {});
    drawCard (g, cardPitch, "PITCH CORRECTION");
    drawCard (g, cardReverb, "REVERB");
    drawCard (g, cardDelay, "DELAY");
    drawCard (g, cardOut, "LEVELS");
}

void HumRigEditor::resized()
{
    auto area = getLocalBounds().reduced (20, 0);
    auto header = area.removeFromTop (64).withTrimmedTop (12);
    voiceSeg.setBounds (header.removeFromRight (150).withSizeKeepingCentre (150, 30));
    listenBtn.setBounds (190, 24, 235, 30);
    header.removeFromRight (16);
    nextBtn.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 30));
    presetBox.setBounds (header.removeFromRight (200).withSizeKeepingCentre (200, 30));
    prevBtn.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 30));

    if (nav != nullptr) nav->setBounds (area.removeFromTop (34)); else area.removeFromTop (34);
    area.removeFromTop (8);
    bodyArea = area.withTrimmedBottom (14);
    for (auto& pg : pages) pg->setBounds (bodyArea);

    auto home = bodyArea;
    auto top = home.removeFromTop (300);
    home.removeFromTop (12);
    auto bottom = home;

    // top-left: meter + level bars on the left, 3x2 knob grid on the right
    cardMix = top.removeFromLeft (530);
    top.removeFromLeft (14);
    cardPitch = top;

    auto m = cardMix.reduced (16);
    auto left = m.removeFromLeft (190);
    m.removeFromLeft (14);
    grMeter.setBounds (left.removeFromTop (140));
    left.removeFromTop (18);
    inBar.setBounds (left.removeFromTop (34));
    left.removeFromTop (12);
    outBar.setBounds (left.removeFromTop (34));

    const int kw = m.getWidth() / 3, kh = (m.getHeight() - 6) / 2;
    KnobBox* mixKnobs[6] = { &warmth, &presence, &air, &comp, &deess, &drive };
    for (int i = 0; i < 6; ++i)
        mixKnobs[i]->setBounds (m.getX() + (i % 3) * kw, m.getY() + (i / 3) * (kh + 6), kw - 6, kh);

    // pitch card
    auto pc = cardPitch.reduced (16, 0).withTrimmedTop (34);
    auto row1 = pc.removeFromTop (30);
    tuneOn.setBounds (row1.removeFromLeft (70));
    row1.removeFromLeft (8);
    keyBox.setBounds (row1.removeFromLeft (84));
    row1.removeFromLeft (8);
    scaleBox.setBounds (row1);
    pc.removeFromTop (8);
    pitchOut.setBounds (pc.removeFromTop (58));
    pc.removeFromTop (4);
    auto knobs = pc.removeFromTop (108);
    const int pw = knobs.getWidth() / 3;
    speed.setBounds (knobs.removeFromLeft (pw)); vibrato.setBounds (knobs.removeFromLeft (pw)); flex.setBounds (knobs);
    pc.removeFromTop (6);
    rangeSeg.setBounds (pc.removeFromTop (28));

    // bottom cards
    cardReverb = bottom.removeFromLeft (330); bottom.removeFromLeft (14);
    cardDelay = bottom.removeFromLeft (310);  bottom.removeFromLeft (14);
    cardOut = bottom;

    auto rv = cardReverb.reduced (12, 0).withTrimmedTop (30).withTrimmedBottom (8);
    const int rw = rv.getWidth() / 3;
    revMix.setBounds (rv.removeFromLeft (rw)); revSize.setBounds (rv.removeFromLeft (rw)); revDecay.setBounds (rv);

    auto dl = cardDelay.reduced (12, 0).withTrimmedTop (30).withTrimmedBottom (8);
    auto dlk = dl.removeFromLeft (150);
    dlyMix.setBounds (dlk.removeFromLeft (75)); dlyFb.setBounds (dlk);
    dl.removeFromLeft (8);
    dlyTimeSeg.setBounds (dl.removeFromTop (28).withTrimmedTop (2));
    dl.removeFromTop (10);
    ping.setBounds (dl.removeFromTop (28).removeFromLeft (110));

    auto lv = cardOut.reduced (12, 0).withTrimmedTop (30).withTrimmedBottom (8);
    const int lw = lv.getWidth() / 3;
    inGain.setBounds (lv.removeFromLeft (lw)); outGain.setBounds (lv.removeFromLeft (lw)); mixKnob.setBounds (lv);
}
} // namespace humrig
