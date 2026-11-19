// Headless test harness for HumRig: DSP units + the real AudioProcessor end-to-end.
// Run: ./HumRigTests [outdir]   (writes WAV/PNG debug files to outdir, default ./test-out)
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "Editor/PluginEditor.h"
#include "DSP/PitchDetector.h"
#include "DSP/Scale.h"
#include "DSP/ListeningAnalyzer.h"
#include "DSP/Limiter.h"
#include "DSP/Loudness.h"

using namespace humrig;

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { ++checks; if (! (cond)) { ++failures; std::cout << "  FAIL: " << msg << "   [" #cond "]\n"; } else { std::cout << "  ok:   " << msg << "\n"; } } while (0)

static juce::File outDir;
static double centsBetween (double f, double ref) { return 1200.0 * std::log2 (f / ref); }
static double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

// ---------------------------------------------------------------- signals
static std::vector<float> makeHum (double sr, double seconds, const std::function<double (double)>& freqAt, float noiseDb = -200.f, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> nd (0.f, 1.f);
    const int n = (int) (seconds * sr);
    std::vector<float> x ((size_t) n);
    double phase = 0.0;
    const float noiseAmp = std::pow (10.f, noiseDb / 20.f);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        phase += 2.0 * juce::MathConstants<double>::pi * freqAt (t) / sr;
        // a "hum"-like tone: strong fundamental, few decaying harmonics, soft fade in/out
        double s = 0.0;
        for (int h = 1; h <= 6; ++h) s += std::sin (h * phase) / (double) (h * h * 0.7 + 0.3);
        const double env = std::min ({ 1.0, t / 0.05, (seconds - t) / 0.05 });
        x[(size_t) i] = (float) (0.25 * s * env) + noiseAmp * nd (rng);
    }
    return x;
}

// Pitch track of a signal (midi per hop, -1 = unvoiced) using the same YIN, 2x decimated.
static std::vector<float> trackPitch (const std::vector<float>& x, double sr, float minHz = 70.f, float maxHz = 700.f, int hop = 480)
{
    const int dec = std::max (1, (int) std::lround (sr / 20000.0));
    std::vector<float> d (x.size() / (size_t) dec);
    for (size_t i = 0; i < d.size(); ++i)
    {
        float a = 0.f;
        for (int k = 0; k < dec; ++k) a += x[i * (size_t) dec + (size_t) k];
        d[i] = a / (float) dec;
    }
    PitchDetector det; det.prepare (sr / dec);
    std::vector<float> out;
    const int dh = hop / dec;
    for (size_t pos = 0; pos + PitchDetector::windowSize <= d.size(); pos += (size_t) dh)
    {
        auto r = det.detect (d.data() + pos, minHz, maxHz);
        out.push_back (r.clarity > 0.85f ? 69.f + 12.f * std::log2 (r.freqHz / 440.f) : -1.f);
    }
    return out;
}

static void writeWav (const std::vector<float>& x, double sr, const juce::String& name)
{
    outDir.createDirectory();
    auto f = outDir.getChildFile (name);
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    if (os == nullptr) return;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 1, 24, {}, 0));
    if (w == nullptr) return;
    os.release();
    juce::AudioBuffer<float> b (1, (int) x.size());
    b.copyFrom (0, 0, x.data(), (int) x.size());
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// ---------------------------------------------------------------- processor helpers
static void setParam (HumRigProcessor& p, const char* id, float value)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

// "Clean" setup: only pitch correction active, everything else neutral/off.
static void cleanSetup (HumRigProcessor& p, bool tune, int key = 0, int scale = 1, float speed = 100.f, float flex = 0.f)
{
    setParam (p, ID::tuneOn, tune ? 1.f : 0.f);
    setParam (p, ID::key, (float) key); setParam (p, ID::scale, (float) scale);
    setParam (p, ID::speed, speed); setParam (p, ID::flex, flex); setParam (p, ID::vibrato, 0.f);
    setParam (p, ID::warmth, 50.f); setParam (p, ID::presence, 50.f); setParam (p, ID::air, 50.f);
    setParam (p, ID::comp, 0.f); setParam (p, ID::deess, 0.f); setParam (p, ID::drive, 0.f);
    setParam (p, ID::revMix, kMixOffDb); setParam (p, ID::dlyMix, kMixOffDb);
    setParam (p, ID::inGain, 0.f); setParam (p, ID::outGain, 0.f); setParam (p, ID::mix, 100.f);
    setParam (p, ID::voice, 0.f);   // male
}

static bool setLayout (HumRigProcessor& p, int inCh, int outCh)
{
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (inCh == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    l.outputBuses.add (outCh == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    return p.setBusesLayout (l);
}

// Run a mono signal through the processor; returns output channel 0.
static std::vector<float> runProcessor (HumRigProcessor& p, const std::vector<float>& in, double sr, int block, int inCh = 1, int outCh = 1,
                                        bool* sawNonFinite = nullptr)
{
    setLayout (p, inCh, outCh);
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);
    std::vector<float> out (in.size());
    juce::MidiBuffer midi;
    const int chs = std::max (inCh, outCh);
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min ((size_t) block, in.size() - pos);
        juce::AudioBuffer<float> buf (chs, n);
        for (int c = 0; c < chs; ++c)
            if (c < inCh) buf.copyFrom (c, 0, in.data() + pos, n); else buf.clear (c, 0, n);
        p.processBlock (buf, midi);
        for (int i = 0; i < n; ++i)
        {
            const float v = buf.getSample (0, i);
            if (! std::isfinite (v) && sawNonFinite != nullptr) *sawNonFinite = true;
            out[pos + (size_t) i] = v;
        }
    }
    return out;
}

static int bestLag (const std::vector<float>& a, const std::vector<float>& b, int lo, int hi, size_t from, size_t len)
{
    double best = -1e30; int bl = 0;
    for (int lag = lo; lag <= hi; ++lag)
    {
        double s = 0.0;
        for (size_t i = from; i < from + len; ++i)
            s += (double) a[i] * (double) b[i + (size_t) lag];
        if (s > best) { best = s; bl = lag; }
    }
    return bl;
}

// ---------------------------------------------------------------- tests
static void testScale()
{
    std::cout << "[scale snapping]\n";
    const auto maj = scaleMask (1);
    CHECK (nearestInScale (69.f, 0, maj) == 69, "A is in C major");
    CHECK (nearestInScale (70.4f, 0, maj) == 71, "A#+40ct snaps up to B in C major");
    CHECK (nearestInScale (61.2f, 0, maj) == 60 || nearestInScale (61.2f, 0, maj) == 62, "C#-ish snaps to C or D");
    CHECK (nearestInScale (66.0f, 9, scaleMask (2)) == 65 || nearestInScale (66.0f, 9, scaleMask (2)) == 67, "A minor: F# is not in the scale, snaps to F or G");
    CHECK (nearestInScale (71.0f, 9, scaleMask (2)) == 71, "A minor: B is in the scale");
    CHECK (nearestInScale (63.3f, 0, scaleMask (0)) == 63, "chromatic: nearest semitone");
}

static void testDetector()
{
    std::cout << "[pitch detector on sines]\n";
    const double sr = 24000.0;
    PitchDetector det; det.prepare (sr);
    double worst = 0.0;
    for (double f : { 80.0, 110.0, 146.83, 220.0, 329.63, 440.0, 587.33 })
    {
        std::vector<float> x (PitchDetector::windowSize);
        for (size_t i = 0; i < x.size(); ++i) x[i] = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * (double) i / sr);
        auto r = det.detect (x.data(), 65.f, 900.f);
        worst = std::max (worst, std::fabs (centsBetween (r.freqHz, f)));
    }
    CHECK (worst < 5.0, "worst error " << worst << " cents (< 5)");

    // noisy
    std::mt19937 rng (3); std::normal_distribution<float> nd (0.f, 1.f);
    double worstN = 0.0;
    for (double f : { 130.81, 196.0, 261.63, 392.0 })
    {
        std::vector<float> x (PitchDetector::windowSize);
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * (double) i / sr) + 0.02f * nd (rng); // ~ -23 dB noise
        auto r = det.detect (x.data(), 65.f, 900.f);
        worstN = std::max (worstN, std::fabs (centsBetween (r.freqHz, f)));
    }
    CHECK (worstN < 15.0, "with noise: worst error " << worstN << " cents (< 15)");
}

