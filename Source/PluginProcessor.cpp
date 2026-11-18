#include "PluginProcessor.h"
#include "Editor/PluginEditor.h"

using namespace humrig;

HumRigProcessor::HumRigProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "HUMRIG", createParameterLayout())
{
    auto g = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    p.tuneOn = g (ID::tuneOn); p.key = g (ID::key); p.scale = g (ID::scale); p.speed = g (ID::speed);
    p.vibrato = g (ID::vibrato); p.flex = g (ID::flex); p.range = g (ID::range); p.voice = g (ID::voice);
    p.warmth = g (ID::warmth); p.presence = g (ID::presence); p.air = g (ID::air);
    p.comp = g (ID::comp); p.deess = g (ID::deess); p.drive = g (ID::drive);
    p.revMix = g (ID::revMix); p.revSize = g (ID::revSize); p.revDecay = g (ID::revDecay);
    p.dlyMix = g (ID::dlyMix); p.dlyFb = g (ID::dlyFb); p.dlyTime = g (ID::dlyTime); p.dlyPing = g (ID::dlyPing);
    p.inGain = g (ID::inGain); p.outGain = g (ID::outGain); p.mix = g (ID::mix);
    instanceId = SharedBus::get().nextInstanceId();
    handledGen = SharedBus::get().requestGen.load();
}

bool HumRigProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    const bool inOk  = in == juce::AudioChannelSet::mono()  || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk;            // mono->mono, stereo->stereo, mono->stereo, stereo->mono
}

void HumRigProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlock = std::max (1, samplesPerBlock);

    corrector.prepare (sr, maxBlock);
    setLatencySamples (corrector.getLatencySamples());

    compressor.prepare (sr);
    deesser.prepare (sr);
    delay.prepare (sr);
    reverb.setSampleRate (sr);
    reverb.reset();

    for (auto& r : dryRing) r.assign ((size_t) std::max (1, corrector.getLatencySamples()), 0.f);
    dryPos = 0;

    analyzer.prepare (sr);
    handledGen = SharedBus::get().requestGen.load();                       // hosts may re-prepare when playback starts:
    analyzing = SharedBus::get().listenState.load() == SharedBus::listening;   // keep listening if a request is already running
    for (auto* v : { &monoRaw, &monoIn, &monoTuned, &voice, &dlyL, &dlyR, &revL, &revR }) v->assign ((size_t) maxBlock, 0.f);

    inGainSm.reset (sr, 0.02);  inGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.inGain->load()));
    outGainSm.reset (sr, 0.02); outGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.outGain->load()));
    mixSm.reset (sr, 0.02);     mixSm.setCurrentAndTargetValue (p.mix->load() / 100.f);

    hpf.reset(); lowShelf.reset(); peak.reset(); highShelf.reset();
    lastVoice = lastWarmth = lastPresence = lastAir = -1.f;
    updateEq();
}

void HumRigProcessor::updateEq()
{
    const float voiceV = p.voice->load(), w = p.warmth->load(), pr = p.presence->load(), a = p.air->load();
    if (voiceV == lastVoice && w == lastWarmth && pr == lastPresence && a == lastAir) return;
    lastVoice = voiceV; lastWarmth = w; lastPresence = pr; lastAir = a;
    const bool female = voiceV > 0.5f;
    hpf.makeHighPass (sr, female ? 110.0 : 75.0, 0.707);
    lowShelf.makeLowShelf (sr, female ? 240.0 : 180.0, (w - 50.0) / 50.0 * 6.0);
    peak.makePeak (sr, female ? 3800.0 : 3000.0, 0.9, (pr - 50.0) / 50.0 * 5.0);
    highShelf.makeHighShelf (sr, female ? 11000.0 : 10000.0, (a - 50.0) / 50.0 * 6.0);
}

void HumRigProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nIn = getTotalNumInputChannels(), nOut = getTotalNumOutputChannels();
    const int total = buffer.getNumSamples();
    for (int ch = nIn; ch < nOut; ++ch) buffer.clear (ch, 0, total);

    // ---- parameters (once per host block) ----
    TuneSettings ts;
    ts.enabled = p.tuneOn->load() > 0.5f;
    ts.key = (int) p.key->load();
    ts.scale = (int) p.scale->load();
    ts.speed = p.speed->load();
    ts.vibrato = p.vibrato->load();
    ts.flex = p.flex->load();
    {
        const bool female = p.voice->load() > 0.5f;
        switch ((int) p.range->load())
        {
            case 1:  ts.minHz = 180.f; ts.maxHz = 1200.f; break;     // soprano
            case 2:  ts.minHz = 90.f;  ts.maxHz = 600.f;  break;     // alto / tenor
            case 3:  ts.minHz = 60.f;  ts.maxHz = 300.f;  break;     // bass
            default: ts.minHz = female ? 110.f : 65.f; ts.maxHz = female ? 1100.f : 600.f; break;
        }
    }
    updateEq();
    compressor.setAmount (p.comp->load());
    deesser.setAmount (p.deess->load());
    drive.setAmount (p.drive->load());

    inGainSm.setTargetValue (juce::Decibels::decibelsToGain (p.inGain->load()));
    outGainSm.setTargetValue (juce::Decibels::decibelsToGain (p.outGain->load()));
    mixSm.setTargetValue (p.mix->load() / 100.f);

    // delay time from host tempo
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) if (*b > 20.0 && *b < 400.0) bpm = *b;
    const double beats[3] = { 1.0, 0.5, 0.25 };
    const double delaySec = beats[juce::jlimit (0, 2, (int) p.dlyTime->load())] * 60.0 / bpm;
    delay.set (delaySec, p.dlyFb->load() / 100.f, p.dlyPing->load() > 0.5f, mixDbToLin (p.dlyMix->load()));

    {
        const float size = p.revSize->load(), decay = p.revDecay->load();
        juce::Reverb::Parameters rp;
        const float dnorm = juce::jlimit (0.f, 1.f, std::log (decay / 0.2f) / std::log (8.f / 0.2f));
        rp.roomSize = 0.35f + 0.63f * dnorm;
        rp.damping = 0.65f - 0.45f * size;
        rp.width = 0.6f + 0.4f * size;
        rp.wetLevel = 1.f; rp.dryLevel = 0.f; rp.freezeMode = 0.f;
        reverb.setParameters (rp);
    }
    const float revGain = mixDbToLin (p.revMix->load());

    float grMin = 0.f, inPeak = 0.f, outPeak = 0.f;
    bool sawBad = false;

    auto& sharedBus = SharedBus::get();
    {
        const int gen = sharedBus.requestGen.load();
        if (gen != handledGen) { handledGen = gen; analyzer.reset(); analyzing = sharedBus.listenState.load() == SharedBus::listening; }
        if (analyzing && sharedBus.listenState.load() != SharedBus::listening) analyzing = false;   // cancelled / finished elsewhere
    }

    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = std::min (maxBlock, total - start);
        float* mi = monoIn.data();

        // ---- input -> mono (+ keep latency-aligned dry for Dry/Wet) ----
        const float* inL = buffer.getReadPointer (0) + start;
        const float* inR = nIn > 1 ? buffer.getReadPointer (1) + start : inL;
        const int ringN = (int) dryRing[0].size();
        for (int i = 0; i < n; ++i)
        {
            const float g = inGainSm.getNextValue();
            const float raw = 0.5f * (inL[i] + inR[i]);
            mi[i] = raw * g;
            if (analyzing) monoRaw[(size_t) i] = raw;
            inPeak = std::max (inPeak, std::fabs (mi[i]));
        }

        if (analyzing)
        {
            analyzer.process (monoRaw.data(), n);
            sharedBus.raiseProgress (analyzer.progress());
            if (analyzer.isDone())
            {
                int expected = SharedBus::listening;
                if (sharedBus.listenState.compare_exchange_strong (expected, SharedBus::writing))   // first instance to finish wins
                {
                    const auto r = analyzer.result();
                    auto& o = sharedBus.result;
                    o.key = r.key; o.minor = r.minor ? 1 : 0; o.female = r.female ? 1 : 0; o.range = r.range;
                    o.medianF0 = r.medianF0; o.keyConfidence = r.keyConfidence; o.inGainDb = r.inGainDb;
                    o.comp = r.comp; o.warmth = r.warmth; o.presence = r.presence; o.air = r.air; o.deess = r.deess;
                    sharedBus.resultGen.store (handledGen);
                    sharedBus.listenProgress.store (1.f);
                    sharedBus.listenState.store (r.valid ? SharedBus::done : SharedBus::idle);
                }
                analyzing = false;
            }
        }

        // pitch stage (constant latency)
        corrector.process (mi, monoTuned.data(), n, ts);

        // ---- tone / dynamics chain (mono) ----
        float* v = voice.data();
        for (int i = 0; i < n; ++i)
        {
            float x = monoTuned[(size_t) i];
            x = hpf.process (x);
            x = lowShelf.process (x);
            x = peak.process (x);
            x = highShelf.process (x);
            x = deesser.process (x);
            x = compressor.process (x);
            grMin = std::min (grMin, compressor.grDb);
            x = drive.process (x);
            v[i] = x;
        }

        // ---- time effects ----
        delay.process (v, dlyL.data(), dlyR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            const float send = (v[i] + 0.5f * (dlyL[(size_t) i] + dlyR[(size_t) i])) * revGain;
            revL[(size_t) i] = revR[(size_t) i] = send;
        }
        if (revGain > 0.f) reverb.processStereo (revL.data(), revR.data(), n);
        else { std::fill_n (revL.begin(), n, 0.f); std::fill_n (revR.begin(), n, 0.f); }

        // ---- sum, dry/wet, output ----
        float* outL = buffer.getWritePointer (0) + start;
        float* outR = nOut > 1 ? buffer.getWritePointer (1) + start : nullptr;
        for (int i = 0; i < n; ++i)
        {
            const float wetL = v[i] + dlyL[(size_t) i] + revL[(size_t) i];
            const float wetR = v[i] + dlyR[(size_t) i] + revR[(size_t) i];

            // latency-aligned dry (original input, stereo preserved)
            const float dL = dryRing[0][(size_t) dryPos], dR = dryRing[1][(size_t) dryPos];
            dryRing[0][(size_t) dryPos] = inL[i];
            dryRing[1][(size_t) dryPos] = inR[i];
            dryPos = (dryPos + 1) % ringN;

            const float m = mixSm.getNextValue(), og = outGainSm.getNextValue();
            float l = (dL * (1.f - m) + wetL * m) * og;
            float r = (dR * (1.f - m) + wetR * m) * og;
            if (! (std::isfinite (l) && std::isfinite (r))) { sawBad = true; l = r = 0.f; }   // never emit NaN/Inf to the host
            l = juce::jlimit (-16.f, 16.f, l); r = juce::jlimit (-16.f, 16.f, r);
            if (outR != nullptr) { outL[i] = l; outR[i] = r; }
            else outL[i] = 0.5f * (l + r);
            outPeak = std::max (outPeak, std::fabs (l));
        }
    }

    if (sawBad)      // something went non-finite: flush all recursive state so it cannot persist
    {
        hpf.reset(); lowShelf.reset(); peak.reset(); highShelf.reset();
        compressor.reset(); deesser.reset(); delay.reset(); reverb.reset(); corrector.reset();
    }

    grDbMeter.store (grMin);
    inLevelMeter.store (inPeak);
    outLevelMeter.store (outPeak);

    // publish for UIs living on another instance (see SharedBus)
    const bool active = inPeak > 1.0e-4f;
    const int64_t now = juce::Time::currentTimeMillis();
    if (active) ownLastActiveMs.store (now);
    if (active || sharedBus.activeInstance.load() == instanceId)
    {
        sharedBus.grDb.store (grMin); sharedBus.inPeak.store (inPeak); sharedBus.outPeak.store (outPeak);
        sharedBus.detectedMidi.store (corrector.detectedMidi.load()); sharedBus.targetMidi.store (corrector.targetMidi.load());
        sharedBus.activeInstance.store (instanceId);
        if (active) sharedBus.lastActiveMs.store (now);
    }
}

