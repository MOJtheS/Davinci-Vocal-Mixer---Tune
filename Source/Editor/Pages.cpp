#include "Pages.h"
#include <cmath>

namespace humrig
{
using namespace colours;
using juce::Colour; using juce::Graphics; using juce::Rectangle;

static juce::Font fontOf (float h, bool bold = false) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }

// ------------------------------------------------------------ ControlPage
ControlPage::ControlPage (HumRigProcessor& p, std::vector<PageGroup> defs, std::unique_ptr<juce::Component> disp, int dispH, juce::String n)
    : proc (p), display (std::move (disp)), displayHeight (dispH), note (std::move (n))
{
    if (display != nullptr) addAndMakeVisible (*display);
    for (auto& d : defs)
    {
        GroupUI g; g.def = d;
        for (const auto& it : d.items)
        {
            if (it.kind == PageItem::Knob)
            {
                knobBoxes.push_back (std::make_unique<KnobBox> (p.apvts, it.id, it.title));
                addAndMakeVisible (*knobBoxes.back());
                g.knobs.push_back (knobBoxes.back().get());
            }
            else if (it.kind == PageItem::Toggle)
            {
                auto t = std::make_unique<juce::ToggleButton> (it.title);
                toggleAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, it.id, *t));
                addAndMakeVisible (*t);
                g.others.push_back (t.get());
                toggles.push_back (std::move (t));
            }
            else
            {
                juce::StringArray labels;
                if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (it.id))) labels = cp->choices;
                if (labels.size() <= 3)
                {
                    segments.push_back (std::make_unique<ChoiceSegment> (p.apvts, it.id, labels));
                    addAndMakeVisible (*segments.back());
                    g.others.push_back (segments.back().get());
                }
                else
                {
                    auto c = std::make_unique<juce::ComboBox>();
                    for (int i = 0; i < labels.size(); ++i) c->addItem (labels[i], i + 1);
                    comboAtt.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, it.id, *c));
                    addAndMakeVisible (*c);
                    g.others.push_back (c.get());
                    combos.push_back (std::move (c));
                }
            }
        }
        groups.push_back (std::move (g));
    }
}

void ControlPage::paint (Graphics& g)
{
    for (auto& gr : groups) drawCardAt (g, gr.bounds, gr.def.title);
    if (note.isNotEmpty())
    {
        g.setColour (inkSoft); g.setFont (fontOf (11.f));
        g.drawFittedText (note, getLocalBounds().removeFromBottom (30).reduced (6, 0), juce::Justification::centredLeft, 2);
    }
}

void ControlPage::resized()
{
    auto area = getLocalBounds();
    if (display != nullptr) { display->setBounds (area.removeFromTop (displayHeight)); area.removeFromTop (12); }
    if (note.isNotEmpty()) area.removeFromBottom (34);
    float total = 0.f; for (auto& g : groups) total += g.def.weight;
    const int gap = 12;
    const int usable = area.getWidth() - gap * (int) (groups.size() - 1);
    int x = area.getX();
    for (size_t gi = 0; gi < groups.size(); ++gi)
    {
        auto& g = groups[gi];
        const int w = gi + 1 == groups.size() ? area.getRight() - x : (int) std::lround ((double) usable * g.def.weight / total);
        g.bounds = { x, area.getY(), w, area.getHeight() };
        x += w + gap;

        auto inner = g.bounds.reduced (10, 0).withTrimmedTop (32).withTrimmedBottom (10);
        for (int i = (int) g.others.size() - 1; i >= 0; --i)        // switches / selectors stack at the bottom
        {
            auto r = inner.removeFromBottom (30);
            g.others[(size_t) i]->setBounds (r);
            inner.removeFromBottom (6);
        }
        const int k = (int) g.knobs.size();
        if (k > 0)
        {
            const int cols = std::max (1, std::min (k, inner.getWidth() / 84));
            const int rows = (k + cols - 1) / cols;
            const int cw = inner.getWidth() / cols, ch = inner.getHeight() / rows;
            for (int i = 0; i < k; ++i)
                g.knobs[(size_t) i]->setBounds (inner.getX() + (i % cols) * cw, inner.getY() + (i / cols) * ch, cw, ch);
        }
    }
}