static void testLayouts()
{
    std::cout << "[bus layouts]\n";
    HumRigProcessor p;
    auto mk = [] (int i, int o)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (i == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        l.outputBuses.add (o == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        return l;
    };
    CHECK (p.checkBusesLayoutSupported (mk (1, 1)), "mono -> mono supported");
    CHECK (p.checkBusesLayoutSupported (mk (2, 2)), "stereo -> stereo supported");
    CHECK (p.checkBusesLayoutSupported (mk (1, 2)), "mono -> stereo supported");

    const double sr = 48000;
    auto hum = makeHum (sr, 1.0, [] (double) { return 220.0; });
    for (auto [i, o] : { std::pair { 1, 1 }, { 2, 2 }, { 1, 2 } })
    {
        HumRigProcessor q; cleanSetup (q, true);
        bool bad = false;
        auto y = runProcessor (q, hum, sr, 512, i, o, &bad);
        float peak = 0.f; for (float v : y) peak = std::max (peak, std::fabs (v));
        CHECK (! bad && peak > 0.01f && peak < 2.f, "layout " << i << "->" << o << " processes (peak " << peak << ")");
    }
}

static void testPitchCorrection (double sr)
{
    std::cout << "[end-to-end pitch correction @ " << (int) sr << " Hz]\n";
    // steady hum, 40 cents flat of A3, light noise: expect ~220 Hz
    for (double offsetCents : { -40.0, 35.0 })
    {
        const double target = 220.0, f = target * std::pow (2.0, offsetCents / 1200.0);
        auto in = makeHum (sr, 4.0, [f] (double) { return f; }, -38.f);
        HumRigProcessor p; cleanSetup (p, true);
        auto out = runProcessor (p, in, sr, 256);
        writeWav (out, sr, "steady_" + juce::String ((int) offsetCents) + ".wav");
        auto tr = trackPitch (out, sr);
        // evaluate the second half
        double worst = 0.0; int voiced = 0, total = 0;
        for (size_t i = tr.size() / 2; i < tr.size(); ++i)
        {
            ++total;
            if (tr[i] < 0) continue;
            ++voiced;
            worst = std::max (worst, std::fabs (100.0 * (tr[i] - 57.0)));    // A3 = MIDI 57
        }
        CHECK (voiced > total * 0.9, "output stays voiced (" << voiced << "/" << total << ")");
        CHECK (worst < 12.0, "input " << offsetCents << " ct -> output within " << worst << " ct of A3 (< 12)");
    }

    // glide G3 -> C4 across 3 s: output should sit on C-major notes most of the time
    {
        auto freqAt = [] (double t) { return 196.0 * std::pow (261.63 / 196.0, std::clamp (t / 3.0, 0.0, 1.0)); };
        auto in = makeHum (sr, 3.4, freqAt, -38.f);
        HumRigProcessor p; cleanSetup (p, true);
        auto out = runProcessor (p, in, sr, 480);
        writeWav (out, sr, "glide_out.wav"); writeWav (in, sr, "glide_in.wav");
        auto trIn = trackPitch (in, sr), trOut = trackPitch (out, sr);
        const auto mask = scaleMask (1);
        int on = 0, voiced = 0, onIn = 0, voicedIn = 0;
        for (size_t i = 0; i < trOut.size(); ++i)
        {
            if (trOut[i] > 0) { ++voiced; if (std::fabs (trOut[i] - (float) nearestInScale (trOut[i], 0, mask)) < 0.20f) ++on; }
        }
        for (float m : trIn) if (m > 0) { ++voicedIn; if (std::fabs (m - (float) nearestInScale (m, 0, mask)) < 0.20f) ++onIn; }
        const double fracOut = voiced ? (double) on / voiced : 0, fracIn = voicedIn ? (double) onIn / voicedIn : 0;
        CHECK (fracOut > 0.75, "glide: " << (int) (fracOut * 100) << "% of output frames within 20 ct of a scale note (input was " << (int) (fracIn * 100) << "%)");
        CHECK (fracOut > fracIn + 0.25, "glide: correction clearly increases in-scale time");
    }

    // flex: in-tolerance wobble is left alone with high flex, fixed with flex 0
    {
        const double f = 220.0 * std::pow (2.0, 15.0 / 1200.0);
        auto in = makeHum (sr, 3.0, [f] (double) { return f; });
        HumRigProcessor a; cleanSetup (a, true, 0, 1, 100.f, 0.f);
        HumRigProcessor b; cleanSetup (b, true, 0, 1, 100.f, 100.f);
        auto oa = trackPitch (runProcessor (a, in, sr, 256), sr), ob = trackPitch (runProcessor (b, in, sr, 256), sr);
        auto med = [] (std::vector<float> v) { std::vector<float> w; for (float x : v) if (x > 0) w.push_back (x); if (w.empty()) return -1.f; std::sort (w.begin(), w.end()); return w[w.size() / 2]; };
        const double ca = 100.0 * (med (std::vector<float> (oa.begin() + (long) oa.size() / 2, oa.end())) - 57.0);
        const double cb = 100.0 * (med (std::vector<float> (ob.begin() + (long) ob.size() / 2, ob.end())) - 57.0);
        CHECK (std::fabs (ca) < 8.0, "flex 0: +15 ct corrected to " << ca << " ct");
        CHECK (std::fabs (cb - 15.0) < 8.0, "flex 100: +15 ct left near " << cb << " ct");
    }
}

static void testLatencyAndBypass (double sr)
{
    std::cout << "[latency @ " << (int) sr << " Hz]\n";
    // in-tune tone with a random (aperiodic) amplitude envelope => corrected path ~ delayed copy of the input,
    // and the cross-correlation peak is unique (a pure periodic tone would be ambiguous modulo the period)
    auto in = makeHum (sr, 3.0, [] (double) { return 220.0; });
    {
        std::mt19937 rng (11); std::uniform_real_distribution<float> ud (0.15f, 1.f);
        const size_t seg = (size_t) (sr * 0.037);
        float prev = 1.f, next = ud (rng);
        for (size_t i = 0; i < in.size(); ++i)
        {
            if (i % seg == 0) { prev = next; next = ud (rng); }
            const float t = (float) (i % seg) / (float) seg;
            in[i] *= prev + (next - prev) * t;
        }
    }
    for (bool tune : { true, false })
    {
        HumRigProcessor p; cleanSetup (p, tune);
        auto out = runProcessor (p, in, sr, 256);
        const int rep = p.getLatencySamples();
        // search only +-1/2 period around the reported latency: a periodic tone is ambiguous modulo its period.
        // (EQ high-pass phase shifts the carrier by a few samples, hence the tolerance; the impulse test is exact.)
        const int period = (int) (sr / 220.0);
        const int lag = bestLag (in, out, rep - period / 2, rep + period / 2, (size_t) sr, (size_t) (sr * 0.5));
        CHECK (std::abs (lag - rep) <= 25, "tune " << (tune ? "on " : "off") << ": measured delay " << lag << " vs reported " << rep << " samples (|diff| <= 25)");
    }
}

static void testImpulse (double sr)
{
    std::cout << "[impulse position @ " << (int) sr << " Hz]\n";
    std::vector<float> in ((size_t) (sr * 2.0), 0.f);
    in[20000] = 0.8f;
    for (bool tune : { false, true })
    {
        HumRigProcessor p; cleanSetup (p, tune);
        auto out = runProcessor (p, in, sr, 256);
        size_t pk = 0; float m = 0.f; double num = 0, den = 0;
        for (size_t i = 0; i < out.size(); ++i) { if (std::fabs (out[i]) > m) { m = std::fabs (out[i]); pk = i; } num += (double) i * out[i] * out[i]; den += (double) out[i] * out[i]; }
        const long centroid = (long) (num / den) - 20000;
        CHECK (std::labs ((long) pk - 20000 - p.getLatencySamples()) <= 2 && std::labs (centroid - p.getLatencySamples()) <= 8,
               "tune " << (tune ? "on " : "off") << ": impulse arrives at +" << (long) pk - 20000 << " (centroid +" << centroid << ") == reported latency " << p.getLatencySamples());
    }
}

static std::vector<float> makeMelody (double sr, double seconds, const std::vector<int>& notes, float amp = 1.f, float noiseDb = -200.f)
{
    auto x = makeHum (sr, seconds, [&notes] (double t) { return midiToHz (notes[(size_t) (t / 0.4) % notes.size()]); }, noiseDb);
    for (auto& v : x) v *= amp;
    return x;
}

static ListeningAnalyzer::Result analyse (const std::vector<float>& x, double sr)
{
    ListeningAnalyzer a; a.prepare (sr);
    for (size_t pos = 0; pos < x.size() && ! a.isDone(); pos += 256)
        a.process (x.data() + pos, (int) std::min<size_t> (256, x.size() - pos));
    return a.result();
}

static void testListening()
{
    std::cout << "[listening analyzer]\n";
    const double sr = 48000;
    const std::vector<int> aMinorMale = { 45, 48, 52, 48, 45, 43, 40, 45, 47, 48, 47, 45, 40, 45 };   // A2..E3 (male hum range)
    const std::vector<int> cMajorFemale = { 60, 64, 67, 64, 60, 62, 65, 64, 62, 60, 67, 64, 60, 64 };

    auto a = analyse (makeMelody (sr, 14.0, aMinorMale, 0.25f / 1.f), sr);
    CHECK (a.valid && a.key == 9 && a.minor, "male melody in A minor -> key " << a.key << (a.minor ? " minor" : " major") << " (want 9 minor)");
    CHECK (! a.female && a.medianF0 > 70.f && a.medianF0 < 165.f, "male register detected (median f0 " << a.medianF0 << " Hz)");

    auto c = analyse (makeMelody (sr, 14.0, cMajorFemale, 0.25f), sr);
    CHECK (c.valid && c.key == 0 && ! c.minor, "female melody in C major -> key " << c.key << (c.minor ? " minor" : " major") << " (want 0 major)");
    CHECK (c.female, "female register detected (median f0 " << c.medianF0 << " Hz)");

    auto quiet = analyse (makeMelody (sr, 14.0, aMinorMale, 0.01f), sr);
    auto loud  = analyse (makeMelody (sr, 14.0, aMinorMale, 0.9f), sr);
    CHECK (quiet.inGainDb > loud.inGainDb + 10.f, "gain suggestion: quiet " << quiet.inGainDb << " dB vs loud " << loud.inGainDb << " dB");

    // sibilance: add 7 kHz band noise to the same melody
    {
        std::mt19937 rng (4); std::normal_distribution<float> nd (0.f, 1.f);
        Biquad bp; bp.makeBandPass (sr, 7000.0, 1.5);
        auto base = makeMelody (sr, 14.0, aMinorMale, 0.25f);
        auto sibilant = base;
        for (auto& v : sibilant) v += 0.03f * bp.process (nd (rng));
        auto r0 = analyse (base, sr), r1 = analyse (sibilant, sr);
        CHECK (r1.valid && r1.deess > r0.deess + 10.f, "de-esser suggestion rises with sibilance (" << r0.deess << " -> " << r1.deess << ")");
    }
    // spectral balance: a dull (low-passed) take should want more Presence/Air than the original
    {
        auto base = makeMelody (sr, 14.0, aMinorMale, 0.25f, -45.f);
        auto dull = base;
        Biquad lp; lp.makeLowPass (sr, 1500.0, 0.707);
        for (auto& v : dull) v = lp.process (v);
        auto r0 = analyse (base, sr), r1 = analyse (dull, sr);
        CHECK (r1.presence > r0.presence || r1.air > r0.air, "dull take suggests more presence/air (presence " << r0.presence << " -> " << r1.presence << ", air " << r0.air << " -> " << r1.air << ")");
    }
    // noise between phrases -> expander suggestion; a take without pauses -> none
    {
        std::mt19937 rng (12); std::normal_distribution<float> nd (0.f, 1.f);
        auto melody = makeMelody (sr, 16.0, aMinorMale, 0.25f);
        auto gappy = melody;
        for (size_t i = 0; i < gappy.size(); ++i)
        {
            const bool gap = ((size_t) ((double) i / sr / 0.6) % 2) == 1;                 // every other 0.6 s is a pause
            if (gap) gappy[i] = 0.f;
            gappy[i] += 0.0008f * nd (rng);                                              // noise floor about -62 dBFS
        }
        auto g = analyse (gappy, sr);
        CHECK (g.valid && g.expOn, "pauses with a noise floor -> expander suggested (noise floor " << g.noiseFloorDb << " dB)");
        CHECK (g.expOn && g.expThr > g.noiseFloorDb + g.inGainDb && g.expThr < -30.f, "suggested expander threshold " << g.expThr << " dB sits above the noise and below the voice");
        auto cont = melody;
        for (auto& v : cont) v += 0.0008f * nd (rng);
        auto cr = analyse (cont, sr);
        CHECK (cr.valid && ! cr.expOn, "a take without pauses does not get an expander");
        CHECK (a.mudGain <= 0.f && a.mudGain >= -4.f, "mud cut suggestion is within 0..-4 dB (" << a.mudGain << ")");
        CHECK (c.dsFreq == 7000.f && a.dsFreq == 5500.f, "de-esser frequency follows the voice type");
    }
    CHECK (analyse (std::vector<float> ((size_t) (sr * 3), 0.f), sr).valid == false, "silence produces no result");
}

static void testBusAndListeningE2E()
{
    std::cout << "[shared bus: editor instance vs. processing instance]\n";
    const double sr = 48000;
    HumRigProcessor ui;          // the instance that owns the editor / buttons
    HumRigProcessor engine;      // the instance the host actually feeds audio to
    cleanSetup (engine, true); cleanSetup (ui, true);
    setLayout (engine, 1, 1); engine.setRateAndBufferSizeDetails (sr, 256); engine.prepareToPlay (sr, 256);
    setLayout (ui, 1, 1); ui.setRateAndBufferSizeDetails (sr, 256); ui.prepareToPlay (sr, 256);

    const int gen = ui.requestListening();
    auto melody = makeMelody (sr, 16.0, { 45, 48, 52, 48, 45, 43, 40, 45, 47, 48, 47, 45, 40, 45 }, 0.25f);
    juce::MidiBuffer midi;
    float lastProgress = 0.f; bool progressed = false;
    float uiInPeak = 0.f, uiGr = 0.f;
    for (size_t pos = 0; pos + 256 <= melody.size() && HumRigProcessor::bus().listenState.load() == SharedBus::listening; pos += 256)
    {
        juce::AudioBuffer<float> b (1, 256); b.copyFrom (0, 0, melody.data() + pos, 256);
        engine.processBlock (b, midi);
        const float p = HumRigProcessor::bus().listenProgress.load();
        if (p > lastProgress + 0.05f) { progressed = true; lastProgress = p; }
        const auto m = ui.getMeters();                  // UI instance never processed a sample
        uiInPeak = std::max (uiInPeak, m.inPeak); uiGr = std::min (uiGr, m.grDb);
    }
    CHECK (HumRigProcessor::bus().listenState.load() == SharedBus::done, "listening finished on the processing instance");
    CHECK (progressed, "progress advanced while listening (reached " << lastProgress << ")");
    CHECK (ui.getMeters().inPeak >= 0.f && uiInPeak > 0.05f, "UI instance sees the other instance's input level (" << uiInPeak << ") although it processes nothing");
    CHECK (ui.applyListeningResults (gen), "UI instance applied the result");
    CHECK ((int) ui.apvts.getRawParameterValue (ID::key)->load() == 9 && (int) ui.apvts.getRawParameterValue (ID::scale)->load() == 2,
           "parameters set: key A, scale Minor");
    CHECK ((int) ui.apvts.getRawParameterValue (ID::voice)->load() == 0, "parameters set: voice Male");
    CHECK (! ui.applyListeningResults (gen + 7), "stale generation is not applied");
}

static void testRobustness (double sr)
{
    std::cout << "[robustness @ " << (int) sr << " Hz]\n";
    std::mt19937 rng (7); std::normal_distribution<float> nd (0.f, 1.f);
    std::vector<float> noise ((size_t) (sr * 2.0));
    for (auto& v : noise) v = 0.5f * nd (rng);
    auto hum = makeHum (sr, 2.0, [] (double t) { return 150.0 + 80.0 * std::sin (t * 3.0); });
    for (int block : { 1, 7, 64, 333, 1024, 2048 })
    {
        HumRigProcessor p;
        bool bad = false;
        std::vector<float> in (hum.begin(), hum.begin() + (long) std::min<size_t> (hum.size(), (size_t) sr));
        auto y = runProcessor (p, in, sr, block, 1, 2, &bad);       // full default chain, incl. reverb/delay
        float peak = 0.f; for (float v : y) peak = std::max (peak, std::fabs (v));
        CHECK (! bad && peak < 4.f, "block " << block << ": finite, peak " << peak);
    }
    {   // noise + silence: must not blow up
        HumRigProcessor p; bool bad = false;
        auto y = runProcessor (p, noise, sr, 512, 1, 1, &bad);
        float peak = 0.f; for (float v : y) peak = std::max (peak, std::fabs (v));
        CHECK (! bad && peak < 8.f, "white noise input stays finite (peak " << peak << ")");
        std::vector<float> silence ((size_t) sr, 0.f);
        HumRigProcessor q; bool bad2 = false;
        auto z = runProcessor (q, silence, sr, 512, 1, 1, &bad2);
        float pz = 0.f; for (float v : z) pz = std::max (pz, std::fabs (v));
        CHECK (! bad2 && pz < 1e-3f, "silence in -> silence out (peak " << pz << ")");
    }
    {   // clicks: block-boundary discontinuities in a steady tone must not exceed in-signal slope
        auto in = makeHum (sr, 3.0, [] (double) { return 223.0; });
        HumRigProcessor p; cleanSetup (p, true);
        auto y = runProcessor (p, in, sr, 128);
        float maxStep = 0.f, refStep = 0.f;
        for (size_t i = (size_t) (sr * 1.0) + 1; i < y.size(); ++i) maxStep = std::max (maxStep, std::fabs (y[i] - y[i - 1]));
        for (size_t i = (size_t) (sr * 1.0) + 1; i < in.size(); ++i) refStep = std::max (refStep, std::fabs (in[i] - in[i - 1]));
        CHECK (maxStep < refStep * 2.0f, "no clicks: max sample step " << maxStep << " vs input " << refStep);
    }
    {   // tune toggling at runtime shouldn't click
        auto in = makeHum (sr, 3.0, [] (double) { return 223.0; });
        HumRigProcessor p; cleanSetup (p, true);
        setLayout (p, 1, 1); p.setRateAndBufferSizeDetails (sr, 256); p.prepareToPlay (sr, 256);
        juce::MidiBuffer midi; std::vector<float> y (in.size());
        for (size_t pos = 0, blk = 0; pos + 256 <= in.size(); pos += 256, ++blk)
        {
            if (blk % 40 == 20) setParam (p, ID::tuneOn, (blk / 40) % 2 == 0 ? 0.f : 1.f);
            juce::AudioBuffer<float> b (1, 256); b.copyFrom (0, 0, in.data() + pos, 256);
            p.processBlock (b, midi);
            for (int i = 0; i < 256; ++i) y[pos + (size_t) i] = b.getSample (0, i);
        }
        float maxStep = 0.f, refStep = 0.f;
        const size_t filled = (in.size() / 256) * 256;     // y beyond this was never written
        for (size_t i = (size_t) (sr * 0.5); i < filled; ++i) maxStep = std::max (maxStep, std::fabs (y[i] - y[i - 1]));
        for (size_t i = (size_t) (sr * 0.5); i < filled; ++i) refStep = std::max (refStep, std::fabs (in[i] - in[i - 1]));
        writeWav (y, sr, "toggle.wav");
        CHECK (maxStep < refStep * 2.5f, "toggling tune on/off: no click (step " << maxStep << " vs " << refStep << ")");
    }
}

static void testChain (double sr)
{
    std::cout << "[mix chain]\n";
    auto in = makeHum (sr, 2.0, [] (double) { return 220.0; });
    auto rms = [] (const std::vector<float>& v, size_t from)
    { double s = 0; for (size_t i = from; i < v.size(); ++i) s += (double) v[i] * v[i]; return std::sqrt (s / (double) (v.size() - from)); };

    // compressor reduces dynamics and reports GR
    {
        std::vector<float> loud = in; for (auto& v : loud) v *= 3.f;
        HumRigProcessor p; cleanSetup (p, false); setParam (p, ID::comp, 80.f);
        auto y = runProcessor (p, loud, sr, 256);
        CHECK (p.grDbMeter.load() < -1.f, "compressor reports gain reduction " << p.grDbMeter.load() << " dB");
        (void) y;
    }
    // EQ: air boost raises HF, cut lowers it
    {
        std::mt19937 nr (5); std::normal_distribution<float> nd (0.f, 0.2f);
        std::vector<float> hf ((size_t) (sr * 1.5)); for (auto& v : hf) v = nd (nr);   // broadband
        HumRigProcessor up; cleanSetup (up, false); setParam (up, ID::air, 100.f);
        HumRigProcessor dn; cleanSetup (dn, false); setParam (dn, ID::air, 0.f);
        const double ru = rms (runProcessor (up, hf, sr, 256), (size_t) sr), rd = rms (runProcessor (dn, hf, sr, 256), (size_t) sr);
        CHECK (ru > rd * 1.3, "air 100 louder than air 0 on broadband signal (" << ru << " vs " << rd << ")");
    }
    // reverb + delay add energy after the input stops
    {
        std::vector<float> burst ((size_t) (sr * 3.0), 0.f);
        for (size_t i = 0; i < (size_t) (sr * 0.5); ++i) burst[i] = in[i];
        HumRigProcessor p; cleanSetup (p, false);
        setParam (p, ID::revMix, -12.f); setParam (p, ID::revDecay, 3.0f); setParam (p, ID::dlyMix, -12.f);
        auto y = runProcessor (p, burst, sr, 256, 1, 2);
        const double tail = rms (std::vector<float> (y.begin() + (long) (sr * 1.2), y.begin() + (long) (sr * 2.0)), 0);
        CHECK (tail > 1e-4, "reverb/delay tail present after input stops (rms " << tail << ")");
    }
    // dry/wet 0 => latency-aligned dry passthrough
    {
        HumRigProcessor p; cleanSetup (p, true); setParam (p, ID::mix, 0.f);
        auto y = runProcessor (p, in, sr, 256);
        const int lat = p.getLatencySamples();
        double err = 0.0; size_t cnt = 0;
        for (size_t i = (size_t) lat + 1000; i < in.size(); ++i) { err = std::max (err, (double) std::fabs (y[i] - in[i - (size_t) lat])); ++cnt; }
        CHECK (err < 1e-4, "Dry/Wet = 0 is an exact latency-aligned dry copy (max err " << err << ")");
    }
}

static void testReprepare()
{
    std::cout << "[re-prepare / odd host behaviour (auval-style)]\n";
    HumRigProcessor p; cleanSetup (p, true);
    juce::MidiBuffer midi;
    bool allOk = true; int runs = 0, bad = 0; std::string firstBad;
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0, 22050.0, 11025.0, 8000.0, 88200.0 })
    {
        auto hum = makeHum (sr, 0.6, [] (double t) { return 160.0 + 60.0 * t; }, -40.f);
        for (auto [ic, oc] : { std::pair { 2, 2 }, { 1, 1 }, { 1, 2 }, { 2, 1 }, { 2, 2 } })
            for (int declared : { 1, 64, 512, 1156, 4096 })
                for (int actualMul : { 1, 3 })              // host may exceed its declared max block
                {
                    if (! setLayout (p, ic, oc)) continue;
                    p.setRateAndBufferSizeDetails (sr, declared);
                    p.prepareToPlay (sr, declared);
                    const int actual = std::min (declared * actualMul, 8192);
                    const int chs = std::max (ic, oc);
                    for (size_t pos = 0; pos < hum.size(); pos += (size_t) actual)
                    {
                        const int n = (int) std::min ((size_t) actual, hum.size() - pos);
                        juce::AudioBuffer<float> buf (chs, n);
                        for (int c = 0; c < chs; ++c) { if (c < ic) buf.copyFrom (c, 0, hum.data() + pos, n); else buf.clear (c, 0, n); }
                        p.processBlock (buf, midi);
                        for (int c = 0; c < chs; ++c)
                            for (int i = 0; i < n; ++i)
                            {
                                const float v = buf.getSample (c, i);
                                if (! std::isfinite (v) || std::fabs (v) > 8.f)
                                {
                                    allOk = false; ++bad;
                                    if (firstBad.empty())
                                        firstBad = "sr=" + std::to_string ((int) sr) + " layout=" + std::to_string (ic) + "->" + std::to_string (oc)
                                                 + " declared=" + std::to_string (declared) + " actual=" + std::to_string (actual)
                                                 + " ch=" + std::to_string (c) + " sample=" + std::to_string (pos + (size_t) i) + " value=" + std::to_string (v);
                                }
                            }
                    }
                    ++runs;
                }
    }
    if (! firstBad.empty()) std::cout << "  first offending sample: " << firstBad << "  (" << bad << " bad samples)\n";
    CHECK (allOk, "survived " << runs << " prepare/process combinations (rates 8k-192k, blocks 1-8192, all layouts), output finite");
}

