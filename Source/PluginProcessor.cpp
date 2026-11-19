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
    q.limOn = g (ID::limOn);
    q.limCeil = g (ID::limCeil);
    q.limRelease = g (ID::limRelease);
    q.limGain = g (ID::limGain);
    q.eqManual = g (ID::eqManual);
    q.eqHpfFreq = g (ID::eqHpfFreq);
    q.eqHpfSlope = g (ID::eqHpfSlope);
    q.eqLowFreq = g (ID::eqLowFreq);
    q.eqMudFreq = g (ID::eqMudFreq);
    q.eqMudGain = g (ID::eqMudGain);
    q.eqMudQ = g (ID::eqMudQ);
    q.eqBoxFreq = g (ID::eqBoxFreq);
    q.eqBoxGain = g (ID::eqBoxGain);
    q.eqBoxQ = g (ID::eqBoxQ);
    q.eqPresFreq = g (ID::eqPresFreq);
    q.eqPresQ = g (ID::eqPresQ);
    q.eqAirFreq = g (ID::eqAirFreq);
    q.eqLpfOn = g (ID::eqLpfOn);
    q.eqLpfFreq = g (ID::eqLpfFreq);
    q.compRatio = g (ID::compRatio);
    q.compAttack = g (ID::compAttack);
    q.compRelease = g (ID::compRelease);
    q.compKnee = g (ID::compKnee);
    q.compMix = g (ID::compMix);
    q.compDet = g (ID::compDet);
    q.compSc = g (ID::compSc);
    q.compThr = g (ID::compThr);
    q.compMk = g (ID::compMk);
    q.dsFreq = g (ID::dsFreq);
    q.dsMode = g (ID::dsMode);
    q.dsRange = g (ID::dsRange);
    q.dsThr = g (ID::dsThr);
    q.dsListen = g (ID::dsListen);
    q.colType = g (ID::colType);
    q.colTone = g (ID::colTone);
    q.colMix = g (ID::colMix);
    q.excFreq = g (ID::excFreq);
    q.excAmt = g (ID::excAmt);
    q.revPre = g (ID::revPre);
    q.revFilt = g (ID::revFilt);
    q.revLo = g (ID::revLo);
    q.revHi = g (ID::revHi);
    q.duck = g (ID::duck);
    q.dlyTimeEx = g (ID::dlyTimeEx);
    q.dlyFree = g (ID::dlyFree);
    q.dlyFilt = g (ID::dlyFilt);
    q.dlyHpf = g (ID::dlyHpf);
    q.dlyLpf = g (ID::dlyLpf);
    q.dblMix = g (ID::dblMix);
    q.dblDelay = g (ID::dblDelay);
    q.dblDetune = g (ID::dblDetune);
    q.dblHpf = g (ID::dblHpf);
    q.width = g (ID::width);
    q.monoCheck = g (ID::monoCheck);
    q.expOn = g (ID::expOn);
    q.expThr = g (ID::expThr);
    q.expRange = g (ID::expRange);
    q.expHold = g (ID::expHold);
    q.expRel = g (ID::expRel);
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
    limiter.prepare (sr);
    setLatencySamples (corrector.getLatencySamples() + limiter.lookahead());   // constant: the limiter always delays, whether it is on or off

    expander.prepare (sr);
    compressor.prepare (sr);
    deesser.prepare (sr);
    drive.prepare (sr);
    delay.prepare (sr);
    preDelay.prepare (sr);
    ducker.prepare (sr);
    doubler.prepare (sr);
    loudness.prepare (sr);
    handledLufsGen = SharedBus::get().lufsResetGen.load();
    reverb.setSampleRate (sr);
    reverb.reset();
    for (auto* f : { &revHpL, &revHpR, &revLpL, &revLpR }) f->reset();
    lastRevLo = lastRevHi = -1.f;

    for (auto& r : dryRing) r.assign ((size_t) std::max (1, corrector.getLatencySamples()), 0.f);
    dryPos = 0;

    analyzer.prepare (sr);
    handledGen = SharedBus::get().requestGen.load();                       // hosts may re-prepare when playback starts:
    analyzing = SharedBus::get().listenState.load() == SharedBus::listening;   // keep listening if a request is already running
    for (auto* v : { &monoRaw, &monoIn, &monoTuned, &voice, &dlyL, &dlyR, &revL, &revR }) v->assign ((size_t) maxBlock, 0.f);

    inGainSm.reset (sr, 0.02);  inGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.inGain->load()));
    outGainSm.reset (sr, 0.02); outGainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.outGain->load()));
    mixSm.reset (sr, 0.02);     mixSm.setCurrentAndTargetValue (p.mix->load() / 100.f);

    for (auto* f : { &hpf, &hpf2, &mudBq, &boxBq, &lowShelf, &peak, &highShelf, &lpf }) f->reset();
    lastEq.fill (-12345.f);
    updateEq();
}