// ------------------------------------------------------------ EQ curve
EqCurveDisplay::EqCurveDisplay (HumRigProcessor& p) : proc (p) { startTimerHz (15); }
void EqCurveDisplay::timerCallback()
{
    const auto v = proc.currentEqValues();
    if (first || std::memcmp (&v, &shown, sizeof (EqValues)) != 0) { shown = v; first = false; repaint(); }
}
void EqCurveDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (dark); g.fillRoundedRectangle (b, 14.f);
    auto plot = b.reduced (34.f, 16.f);
    const double sr = std::max (8000.0, proc.sampleRate());
    EqBands bands; bands.design (sr, shown);

    auto xOf = [&] (double f) { return plot.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * plot.getWidth(); };
    auto yOf = [&] (double db) { return plot.getCentreY() - (float) (db / 12.0) * plot.getHeight() * 0.5f; };

    g.setFont (fontOf (10.f)); g.setColour (Colour (0xff3a3d55));
    for (double db : { -12.0, -6.0, 0.0, 6.0, 12.0 })
    {
        g.drawHorizontalLine ((int) yOf (db), plot.getX(), plot.getRight());
        g.setColour (Colour (0xff7e82a0)); g.drawText (juce::String ((int) db), (int) b.getX() + 2, (int) yOf (db) - 7, 28, 14, juce::Justification::centredRight);
        g.setColour (Colour (0xff3a3d55));
    }
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        g.drawVerticalLine ((int) xOf (f), plot.getY(), plot.getBottom());
        g.setColour (Colour (0xff7e82a0));
        g.drawText (f >= 1000.0 ? juce::String (f / 1000.0, f >= 10000.0 ? 0 : (f == 1000.0 || f == 2000.0 || f == 5000.0 ? 0 : 1)) + "k" : juce::String ((int) f),
                    (int) xOf (f) - 18, (int) plot.getBottom() + 1, 36, 14, juce::Justification::centred);
        g.setColour (Colour (0xff3a3d55));
    }

    juce::Path curve, fill;
    const int steps = (int) plot.getWidth();
    for (int i = 0; i <= steps; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, (double) i / steps);
        const float y = std::clamp (yOf (bands.magnitudeDb (f, sr)), plot.getY(), plot.getBottom());
        const float x = plot.getX() + (float) i;
        if (i == 0) { curve.startNewSubPath (x, y); fill.startNewSubPath (x, yOf (0.0)); fill.lineTo (x, y); }
        else { curve.lineTo (x, y); fill.lineTo (x, y); }
    }
    fill.lineTo (plot.getRight(), yOf (0.0)); fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (accentA.withAlpha (0.35f), 0.f, plot.getY(), accentB.withAlpha (0.10f), 0.f, plot.getBottom(), false));
    g.fillPath (fill);
    g.setColour (Colour (0xfff2f3ff));
    g.strokePath (curve, juce::PathStrokeType (2.f));

    // band handles
    struct Node { double f; double db; const char* name; bool on; };
    const bool man = shown.manual;
    const Node nodes[] = {
        { man ? shown.hpfFreq : (shown.female ? 110.0 : 75.0), 0.0, "LOW CUT", true },
        { shown.mudFreq, shown.mudGain, "MUD", std::fabs (shown.mudGain) > 0.01f },
        { shown.boxFreq, shown.boxGain, "BOX", std::fabs (shown.boxGain) > 0.01f },
        { man ? shown.presFreq : (shown.female ? 3800.0 : 3000.0), (shown.presence - 50.0) / 50.0 * 5.0, "PRESENCE", true },
        { man ? shown.airFreq : (shown.female ? 11000.0 : 10000.0), (shown.air - 50.0) / 50.0 * 6.0, "AIR", true } };
    g.setFont (fontOf (9.5f, true));
    for (const auto& n : nodes)
    {
        if (! n.on) continue;
        const float x = xOf (n.f), y = std::clamp (yOf (n.db), plot.getY(), plot.getBottom());
        g.setColour (accentB); g.fillEllipse (x - 5.f, y - 5.f, 10.f, 10.f);
        g.setColour (juce::Colours::white); g.drawEllipse (x - 5.f, y - 5.f, 10.f, 10.f, 1.5f);
        g.setColour (Colour (0xffc9cde8)); g.drawText (n.name, (int) x - 30, (int) y - 22, 60, 12, juce::Justification::centred);
    }
}