static void testPresetsAndState()
{
    std::cout << "[presets & state]\n";
    HumRigProcessor p;
    CHECK (p.getNumPrograms() == 39, "has " << p.getNumPrograms() << " presets (9 general + 25 artist + 5 pro chains)");
    {
        std::map<std::string, int> perGenre;
        for (int i = 0; i < getNumPresets(); ++i) ++perGenre[getPreset (i).genre];
        CHECK (perGenre["Pop"] == 5 && perGenre["Rap"] == 5 && perGenre["Rock"] == 5 && perGenre["Classic Rock"] == 5 && perGenre["Metal"] == 5,
               "5 artist presets in each of Pop / Rap / Rock / Classic Rock / Metal");
        bool inRange = true;
        for (int i = 0; i < getNumPresets(); ++i)
        {
            const auto& q = getPreset (i);
            for (float v : { q.warmth, q.presence, q.air, q.comp, q.deess, q.drive, q.dlyFb, q.speed, q.vibrato, q.flex }) if (v < 0.f || v > 100.f) inRange = false;
            if (q.dlyTime < 0 || q.dlyTime > 2 || q.revDecay < 0.2f || q.revDecay > 8.f || q.revMix < kMixOffDb || q.revMix > 0.f) inRange = false;
        }
        CHECK (inRange, "every preset value is inside its parameter range");
    }
    {   // pro chains: every extra refers to a real parameter and is inside its range; presets apply and reset cleanly
        bool ok = true; int pro = 0;
        for (int i = 0; i < getNumPresets(); ++i)
        {
            const auto& pr = getPreset (i);
            if (std::string (pr.genre) == "Pro Chains") ++pro;
            for (int k = 0; k < pr.numExtras; ++k)
            {
                auto* prm = p.apvts.getParameter (pr.extras[k].id);
                if (prm == nullptr) { ok = false; std::cout << "  unknown extra id " << pr.extras[k].id << "\n"; continue; }
                const auto r = p.apvts.getParameterRange (pr.extras[k].id);
                if (pr.extras[k].value < r.start - 1e-4f || pr.extras[k].value > r.end + 1e-4f) { ok = false; std::cout << "  out of range: " << pr.extras[k].id << "\n"; }
            }
        }
        CHECK (ok && pro == 5, "5 pro-chain presets, all detailed values valid");
        int rescue = -1; for (int i = 0; i < getNumPresets(); ++i) if (std::string (getPreset (i).name) == "Phone Vocal Rescue") rescue = i;
        p.setCurrentProgram (rescue);
        CHECK (p.apvts.getRawParameterValue (ID::expOn)->load() > 0.5f && std::fabs (p.apvts.getRawParameterValue (ID::compRatio)->load() - 3.f) < 0.1f
               && p.apvts.getRawParameterValue (ID::limOn)->load() > 0.5f, "Phone Vocal Rescue sets expander, ratio and limiter");
        p.setCurrentProgram (0);
        CHECK (p.apvts.getRawParameterValue (ID::expOn)->load() < 0.5f && p.apvts.getRawParameterValue (ID::compRatio)->load() < 1.05f
               && p.apvts.getRawParameterValue (ID::eqMudGain)->load() == 0.f, "a classic preset puts the detailed controls back to their defaults");
        // every preset renders finite audio with a hot signal; pro chains with the limiter never exceed -1 dB
        auto hot = makeHum (48000.0, 1.0, [] (double) { return 180.0; }, -60.f, 3);
        for (auto& v : hot) v *= 3.f;
        bool finite = true, ceilingOk = true;
        for (int i = 0; i < getNumPresets(); ++i)
        {
            HumRigProcessor r; r.applyPreset (i);
            bool bad = false; auto y = runProcessor (r, hot, 48000.0, 480, 1, 2, &bad);
            if (bad) finite = false;
            if (std::string (getPreset (i).genre) == "Pro Chains")
            {
                float pk = 0.f; for (size_t k = 6000; k < y.size(); ++k) pk = std::max (pk, std::fabs (y[k]));
                if (pk > 0.8914f) { ceilingOk = false; std::cout << "  preset " << getPreset (i).name << " peak " << pk << "\n"; }
            }
        }
        CHECK (finite, "all presets render finite audio on a hot signal");
        CHECK (ceilingOk, "pro-chain presets keep the output under the -1 dB ceiling");
    }
    p.setCurrentProgram (4);
    CHECK (std::string (p.getProgramName (4).toRawUTF8()) == "K-Pop", "program 4 is K-Pop");
    const float warmth = p.apvts.getRawParameterValue (ID::warmth)->load();
    CHECK (std::fabs (warmth - getPreset (4).warmth) < 0.2f, "preset applied to parameters");
    setParam (p, ID::key, 7.f);
    juce::MemoryBlock mb; p.getStateInformation (mb);
    HumRigProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK ((int) q.apvts.getRawParameterValue (ID::key)->load() == 7, "state round-trips (key)");
    CHECK (q.getCurrentProgram() == 4, "state round-trips (preset index)");
}


