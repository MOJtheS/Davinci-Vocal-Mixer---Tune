// Headless test harness for HumRig: DSP units + the real AudioProcessor end-to-end.
// Run: ./HumRigTests [outdir]   (writes WAV/PNG debug files to outdir, default ./test-out)
#include <cmath>
#include <iostream>
#include <random>
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "Editor/PluginEditor.h"
#include "DSP/PitchDetector.h"
#include "DSP/Scale.h"

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
    CHECK (p.getNumPrograms() >= 8, "has " << p.getNumPrograms() << " presets");
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

static void testEditor()
{
    std::cout << "[editor snapshot]\n";
    HumRigProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    CHECK (ed != nullptr, "editor created");
    if (ed == nullptr) return;
    juce::MessageManager::getInstance()->runDispatchLoopUntil (100);    // let ParameterAttachments deliver their initial values
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    outDir.createDirectory();
    auto f = outDir.getChildFile ("editor.png");
    f.deleteFile();
    juce::PNGImageFormat png;
    if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
        png.writeImageToStream (img, *os);
    CHECK (f.getSize() > 1000, "wrote " << f.getFullPathName());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "test-out");

    testScale();
    testDetector();
    testLayouts();
    testReprepare();
    testPresetsAndState();
    testPitchCorrection (48000.0);
    testPitchCorrection (44100.0);
    testLatencyAndBypass (48000.0);
    testLatencyAndBypass (44100.0);
    testImpulse (48000.0);
    testImpulse (44100.0);
    testRobustness (48000.0);
    testChain (48000.0);
    testEditor();

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