// ------------------------------------------------------------ GR history
GrHistoryDisplay::GrHistoryDisplay (HumRigProcessor& p) : proc (p), history (180, 0.f) { startTimerHz (30); }
void GrHistoryDisplay::timerCallback()
{
    history[(size_t) pos] = proc.getMeters().grDb;
    pos = (pos + 1) % (int) history.size();
    repaint();
}
void GrHistoryDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (dark); g.fillRoundedRectangle (b, 14.f);
    auto plot = b.reduced (16.f, 14.f);
    g.setColour (Colour (0xff3a3d55));
    for (int db : { 0, 5, 10, 15, 20 })
    {
        const float y = plot.getY() + plot.getHeight() * (float) db / 20.f;
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (Colour (0xff7e82a0)); g.setFont (fontOf (9.5f));
        g.drawText (juce::String (-db), (int) plot.getX() + 2, (int) y + 1, 24, 12, juce::Justification::centredLeft);
        g.setColour (Colour (0xff3a3d55));
    }
    juce::Path path;
    const int n = (int) history.size();
    path.startNewSubPath (plot.getX(), plot.getY());
    for (int i = 0; i < n; ++i)
    {
        const float v = std::clamp (-history[(size_t) ((pos + i) % n)], 0.f, 20.f);
        path.lineTo (plot.getX() + plot.getWidth() * (float) i / (float) (n - 1), plot.getY() + plot.getHeight() * v / 20.f);
    }
    path.lineTo (plot.getRight(), plot.getY()); path.closeSubPath();
    g.setGradientFill (juce::ColourGradient (Colour (0xffff8a5c).withAlpha (0.85f), 0.f, plot.getY(), Colour (0xffff4d6d).withAlpha (0.35f), 0.f, plot.getBottom(), false));
    g.fillPath (path);
    g.setColour (juce::Colours::white); g.setFont (fontOf (22.f, true));
    g.drawText (juce::String (proc.getMeters().grDb, 1) + " dB", b.reduced (20.f, 10.f).removeFromRight (150.f), juce::Justification::topRight);
    g.setColour (Colour (0xff9fa3c0)); g.setFont (fontOf (10.f, true));
    g.drawText ("GAIN REDUCTION", b.reduced (46.f, 14.f).removeFromBottom (14.f).removeFromLeft (200.f), juce::Justification::bottomLeft);
}