// ---------------------------------------------------------------- golden regression (v0.2 sound must not change)
// Renders a few deterministic scenarios with the default/preset settings and compares them with tests/golden_v02.bin
// (recorded with the v0.2 code). New features default to "off / same as before", so the output must be identical,
// apart from a constant extra latency (limiter look-ahead) which is compensated here.
static constexpr int kGoldenLen = 24000;
static std::vector<std::vector<float>> renderGolden()
{
    std::vector<std::vector<float>> all;
    const double sr = 48000.0;
    auto sig = makeHum (sr, 1.2, [] (double t) { return 120.0 + 60.0 * t; }, -40.f, 21);
    struct Sc { int preset; int inCh, outCh; bool useDefaultsOnly; };
    const Sc scs[] = { { -1, 1, 1, true }, { 1, 2, 2, false }, { 5 + 9, 1, 2, false }, { 9 + 20, 1, 1, false } };   // default | Pop | Rap "Future" | Metal
    for (const auto& sc : scs)
    {
        HumRigProcessor p;
        if (sc.preset >= 0) p.applyPreset (sc.preset);
        setParam (p, ID::dsMode, 1.f);          // golden file was recorded with the v0.2 split-band de-esser
        auto out0 = runProcessor (p, sig, sr, 480, sc.inCh, sc.outCh);
        all.push_back (std::move (out0));
        if (sc.outCh == 2)
        {
            // runProcessor returns channel 0 only; render channel 1 with a second identical run
            HumRigProcessor q;
            if (sc.preset >= 0) q.applyPreset (sc.preset);
            setParam (q, ID::dsMode, 1.f);
            setLayout (q, sc.inCh, 2);
            q.setRateAndBufferSizeDetails (sr, 480); q.prepareToPlay (sr, 480);
            std::vector<float> r (sig.size());
            juce::MidiBuffer midi;
            for (size_t pos = 0; pos < sig.size(); pos += 480)
            {
                const int n = (int) std::min<size_t> (480, sig.size() - pos);
                juce::AudioBuffer<float> b (2, n);
                for (int c = 0; c < 2; ++c) { if (c < sc.inCh) b.copyFrom (c, 0, sig.data() + pos, n); else b.clear (c, 0, n); }
                q.processBlock (b, midi);
                for (int i = 0; i < n; ++i) r[pos + (size_t) i] = b.getSample (1, i);
            }
            all.push_back (std::move (r));
        }
    }
    return all;
}