HumRigProcessor::MeterSnapshot HumRigProcessor::getMeters() const
{
    const int64_t now = juce::Time::currentTimeMillis();
    MeterSnapshot m;
    auto& b = SharedBus::get();
    if (now - ownLastActiveMs.load() > 400 && now - b.lastActiveMs.load() < 400)
    {
        m.grDb = b.grDb.load(); m.inPeak = b.inPeak.load(); m.outPeak = b.outPeak.load();
        m.detectedMidi = b.detectedMidi.load(); m.targetMidi = b.targetMidi.load();
    }
    else
    {
        m.grDb = grDbMeter.load(); m.inPeak = inLevelMeter.load(); m.outPeak = outLevelMeter.load();
        m.detectedMidi = corrector.detectedMidi.load(); m.targetMidi = corrector.targetMidi.load();
    }
    return m;
}

int HumRigProcessor::requestListening()
{
    auto& b = SharedBus::get();
    b.listenProgress.store (0.f);
    b.listenState.store (SharedBus::listening);   // state first, so instances that see the new generation also see "listening"
    return b.requestGen.fetch_add (1) + 1;
}

void HumRigProcessor::cancelListening()
{
    int expected = SharedBus::listening;
    SharedBus::get().listenState.compare_exchange_strong (expected, SharedBus::idle);
}