void HumRigProcessor::updateEq()
{
    const float voiceV = p.voice->load(), w = p.warmth->load(), pr = p.presence->load(), a = p.air->load();
    const bool manual = q.eqManual->load() > 0.5f;
    const std::array<float, 24> now {
        voiceV, w, pr, a, manual ? 1.f : 0.f, q.eqHpfFreq->load(), q.eqHpfSlope->load(), q.eqLowFreq->load(),
        q.eqMudFreq->load(), q.eqMudGain->load(), q.eqMudQ->load(), q.eqBoxFreq->load(), q.eqBoxGain->load(), q.eqBoxQ->load(),
        q.eqPresFreq->load(), q.eqPresQ->load(), q.eqAirFreq->load(), q.eqLpfOn->load(), q.eqLpfFreq->load(), 0.f, 0.f, 0.f, 0.f, 0.f };
    if (now == lastEq) return;
    lastEq = now;
    const bool female = voiceV > 0.5f;
    const double hpfHz  = manual ? (double) now[5]  : (female ? 110.0 : 75.0);
    const double lowHz  = manual ? (double) now[7]  : (female ? 240.0 : 180.0);
    const double presHz = manual ? (double) now[14] : (female ? 3800.0 : 3000.0);
    const double presQ  = manual ? (double) now[15] : 0.9;
    const double airHz  = manual ? (double) now[16] : (female ? 11000.0 : 10000.0);
    hpf.makeHighPass (sr, hpfHz, 0.707);
    hpf2.makeHighPass (sr, hpfHz, 0.707);
    hpf24 = manual && now[6] > 0.5f;
    mudOn = std::fabs (now[9]) > 0.01f;   if (mudOn) mudBq.makePeak (sr, now[8], now[10], now[9]);
    boxOn = std::fabs (now[12]) > 0.01f;  if (boxOn) boxBq.makePeak (sr, now[11], now[13], now[12]);
    lowShelf.makeLowShelf (sr, lowHz, (w - 50.0) / 50.0 * 6.0);
    peak.makePeak (sr, presHz, presQ, (pr - 50.0) / 50.0 * 5.0);
    highShelf.makeHighShelf (sr, airHz, (a - 50.0) / 50.0 * 6.0);
    lpfOn = now[17] > 0.5f;               if (lpfOn) lpf.makeLowPass (sr, now[18], 0.707);
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
    {
        Compressor::Settings cs;
        cs.amount = p.comp->load(); cs.ratioOverride = q.compRatio->load(); cs.attackMs = q.compAttack->load(); cs.releaseMs = q.compRelease->load();
        cs.kneeDb = q.compKnee->load(); cs.mix = q.compMix->load() * 0.01f; cs.rms = q.compDet->load() > 0.5f; cs.scHpfHz = q.compSc->load();
        cs.thresholdTrimDb = q.compThr->load(); cs.makeupTrimDb = q.compMk->load();
        compressor.set (cs);

        DeEsser::Settings ds;
        ds.amount = p.deess->load(); ds.freqHz = q.dsFreq->load(); ds.mode = (int) q.dsMode->load(); ds.maxReductionDb = q.dsRange->load();
        ds.thresholdTrimDb = q.dsThr->load(); ds.listen = q.dsListen->load() > 0.5f;
        deesser.set (ds);

        Drive::Settings dr;
        dr.amount = p.drive->load(); dr.type = (int) q.colType->load(); dr.tone = q.colTone->load(); dr.mix = q.colMix->load() * 0.01f;
        dr.excFreq = q.excFreq->load(); dr.excAmount = q.excAmt->load();
        drive.set (dr);

        expander.set (q.expOn->load() > 0.5f, q.expThr->load(), q.expRange->load(), q.expHold->load(), q.expRel->load());
        limiter.setParams (q.limOn->load() > 0.5f, q.limCeil->load(), q.limRelease->load(), q.limGain->load());
        preDelay.setMs (q.revPre->load());
        ducker.setAmount (q.duck->load());
        doubler.set (q.dblDelay->load(), q.dblDetune->load(), q.dblHpf->load());
    }
    const float dblGain = q.dblMix->load() * 0.01f;
    const float widthV = q.width->load() * 0.01f;
    const bool monoCheckOn = q.monoCheck->load() > 0.5f;
    const bool revFiltOn = q.revFilt->load() > 0.5f;
    if (revFiltOn && (q.revLo->load() != lastRevLo || q.revHi->load() != lastRevHi))
    {
        lastRevLo = q.revLo->load(); lastRevHi = q.revHi->load();
        revHpL.makeHighPass (sr, lastRevLo, 0.707); revHpR = revHpL;
        revLpL.makeLowPass (sr, lastRevHi, 0.707);  revLpR = revLpL;
    }

    inGainSm.setTargetValue (juce::Decibels::decibelsToGain (p.inGain->load()));
    outGainSm.setTargetValue (juce::Decibels::decibelsToGain (p.outGain->load()));
    mixSm.setTargetValue (p.mix->load() / 100.f);

    // delay time from host tempo
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) if (*b > 20.0 && *b < 400.0) bpm = *b;
    const double beats[3] = { 1.0, 0.5, 0.25 };
    double delaySec = beats[juce::jlimit (0, 2, (int) p.dlyTime->load())] * 60.0 / bpm;
    {
        const int ex = (int) q.dlyTimeEx->load();
        if (ex == 11) delaySec = (double) q.dlyFree->load() * 0.001;
        else if (ex >= 1 && ex <= 10) delaySec = kDelayTimeExBeats[ex] * 60.0 / bpm;
    }
    delay.setFilters (q.dlyFilt->load() > 0.5f, q.dlyLpf->load(), q.dlyHpf->load());
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

    float grMin = 0.f, inPeak = 0.f, outPeak = 0.f, limGrMin = 0.f;
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

        for (int i = 0; i < n; ++i) mi[i] = expander.process (mi[i]);

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
            if (hpf24) x = hpf2.process (x);
            if (mudOn) x = mudBq.process (x);
            if (boxOn) x = boxBq.process (x);
            x = lowShelf.process (x);
            x = peak.process (x);
            x = highShelf.process (x);
            if (lpfOn) x = lpf.process (x);
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
            revL[(size_t) i] = revR[(size_t) i] = preDelay.process (send);
        }
        if (revGain > 0.f)
        {
            reverb.processStereo (revL.data(), revR.data(), n);
            if (revFiltOn)
                for (int i = 0; i < n; ++i)
                {
                    revL[(size_t) i] = revLpL.process (revHpL.process (revL[(size_t) i]));
                    revR[(size_t) i] = revLpR.process (revHpR.process (revR[(size_t) i]));
                }
        }
        else { std::fill_n (revL.begin(), n, 0.f); std::fill_n (revR.begin(), n, 0.f); }

        // ---- sum, dry/wet, output ----
        float* outL = buffer.getWritePointer (0) + start;
        float* outR = nOut > 1 ? buffer.getWritePointer (1) + start : nullptr;
        for (int i = 0; i < n; ++i)
        {
            const float duck = ducker.process (v[i]);
            float wetL = v[i] + (dlyL[(size_t) i] + revL[(size_t) i]) * duck;
            float wetR = v[i] + (dlyR[(size_t) i] + revR[(size_t) i]) * duck;
            if (dblGain > 0.f)
            {
                float dL, dR;
                doubler.process (v[i], dL, dR);
                wetL += dblGain * dL; wetR += dblGain * dR;
            }
            if (std::fabs (widthV - 1.f) > 0.001f)
            {
                const float mid = 0.5f * (wetL + wetR), side = 0.5f * (wetL - wetR) * widthV;
                wetL = mid + side; wetR = mid - side;
            }

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
            if (outR == nullptr) l = r = 0.5f * (l + r);           // mono output: limit/measure what is actually delivered
            limiter.process (l, r, l, r);
            if (monoCheckOn) l = r = 0.5f * (l + r);
            loudness.push (l, r);
            if (outR != nullptr) { outL[i] = l; outR[i] = r; }
            else outL[i] = l;
            outPeak = std::max (outPeak, std::fabs (l));
            limGrMin = std::min (limGrMin, limiter.grDb());
        }
    }

    if (sawBad)      // something went non-finite: flush all recursive state so it cannot persist
    {
        for (auto* f : { &hpf, &hpf2, &mudBq, &boxBq, &lowShelf, &peak, &highShelf, &lpf, &revHpL, &revHpR, &revLpL, &revLpR }) f->reset();
        compressor.reset(); deesser.reset(); drive.reset(); expander.reset(); delay.reset(); preDelay.reset(); ducker.reset(); doubler.reset();
        limiter.reset(); reverb.reset(); corrector.reset();
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
        sharedBus.momentaryLufs.store (loudness.momentary); sharedBus.shortTermLufs.store (loudness.shortTerm);
        sharedBus.integratedLufs.store (loudness.integrated); sharedBus.outPeakDb.store (loudness.truePeakDb);
        sharedBus.limiterGrDb.store (limGrMin);
    }
    {
        const int g = sharedBus.lufsResetGen.load();
        if (g != handledLufsGen) { handledLufsGen = g; loudness.resetAll(); }
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