// ------------------------------------------------------------ master display
MasterDisplay::MasterDisplay (HumRigProcessor& p) : proc (p)
{
    addAndMakeVisible (resetBtn);
    resetBtn.setColour (juce::TextButton::textColourOffId, ink);
    resetBtn.onClick = [] { HumRigProcessor::bus().lufsResetGen.fetch_add (1); };
    startTimerHz (15);
}
void MasterDisplay::resized() { resetBtn.setBounds (getWidth() - 90, 12, 74, 26); }
void MasterDisplay::paint (Graphics& g)
{
    auto& b = HumRigProcessor::bus();
    auto r = getLocalBounds().toFloat();
    g.setColour (dark); g.fillRoundedRectangle (r, 14.f);
    auto num = [] (float v) { return v <= -99.f ? juce::String ("--") : juce::String (v, 1); };
    struct Cell { const char* label; float v; bool big; };
    const float mom = b.momentaryLufs.load(), st = b.shortTermLufs.load(), integ = b.integratedLufs.load();
    const Cell cells[] = { { "MOMENTARY", mom, false }, { "SHORT-TERM", st, false }, { "INTEGRATED  LUFS", integ, true },
                           { "PEAK  dB", b.outPeakDb.load(), false }, { "LIMITER  dB", b.limiterGrDb.load(), false } };
    auto area = r.reduced (18.f, 14.f);
    area.removeFromRight (90.f);
    const float cw[5] = { 0.18f, 0.18f, 0.28f, 0.18f, 0.18f };
    float x = area.getX();
    for (int i = 0; i < 5; ++i)
    {
        auto c = area.withX (x).withWidth (area.getWidth() * cw[i]);
        x += c.getWidth();
        g.setColour (Colour (0xff9fa3c0)); g.setFont (fontOf (10.f, true));
        g.drawText (cells[i].label, c.removeFromTop (16.f), juce::Justification::centredLeft);
        g.setColour (i == 2 ? juce::Colours::white : Colour (0xffd6d9f0));
        g.setFont (fontOf (cells[i].big ? 46.f : 30.f, true));
        g.drawText (num (cells[i].v), c.removeFromTop (cells[i].big ? 60.f : 44.f), juce::Justification::centredLeft);
    }
    // loudness bar -40 .. 0 LUFS with markers at -23 (broadcast) and -14 (streaming)
    auto bar = r.reduced (18.f, 0.f).removeFromBottom (44.f).removeFromTop (16.f);
    g.setColour (Colour (0xff2a2d44)); g.fillRoundedRectangle (bar, 8.f);
    auto xl = [&] (float lufs) { return bar.getX() + bar.getWidth() * std::clamp ((lufs + 40.f) / 40.f, 0.f, 1.f); };
    g.setGradientFill (juce::ColourGradient (accentA, bar.getX(), 0.f, accentB, bar.getRight(), 0.f, false));
    g.fillRoundedRectangle (bar.withRight (xl (std::max (mom, -40.f))), 8.f);
    g.setColour (juce::Colours::white); g.setFont (fontOf (9.5f, true));
    for (float m : { -23.f, -14.f })
    {
        g.drawVerticalLine ((int) xl (m), bar.getY() - 2.f, bar.getBottom() + 2.f);
        g.drawText (juce::String ((int) m), (int) xl (m) - 16, (int) bar.getBottom() + 3, 32, 12, juce::Justification::centred);
    }
}

// ------------------------------------------------------------ nav
PageNav::PageNav (juce::StringArray n) : names (std::move (n)) {}
void PageNav::setCurrent (int i) { cur = juce::jlimit (0, names.size() - 1, i); repaint(); }
void PageNav::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float arrowW = 34.f;
    for (int side = 0; side < 2; ++side)
    {
        auto a = side == 0 ? b.removeFromLeft (arrowW) : b.removeFromRight (arrowW);
        g.setColour (Colour (0xffe3e6f3)); g.fillRoundedRectangle (a.reduced (1.f), 9.f);
        juce::Path p;
        const auto c = a.getCentre();
        if (side == 0) p.addTriangle (c.x + 4.f, c.y - 6.f, c.x + 4.f, c.y + 6.f, c.x - 5.f, c.y);
        else           p.addTriangle (c.x - 4.f, c.y - 6.f, c.x - 4.f, c.y + 6.f, c.x + 5.f, c.y);
        g.setColour (ink); g.fillPath (p);
    }
    b.reduce (6.f, 0.f);
    const float w = b.getWidth() / (float) names.size();
    for (int i = 0; i < names.size(); ++i)
    {
        auto chip = juce::Rectangle<float> (b.getX() + w * (float) i, b.getY(), w, b.getHeight()).reduced (2.f);
        const bool on = i == cur;
        if (on)
        {
            g.setGradientFill (juce::ColourGradient (accentA, chip.getX(), chip.getY(), accentB, chip.getRight(), chip.getBottom(), false));
            g.fillRoundedRectangle (chip, chip.getHeight() / 2.f);
        }
        g.setColour (on ? juce::Colours::white : ink);
        g.setFont (fontOf (12.f, true));
        g.drawText (names[i], chip, juce::Justification::centred);
    }
}
void PageNav::mouseUp (const juce::MouseEvent& e)
{
    const auto p = e.position;
    int target = cur;
    if (p.x < 34.f) target = cur - 1;
    else if (p.x > (float) getWidth() - 34.f) target = cur + 1;
    else target = juce::jlimit (0, names.size() - 1, (int) ((p.x - 40.f) / (((float) getWidth() - 80.f) / (float) names.size())));
    target = (target + names.size()) % names.size();
    if (target != cur) { setCurrent (target); if (onSelect) onSelect (target); }
}