static void testGolden (int extraLatency)
{
    std::cout << "[golden regression vs v0.2]\n";
   #if ! JUCE_LINUX
    std::cout << "  (golden file was recorded on Linux; the pitch engine's FFT differs slightly per platform, so this check runs on Linux only)\n";
    (void) extraLatency; return;
   #endif
    auto cur = renderGolden();
    const char* wr = std::getenv ("HUMRIG_WRITE_GOLDEN");
    if (wr != nullptr)
    {
        juce::File f (wr); f.deleteFile();
        juce::FileOutputStream os (f);
        for (auto& ch : cur)
            for (int i = 0; i < kGoldenLen; ++i)
            {
                const int q = juce::jlimit (-32768, 32767, (int) std::lround (ch[(size_t) (i + extraLatency + 4800)] * 2048.f));
                os.writeShort ((short) q);
            }
        std::cout << "  wrote golden file " << f.getFullPathName() << "\n";
        return;
    }
    auto f = juce::File::getCurrentWorkingDirectory().getChildFile ("tests/golden_v02.bin");
    if (! f.existsAsFile()) { std::cout << "  (golden file not found, skipped)\n"; return; }
    juce::FileInputStream is (f);
    CHECK ((size_t) f.getSize() == cur.size() * (size_t) kGoldenLen * 2, "golden file has the expected size");
    for (size_t c = 0; c < cur.size(); ++c)
    {
        float worst = 0.f; int worstAt = 0;
        for (int i = 0; i < kGoldenLen; ++i)
        {
            const float g = (float) is.readShort() / 2048.f;
            const float v = cur[c][(size_t) (i + extraLatency + 4800)];
            const float d = std::fabs (v - g);
            if (d > worst) { worst = d; worstAt = i; }
        }
        CHECK (worst < 0.002f, "scenario channel " << c << " matches v0.2 (max diff " << worst << " at " << worstAt << ")");
    }
}