bool HumRigProcessor::applyListeningResults (int generation)
{
    auto& b = SharedBus::get();
    if (b.listenState.load() != SharedBus::done || b.resultGen.load() != generation) return false;
    auto set = [this] (const char* id, float v)
    {
        if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    };
    const auto& r = b.result;
    set (ID::key, (float) r.key.load());
    set (ID::scale, r.minor.load() ? 2.f : 1.f);              // Minor : Major
    set (ID::voice, r.female.load() ? 1.f : 0.f);
    set (ID::range, (float) r.range.load());
    set (ID::inGain, r.inGainDb.load());
    set (ID::comp, r.comp.load());
    set (ID::warmth, r.warmth.load()); set (ID::presence, r.presence.load()); set (ID::air, r.air.load());
    set (ID::deess, r.deess.load());
    return true;
}

// ---------------- presets / programs / state ----------------
int HumRigProcessor::getNumPrograms() { return getNumPresets(); }
const juce::String HumRigProcessor::getProgramName (int i) { return getPreset (i).name; }
void HumRigProcessor::setCurrentProgram (int i) { applyPreset (i); }

void HumRigProcessor::applyPreset (int index)
{
    index = juce::jlimit (0, getNumPresets() - 1, index);
    currentPreset = index;
    const auto& pr = getPreset (index);
    auto set = [this] (const char* id, float v)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    };
    set (ID::warmth, pr.warmth); set (ID::presence, pr.presence); set (ID::air, pr.air);
    set (ID::comp, pr.comp); set (ID::deess, pr.deess); set (ID::drive, pr.drive);
    set (ID::revMix, pr.revMix); set (ID::revSize, pr.revSize); set (ID::revDecay, pr.revDecay);
    set (ID::dlyMix, pr.dlyMix); set (ID::dlyFb, pr.dlyFb); set (ID::dlyTime, (float) pr.dlyTime);
    set (ID::dlyPing, pr.dlyPing ? 1.f : 0.f);
    set (ID::speed, pr.speed); set (ID::vibrato, pr.vibrato); set (ID::flex, pr.flex);
}

void HumRigProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void HumRigProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xml);
            currentPreset = juce::jlimit (0, getNumPresets() - 1, (int) vt.getProperty ("preset", 0));
            apvts.replaceState (vt);
        }
}

juce::AudioProcessorEditor* HumRigProcessor::createEditor() { return new HumRigEditor (*this); }