// ------------------------------------------------------------ the pages
std::vector<std::pair<juce::String, std::unique_ptr<juce::Component>>> createPages (HumRigProcessor& p)
{
    using K = PageItem;
    auto knob = [] (const char* id, const char* t) { return PageItem { K::Knob, id, t }; };
    auto tog  = [] (const char* id, const char* t) { return PageItem { K::Toggle, id, t }; };
    auto sel  = [] (const char* id, const char* t) { return PageItem { K::Choice, id, t }; };
    std::vector<std::pair<juce::String, std::unique_ptr<juce::Component>>> pages;
    auto add = [&] (const char* name, std::unique_ptr<juce::Component> c) { pages.emplace_back (name, std::move (c)); };

    add ("Clean", std::make_unique<ControlPage> (p, std::vector<PageGroup> {
        { "INPUT", { knob (ID::inGain, "Input trim") }, 0.8f },
        { "LOW CUT", { knob (ID::eqHpfFreq, "Frequency"), sel (ID::eqHpfSlope, "Slope"), tog (ID::eqManual, "Manual freqs") }, 1.f },
        { "NOISE BETWEEN PHRASES (EXPANDER)", { knob (ID::expThr, "Threshold"), knob (ID::expRange, "Reduction"), knob (ID::expHold, "Hold"), knob (ID::expRel, "Release"), tog (ID::expOn, "Expander on") }, 2.4f } },
        nullptr, 0, "Tip: a gentle expander (about 10 dB) turns room noise down between words without cutting the voice. Switch it on, then raise the threshold until the noise just disappears in the gaps."));

    add ("EQ", std::make_unique<ControlPage> (p, std::vector<PageGroup> {
        { "LOW CUT", { knob (ID::eqHpfFreq, "Frequency"), sel (ID::eqHpfSlope, "Slope"), tog (ID::eqManual, "Manual freqs") }, 1.f },
        { "MUD", { knob (ID::eqMudFreq, "Freq"), knob (ID::eqMudGain, "Gain"), knob (ID::eqMudQ, "Q") }, 1.f },
        { "BOX", { knob (ID::eqBoxFreq, "Freq"), knob (ID::eqBoxGain, "Gain"), knob (ID::eqBoxQ, "Q") }, 1.f },
        { "WARMTH", { knob (ID::eqLowFreq, "Freq"), knob (ID::warmth, "Warmth") }, 1.f },
        { "PRESENCE", { knob (ID::eqPresFreq, "Freq"), knob (ID::eqPresQ, "Q"), knob (ID::presence, "Presence") }, 1.f },
        { "AIR", { knob (ID::eqAirFreq, "Freq"), knob (ID::air, "Air") }, 1.f },
        { "HIGH CUT", { knob (ID::eqLpfFreq, "Frequency"), tog (ID::eqLpfOn, "On") }, 1.f } },
        std::make_unique<EqCurveDisplay> (p), 168));

    add ("Compress", std::make_unique<ControlPage> (p, std::vector<PageGroup> {
        { "AMOUNT", { knob (ID::comp, "Comp amount") }, 1.f },
        { "TIMING", { knob (ID::compAttack, "Attack"), knob (ID::compRelease, "Release"), knob (ID::compKnee, "Knee") }, 1.6f },
        { "LEVEL", { knob (ID::compRatio, "Ratio"), knob (ID::compThr, "Threshold"), knob (ID::compMk, "Make-up"), sel (ID::compDet, "Detector") }, 1.6f },
        { "SIDE-CHAIN & PARALLEL", { knob (ID::compSc, "Side-chain HPF"), knob (ID::compMix, "Mix") }, 1.2f } },
        std::make_unique<GrHistoryDisplay> (p), 150, "Ratio 'Auto' follows the Comp amount. Aim for about 3-6 dB of gain reduction on the loudest words."));

    add ("De-ess", std::make_unique<ControlPage> (p, std::vector<PageGroup> {
        { "DE-ESSER", { knob (ID::deess, "Amount"), knob (ID::dsFreq, "Frequency"), knob (ID::dsRange, "Max reduction"), knob (ID::dsThr, "Threshold"),
                        sel (ID::dsMode, "Mode"), tog (ID::dsListen, "Listen to the band") }, 1.f } },
        nullptr, 0, "Press 'Listen to the band', sweep Frequency until the harsh 'S' sounds are loudest, then switch Listen off. Humming has little sibilance; keep the amount low."));

    add ("Color", std::make_unique<ControlPage> (p, std::vector<PageGroup> {
        { "SATURATION", { knob (ID::drive, "Drive"), knob (ID::colTone, "Tone"), knob (ID::colMix, "Mix"), sel (ID::colType, "Type") }, 1.4f },
        { "EXCITER (AIR)", { knob (ID::excFreq, "Frequency"), knob (ID::excAmt, "Amount") }, 1.f } },
        nullptr, 0, "Soft = clean warmth, Tube = fuller (even harmonics), Tape = rounded highs. Use Mix to blend the colour in gently."));

    add ("Space", std::make_unique<ControlPage> (p, std::vector<PageGroup> {
        { "REVERB", { knob (ID::revMix, "Mix"), knob (ID::revSize, "Size"), knob (ID::revDecay, "Decay"), knob (ID::revPre, "Pre-delay"),
                      knob (ID::revLo, "Low cut"), knob (ID::revHi, "High cut"), tog (ID::revFilt, "Use the cuts") }, 1.5f },
        { "DELAY", { knob (ID::dlyMix, "Mix"), knob (ID::dlyFb, "Feedback"), knob (ID::dlyFree, "Free time"), knob (ID::dlyHpf, "Low cut"), knob (ID::dlyLpf, "High cut"),
                     sel (ID::dlyTimeEx, "Note"), tog (ID::dlyFilt, "Use the cuts"), tog (ID::dlyPing, "Ping-pong") }, 1.6f },
        { "DUCKING", { knob (ID::duck, "Amount") }, 0.7f } },
        nullptr, 0, "Ducking turns reverb and delay down while the voice is singing, so the voice stays in front. 'Note' = As selected uses the 1/4, 1/8, 1/16 switch on the Home page."));

    add ("Stereo", std::make_unique<ControlPage> (p, std::vector<PageGroup> {
        { "DOUBLER", { knob (ID::dblMix, "Mix"), knob (ID::dblDelay, "Delay"), knob (ID::dblDetune, "Detune"), knob (ID::dblHpf, "Low cut") }, 2.f },
        { "WIDTH", { knob (ID::width, "Width"), tog (ID::monoCheck, "Mono check") }, 1.f } },
        nullptr, 0, "The doubler adds two slightly different copies of the voice left and right. Press 'Mono check' to hear how it sounds on a phone speaker."));

    add ("Master", std::make_unique<ControlPage> (p, std::vector<PageGroup> {
        { "LIMITER", { knob (ID::limGain, "Push"), knob (ID::limCeil, "Ceiling"), knob (ID::limRelease, "Release"), tog (ID::limOn, "Limiter on") }, 1.6f },
        { "OUTPUT", { knob (ID::outGain, "Output"), knob (ID::mix, "Dry/Wet") }, 1.f } },
        std::make_unique<MasterDisplay> (p), 168, "The limiter stops the voice from ever going over the ceiling (-1 dB is a safe choice). Streaming services aim near -14 LUFS for a full song."));
    return pages;
}
} // namespace humrig