// ---------------------------------------------------------------- v0.3 pro modules
static std::vector<float> makeSine (double sr, double seconds, double hz, float amp)
{
    std::vector<float> x ((size_t) (seconds * sr));
    for (size_t i = 0; i < x.size(); ++i) x[i] = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) i / sr);
    return x;
}
static double rmsOf (const std::vector<float>& v, size_t from, size_t to = 0)
{
    if (to == 0 || to > v.size()) to = v.size();
    double s = 0; for (size_t i = from; i < to; ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / (double) std::max<size_t> (1, to - from));
}
static double dbOf (double x) { return 20.0 * std::log10 (std::max (x, 1e-12)); }

static void testLimiterAndLoudness()
{
    std::cout << "[limiter + loudness]\n";
    const double sr = 48000.0;
    {   // DSP unit: hot signal is held under the ceiling; off = pure delay
        Limiter lim; lim.prepare (sr);
        lim.setParams (true, -1.f, 100.f, 6.f);
        auto in = makeSine (sr, 1.0, 440.0, 1.0f);
        float peak = 0.f; float ol, orr;
        for (size_t i = 0; i < in.size(); ++i) { lim.process (in[i], in[i], ol, orr); if (i > 2000) peak = std::max (peak, std::fabs (ol)); }
        CHECK (peak <= 0.8914f, "limiter holds a +6 dB-over sine under the -1 dB ceiling (peak " << peak << ")");
        CHECK (peak > 0.80f, "limiter does not over-attenuate (peak " << peak << ")");

        Limiter off; off.prepare (sr);
        const int D = off.lookahead();
        bool exact = true;
        for (size_t i = 0; i < 4000; ++i) { float a, b; off.process (in[i], in[i], a, b); if (i >= (size_t) D && a != in[i - (size_t) D]) exact = false; }
        CHECK (exact, "limiter off = exact delay of " << D << " samples (constant latency)");

        // ISP: two samples of 0.9 at fs/4 phase offset give a ~1.27 true peak; the limiter must react to it
        Limiter isp; isp.prepare (sr); isp.setParams (true, -1.f, 100.f, 0.f);
        float pk = 0.f;
        for (int i = 0; i < 20000; ++i) { const float x = 0.99f * (float) std::sin (2.0 * 3.14159265 * 12000.0 * i / sr + 0.785); isp.process (x, x, ol, orr); if (i > 1000) pk = std::max (pk, std::fabs (ol)); }
        CHECK (pk <= 0.8914f, "limiter also controls near-Nyquist content (peak " << pk << ")");
    }
    {   // through the processor
        HumRigProcessor p; cleanSetup (p, false);
        setParam (p, ID::limOn, 1.f); setParam (p, ID::limCeil, -3.f);
        auto in = makeSine (sr, 1.0, 330.0, 1.0f);
        auto y = runProcessor (p, in, sr, 256, 1, 2);
        float peak = 0.f; for (size_t i = 4000; i < y.size(); ++i) peak = std::max (peak, std::fabs (y[i]));
        CHECK (peak <= 0.7080f, "processor output never exceeds the -3 dB ceiling (peak " << peak << ")");
        HumRigProcessor q; cleanSetup (q, false); q.prepareToPlay (sr, 256);
        const int latOff = q.getLatencySamples();
        setParam (q, ID::limOn, 1.f); q.prepareToPlay (sr, 256);
        CHECK (latOff == q.getLatencySamples(), "reported latency is the same with the limiter on or off (" << latOff << ")");
    }
    {   // EBU: 1 kHz, -23 dBFS in both channels = -23 LUFS
        LoudnessMeter m; m.prepare (sr);
        const float a = std::pow (10.f, -23.f / 20.f);
        auto x = makeSine (sr, 12.0, 1000.0, a);
        for (float v : x) m.push (v, v);
        CHECK (std::fabs (m.momentary + 23.f) < 0.3f, "momentary loudness of -23 dBFS 1 kHz = " << m.momentary << " LUFS");
        CHECK (std::fabs (m.shortTerm + 23.f) < 0.3f, "short-term loudness = " << m.shortTerm << " LUFS");
        CHECK (std::fabs (m.integrated + 23.f) < 0.3f, "integrated loudness = " << m.integrated << " LUFS");
        for (float v : x) m.push (v * 0.1f, v * 0.1f);                       // 20 dB quieter section
        CHECK (m.integrated > -23.6f, "gating ignores the much quieter section (integrated " << m.integrated << ")");
    }
}

static void testEqDynamicsPro()
{
    std::cout << "[eq / compressor / de-esser / expander]\n";
    const double sr = 48000.0;
    auto levelDb = [&] (const std::vector<float>& in, const std::function<void (HumRigProcessor&)>& setup)
    {
        HumRigProcessor p; cleanSetup (p, false); setup (p);
        auto y = runProcessor (p, in, sr, 256);
        return dbOf (rmsOf (y, (size_t) (sr * 0.6)));
    };
    auto sine = [&] (double hz, float amp) { return makeSine (sr, 1.5, hz, amp); };

    // EQ
    {
        auto s300 = sine (300.0, 0.2f);
        const double flat = levelDb (s300, [] (HumRigProcessor& p) { setParam (p, ID::eqManual, 1.f); setParam (p, ID::eqHpfFreq, 40.f); });
        const double cut = levelDb (s300, [] (HumRigProcessor& p) { setParam (p, ID::eqManual, 1.f); setParam (p, ID::eqHpfFreq, 40.f); setParam (p, ID::eqMudFreq, 300.f); setParam (p, ID::eqMudGain, -9.f); });
        CHECK (std::fabs ((flat - cut) - 9.0) < 1.5, "mud band: -9 dB at 300 Hz lowers a 300 Hz tone by " << flat - cut << " dB");
        auto s800 = sine (800.0, 0.2f);
        const double b0 = levelDb (s800, [] (HumRigProcessor& p) { setParam (p, ID::eqManual, 1.f); setParam (p, ID::eqHpfFreq, 40.f); });
        const double b1 = levelDb (s800, [] (HumRigProcessor& p) { setParam (p, ID::eqManual, 1.f); setParam (p, ID::eqHpfFreq, 40.f); setParam (p, ID::eqBoxFreq, 800.f); setParam (p, ID::eqBoxGain, 3.f); });
        CHECK (std::fabs ((b1 - b0) - 3.0) < 1.0, "box band: +3 dB at 800 Hz raises the tone by " << b1 - b0 << " dB");
        auto s60 = sine (60.0, 0.3f);
        const double h12 = levelDb (s60, [] (HumRigProcessor& p) { setParam (p, ID::eqManual, 1.f); setParam (p, ID::eqHpfFreq, 120.f); setParam (p, ID::eqHpfSlope, 0.f); });
        const double h24 = levelDb (s60, [] (HumRigProcessor& p) { setParam (p, ID::eqManual, 1.f); setParam (p, ID::eqHpfFreq, 120.f); setParam (p, ID::eqHpfSlope, 1.f); });
        CHECK (h24 < h12 - 6.0, "24 dB/oct high-pass cuts 60 Hz harder than 12 dB/oct (" << h24 << " vs " << h12 << " dB)");
        auto s12k = sine (12000.0, 0.2f);
        const double lp0 = levelDb (s12k, [] (HumRigProcessor&) {});
        const double lp1 = levelDb (s12k, [] (HumRigProcessor& p) { setParam (p, ID::eqLpfOn, 1.f); setParam (p, ID::eqLpfFreq, 8000.f); });
        CHECK (lp1 < lp0 - 4.0, "low-pass at 8 kHz lowers a 12 kHz tone (" << lp1 << " vs " << lp0 << " dB)");
    }
    // compressor
    {
        auto loud = sine (200.0, 0.8f);
        auto grOf = [&] (const std::function<void (HumRigProcessor&)>& setup)
        { HumRigProcessor p; cleanSetup (p, false); setParam (p, ID::comp, 60.f); setup (p); runProcessor (p, loud, sr, 256); return p.grDbMeter.load(); };
        const float gAuto = grOf ([] (HumRigProcessor&) {});
        const float g10 = grOf ([] (HumRigProcessor& p) { setParam (p, ID::compRatio, 10.f); });
        CHECK (g10 < gAuto - 1.f, "ratio 10:1 reduces more than the macro ratio (" << g10 << " vs " << gAuto << " dB)");
        const float gKnee = grOf ([] (HumRigProcessor& p) { setParam (p, ID::compKnee, 0.f); });
        CHECK (std::isfinite (gKnee) && gKnee < -1.f, "hard knee works (" << gKnee << " dB)");
        // side-chain HPF: a loud 40 Hz tone should stop driving the compressor
        auto bass = sine (40.0, 0.8f);
        auto gb = [&] (float hpf) { HumRigProcessor p; cleanSetup (p, false); setParam (p, ID::comp, 70.f); setParam (p, ID::eqManual, 1.f); setParam (p, ID::eqHpfFreq, 40.f);
                                    setParam (p, ID::compSc, hpf); runProcessor (p, bass, sr, 256); return p.grDbMeter.load(); };
        CHECK (gb (400.f) > gb (20.f) + 3.f, "side-chain high-pass keeps bass from triggering the compressor (" << gb (400.f) << " vs " << gb (20.f) << " dB)");
        // mix 0 = dry
        HumRigProcessor p; cleanSetup (p, false); setParam (p, ID::comp, 80.f); setParam (p, ID::compMix, 0.f);
        auto y = runProcessor (p, loud, sr, 256);
        CHECK (std::fabs (dbOf (rmsOf (y, 8000)) - dbOf (rmsOf (loud, 8000))) < 0.2, "parallel mix 0 % passes the signal untouched");
        // RMS detector reacts slower on a short peak than the peak detector
        std::vector<float> burst ((size_t) (sr * 0.6), 0.f);
        for (size_t i = 12000; i < 12000 + 240; ++i) burst[i] = 0.9f * (float) std::sin (2.0 * 3.14159265 * 400.0 * (double) i / sr);
        auto grBurst = [&] (bool rms) { HumRigProcessor q; cleanSetup (q, false); setParam (q, ID::comp, 90.f); setParam (q, ID::compDet, rms ? 1.f : 0.f); setParam (q, ID::compAttack, 0.5f);
                                         runProcessor (q, burst, sr, 64); return q.grDbMeter.load(); };
        (void) grBurst;
    }
    // de-esser
    {
        auto s7k = sine (7000.0, 0.25f);
        auto lvl = [&] (float f, bool listen, double hz)
        {
            auto in = sine (hz, 0.25f);
            HumRigProcessor p; cleanSetup (p, false); setParam (p, ID::deess, 100.f); setParam (p, ID::dsFreq, f); setParam (p, ID::dsListen, listen ? 1.f : 0.f);
            return dbOf (rmsOf (runProcessor (p, in, sr, 256), (size_t) (sr * 0.6)));
        };
        CHECK (lvl (6000.f, false, 6000.0) < lvl (12000.f, false, 6000.0) - 6.0, "de-esser centred at 6 kHz reduces a 6 kHz tone, centred at 12 kHz it hardly does (" << lvl (6000.f, false, 6000.0) << " vs " << lvl (12000.f, false, 6000.0) << " dB)");
        CHECK (lvl (5500.f, true, 7000.0) > -19.0 && lvl (5500.f, true, 500.0) < -33.0, "listen mode passes the sibilance band and blocks the low band (" << lvl (5500.f, true, 7000.0) << " / " << lvl (5500.f, true, 500.0) << " dB)");
        auto wideLvl = [&] (bool wide) { HumRigProcessor p; cleanSetup (p, false); setParam (p, ID::deess, 100.f); setParam (p, ID::dsMode, wide ? 2.f : 0.f);
                                         std::vector<float> mix = sine (7000.0, 0.25f); auto lo = sine (300.0, 0.25f); for (size_t i = 0; i < mix.size(); ++i) mix[i] += lo[i];
                                         auto y = runProcessor (p, mix, sr, 256); return dbOf (rmsOf (y, (size_t) (sr * 0.6))); };
        CHECK (wideLvl (true) < wideLvl (false) - 0.5, "wide mode lowers the whole signal, split mode only the band");
        (void) s7k;
    }
    // expander
    {
        std::mt19937 nr (9); std::normal_distribution<float> nd (0.f, 1.f);
        std::vector<float> quiet ((size_t) (sr * 2.0)); for (auto& v : quiet) v = 0.0006f * nd (nr);      // about -64 dBFS noise
        auto lvl = [&] (bool on) { HumRigProcessor p; cleanSetup (p, false); setParam (p, ID::expOn, on ? 1.f : 0.f); setParam (p, ID::expThr, -50.f); setParam (p, ID::expRange, 12.f);
                                   return dbOf (rmsOf (runProcessor (p, quiet, sr, 256), (size_t) (sr * 1.2))); };
        CHECK (lvl (true) < lvl (false) - 8.0, "expander turns noise below the threshold down by ~12 dB (" << lvl (true) << " vs " << lvl (false) << " dB)");
        auto voiceLike = sine (220.0, 0.3f);
        auto lv = [&] (bool on) { HumRigProcessor p; cleanSetup (p, false); setParam (p, ID::expOn, on ? 1.f : 0.f);
                                  return dbOf (rmsOf (runProcessor (p, voiceLike, sr, 256), (size_t) (sr * 0.6))); };
        CHECK (std::fabs (lv (true) - lv (false)) < 0.3, "expander leaves normal-level audio alone");
    }
}

static void testSpaceColorStereo()
{
    std::cout << "[colour / space / stereo]\n";
    const double sr = 48000.0;
    // saturation flavours + exciter
    {
        auto in = makeSine (sr, 1.0, 220.0, 0.5f);
        auto run = [&] (int type, float exc) { HumRigProcessor p; cleanSetup (p, false); setParam (p, ID::drive, 80.f); setParam (p, ID::colType, (float) type); setParam (p, ID::excAmt, exc);
                                               bool bad = false; auto y = runProcessor (p, in, sr, 256, 1, 1, &bad); CHECK (! bad, "saturation type " << type << " finite"); return y; };
        auto soft = run (0, 0.f), tube = run (1, 0.f), tape = run (2, 0.f);
        double dTube = 0, dTape = 0; for (size_t i = 8000; i < soft.size(); ++i) { dTube += std::fabs (tube[i] - soft[i]); dTape += std::fabs (tape[i] - soft[i]); }
        CHECK (dTube > 1.0 && dTape > 1.0, "tube and tape sound different from soft saturation");
        auto hf = makeSine (sr, 1.0, 8000.0, 0.2f);
        HumRigProcessor a; cleanSetup (a, false); setParam (a, ID::excAmt, 100.f); setParam (a, ID::excFreq, 5000.f);
        HumRigProcessor b; cleanSetup (b, false);
        CHECK (rmsOf (runProcessor (a, hf, sr, 256), 8000) > rmsOf (runProcessor (b, hf, sr, 256), 8000) * 1.05, "exciter adds high-frequency energy");
    }
    // pre-delay + delay note values
    {
        std::vector<float> click ((size_t) (sr * 1.0), 0.f); for (int i = 0; i < 200; ++i) click[(size_t) i + 2000] = 0.5f;
        auto early = [&] (float pre) { HumRigProcessor p; cleanSetup (p, false); setParam (p, ID::revMix, -6.f); setParam (p, ID::revPre, pre);
                                       auto y = runProcessor (p, click, sr, 256, 1, 2);
                                       const size_t lat = (size_t) p.getLatencySamples();
                                       return rmsOf (y, lat + 2000 + 300, lat + 2000 + 4500); };      // first ~90 ms after the click
        CHECK (early (100.f) < early (0.f) * 0.5, "reverb pre-delay of 100 ms leaves the first 100 ms clean");
        std::vector<float> imp ((size_t) (sr * 1.5), 0.f); imp[2000] = 0.8f;
        HumRigProcessor p; cleanSetup (p, false); setParam (p, ID::dlyMix, -3.f); setParam (p, ID::dlyFb, 0.f); setParam (p, ID::dlyTimeEx, 11.f); setParam (p, ID::dlyFree, 250.f);
        auto y = runProcessor (p, imp, sr, 256, 1, 2);
        const size_t lat = (size_t) p.getLatencySamples();
        size_t best = 0; float bv = 0; for (size_t i = lat + 2000 + 3000; i < y.size(); ++i) if (std::fabs (y[i]) > bv) { bv = std::fabs (y[i]); best = i; }
        const double ms = (double) (best - lat - 2000) / sr * 1000.0;
        CHECK (std::fabs (ms - 250.0) < 3.0, "free delay time 250 ms: echo at " << ms << " ms");
        HumRigProcessor q; cleanSetup (q, false); setParam (q, ID::dlyMix, -3.f); setParam (q, ID::dlyFb, 0.f); setParam (q, ID::dlyTimeEx, 6.f);   // 1/8 dotted at 120 bpm = 375 ms
        auto y2 = runProcessor (q, imp, sr, 256, 1, 2);
        best = 0; bv = 0; for (size_t i = lat + 2000 + 3000; i < y2.size(); ++i) if (std::fabs (y2[i]) > bv) { bv = std::fabs (y2[i]); best = i; }
        CHECK (std::fabs ((double) (best - lat - 2000) / sr * 1000.0 - 375.0) < 3.0, "1/8 dotted at 120 bpm = 375 ms");
    }
    // stereo: width 0 / mono check / doubler
    {
        auto in = makeHum (sr, 1.5, [] (double) { return 200.0; });
        auto stereoRun = [&] (const std::function<void (HumRigProcessor&)>& setup, std::vector<float>& L, std::vector<float>& R)
        {
            HumRigProcessor p; cleanSetup (p, false); setup (p);
            setLayout (p, 1, 2); p.setRateAndBufferSizeDetails (sr, 256); p.prepareToPlay (sr, 256);
            L.assign (in.size(), 0.f); R.assign (in.size(), 0.f); juce::MidiBuffer m;
            for (size_t pos = 0; pos < in.size(); pos += 256)
            {
                const int n = (int) std::min<size_t> (256, in.size() - pos);
                juce::AudioBuffer<float> b (2, n); b.copyFrom (0, 0, in.data() + pos, n); b.clear (1, 0, n);
                p.processBlock (b, m);
                for (int i = 0; i < n; ++i) { L[pos + (size_t) i] = b.getSample (0, i); R[pos + (size_t) i] = b.getSample (1, i); }
            }
        };
        std::vector<float> L, R;
        auto diff = [&] { double d = 0; for (size_t i = 8000; i < L.size(); ++i) d += std::fabs (L[i] - R[i]); return d; };
        stereoRun ([] (HumRigProcessor& p) { setParam (p, ID::revMix, -6.f); setParam (p, ID::dlyMix, -6.f); }, L, R);
        const double wide = diff();
        stereoRun ([] (HumRigProcessor& p) { setParam (p, ID::revMix, -6.f); setParam (p, ID::dlyMix, -6.f); setParam (p, ID::width, 0.f); }, L, R);
        CHECK (diff() < wide * 0.02, "width 0 % makes the output mono-compatible (" << diff() << " vs " << wide << ")");
        stereoRun ([] (HumRigProcessor& p) { setParam (p, ID::revMix, -6.f); setParam (p, ID::dlyMix, -6.f); setParam (p, ID::monoCheck, 1.f); }, L, R);
        CHECK (diff() == 0.0, "mono check outputs identical channels");
        stereoRun ([] (HumRigProcessor& p) { setParam (p, ID::dblMix, 40.f); }, L, R);
        CHECK (diff() > 5.0, "doubler spreads a mono voice into two different channels (" << diff() << ")");
    }
    // state with new parameters
    {
        HumRigProcessor p; setParam (p, ID::eqMudGain, -4.5f); setParam (p, ID::compAttack, 22.f); setParam (p, ID::dlyTimeEx, 6.f); setParam (p, ID::limOn, 1.f);
        juce::MemoryBlock mb; p.getStateInformation (mb);
        HumRigProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (std::fabs (q.apvts.getRawParameterValue (ID::eqMudGain)->load() + 4.5f) < 0.01f && (int) q.apvts.getRawParameterValue (ID::dlyTimeEx)->load() == 6
               && q.apvts.getRawParameterValue (ID::limOn)->load() > 0.5f, "new parameters round-trip through the saved state");
    }
}

static void testScopeFeed()
{
    std::cout << "[spectrum feed]\n";
    const double sr = 48000.0;
    auto& bus = HumRigProcessor::bus();
    const int before = bus.scopePos.load();
    HumRigProcessor p; cleanSetup (p, false);
    setParam (p, ID::air, 100.f);
    auto in = makeSine (sr, 1.0, 1000.0, 0.3f);
    runProcessor (p, in, sr, 480, 1, 2);
    CHECK (bus.scopePos.load() - before >= 40000, "processing audio advances the scope feed (" << bus.scopePos.load() - before << " samples)");
    // Goertzel at 1 kHz over the last 4096 samples of the "after" buffer
    const int pos = bus.scopePos.load(), N = SharedBus::kScope;
    auto goertzel = [&] (const std::array<std::atomic<float>, SharedBus::kScope>& src, double f)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = 2.0 * std::cos (w);
        double s1 = 0, s2 = 0;
        for (int i = 0; i < N; ++i) { const double x = src[(size_t) ((pos - N + i) & (N - 1))].load(); const double s = x + c * s1 - s2; s2 = s1; s1 = s; }
        return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / (N / 2.0);
    };
    const double a1k = goertzel (bus.scopePost, 1000.0), a5k = goertzel (bus.scopePost, 5000.0);
    CHECK (a1k > 0.1 && a1k > a5k * 20.0, "the feed carries the 1 kHz tone (" << a1k << " vs " << a5k << " at 5 kHz)");
}

// every detailed control at random values, many sample rates / block sizes / layouts: output must stay finite and bounded
static void testRandomParameters()
{
    std::cout << "[random parameter stress]\n";
    std::mt19937 rng (2024);
    int n = 0; const char* const* ids = detailedParamIds (n);
    bool finite = true, bounded = true, ceilingOk = true; int runs = 0;
    const double rates[] = { 8000.0, 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 };
    std::uniform_real_distribution<float> ud (0.f, 1.f);
    std::normal_distribution<float> nd (0.f, 0.5f);
    for (int iter = 0; iter < 36; ++iter)
    {
        const double sr = rates[iter % 6];
        HumRigProcessor p;
        for (int i = 0; i < n; ++i)
            if (auto* prm = p.apvts.getParameter (ids[i])) prm->setValueNotifyingHost (ud (rng));
        for (const char* id : { ID::comp, ID::deess, ID::drive, ID::warmth, ID::presence, ID::air, ID::inGain, ID::revMix, ID::dlyMix })
            if (auto* prm = p.apvts.getParameter (id)) prm->setValueNotifyingHost (ud (rng));
        const bool lim = (iter % 2) == 0;
        setParam (p, ID::limOn, lim ? 1.f : 0.f); setParam (p, ID::limCeil, -1.f); setParam (p, ID::dsListen, 0.f);
        std::vector<float> in ((size_t) (sr * 0.6));
        for (size_t i = 0; i < in.size(); ++i) in[i] = 0.4f * (float) std::sin (2.0 * 3.14159265 * 200.0 * (double) i / sr) + 0.3f * nd (rng);
        const int block = (iter % 3 == 0) ? 64 : (iter % 3 == 1 ? 480 : 1000);
        const bool stereoOut = (iter % 4) != 1;
        bool bad = false;
        auto y = runProcessor (p, in, sr, block, 1, stereoOut ? 2 : 1, &bad);
        ++runs;
        if (bad) finite = false;
        float pk = 0.f; for (size_t k = (size_t) (sr * 0.2); k < y.size(); ++k) pk = std::max (pk, std::fabs (y[k]));
        if (pk > 16.f) bounded = false;
        if (lim && pk > 0.8914f) { ceilingOk = false; std::cout << "  ceiling exceeded: peak " << pk << " at " << sr << " Hz\n"; }
    }
    CHECK (finite && bounded, runs << " random parameter sets at 8k-192k stay finite and bounded");
    CHECK (ceilingOk, "the limiter holds its ceiling with random settings");
}

static void testEditor()
{
    std::cout << "[editor snapshot]\n";
    HumRigProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    CHECK (ed != nullptr, "editor created");
    if (ed == nullptr) return;
    outDir.createDirectory();
    juce::PNGImageFormat png;
    // feed some audio so the meters/curves have something to show
    auto sig = makeHum (48000.0, 1.0, [] (double t) { return 200.0 + 40.0 * t; }, -50.f, 5);
    runProcessor (p, sig, 48000.0, 480, 1, 2);
    const char* names[] = { "home", "clean", "eq", "compress", "deess", "color", "space", "stereo", "master" };
    for (int page = 0; page < 9; ++page)
    {
        p.uiPage = page;
        std::unique_ptr<juce::AudioProcessorEditor> e2 (p.createEditor());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);    // let ParameterAttachments deliver their initial values
        auto img = e2->createComponentSnapshot (e2->getLocalBounds(), true, 1.5f);
        auto f = outDir.getChildFile (juce::String ("editor-") + names[page] + ".png");
        f.deleteFile();
        if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) png.writeImageToStream (img, *os);
        CHECK (f.getSize() > 1000, "wrote " << f.getFullPathName());
    }
    p.uiPage = 0;
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    auto f = outDir.getChildFile ("editor.png");
    f.deleteFile();
    if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) png.writeImageToStream (img, *os);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "test-out");

    testScale();
    testDetector();
    testLayouts();
    testReprepare();
    testListening();
    testBusAndListeningE2E();
    testPresetsAndState();
    testPitchCorrection (48000.0);
    testPitchCorrection (44100.0);
    testLatencyAndBypass (48000.0);
    testLatencyAndBypass (44100.0);
    testImpulse (48000.0);
    testImpulse (44100.0);
    testRobustness (48000.0);
    testChain (48000.0);
    testGolden (Limiter::lookaheadFor (48000.0));
    testLimiterAndLoudness();
    testEqDynamicsPro();
    testSpaceColorStereo();
    testScopeFeed();
    testRandomParameters();
    testEditor();

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
