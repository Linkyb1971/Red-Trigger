#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace dt;

RedTriggerProcessor::RedTriggerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "RedTriggerState", createParameterLayout())
{
    for (int c = 0; c < maxChannels; ++c)
    {
        chMicParams[(size_t) c]   = apvts.getRawParameterValue (chMic (c));
        chLevelParams[(size_t) c] = apvts.getRawParameterValue (chLevel (c));
        chPanParams[(size_t) c]   = apvts.getRawParameterValue (chPan (c));
        chMuteParams[(size_t) c]  = apvts.getRawParameterValue (chMute (c));
    }
    startTimer (1000);
}

RedTriggerProcessor::~RedTriggerProcessor()
{
    stopTimer();
}

bool RedTriggerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    // mono->mono, stereo->stereo, mono->stereo (trigger samples can be stereo)
    return inOk && outOk && in.size() <= out.size();
}

void RedTriggerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    detector.prepare (sampleRate);
    engine.prepare (sampleRate);
    eq.prepare (sampleRate);
    comp.prepare (sampleRate);

    const int maxBlock = std::max (samplesPerBlock, 512);
    triggerBuffer.setSize (2, maxBlock);
    dryBuffer.setSize (2, maxBlock);
    detectBuffer.setSize (1, maxBlock);

    windowSamples = detector.getWindowSamples();
    for (auto& d : delayLine)
        d.assign ((size_t) windowSamples, 0.0f);
    delayIndex = 0;

    displayDecimation = std::max (1, (int) (sampleRate / 400.0));
    displayCount = 0;

    lastAlign = param (pid::align) > 0.5f;
    currentLatency = lastAlign ? windowSamples : 0;
    setLatencySamples (currentLatency);
}

void RedTriggerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    const int numIn = std::max (1, getTotalNumInputChannels());
    const int numOut = getTotalNumOutputChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);

    if (triggerBuffer.getNumSamples() < n)
    {
        // Host exceeded the promised block size - grow (rare).
        triggerBuffer.setSize (2, n, false, false, true);
        dryBuffer.setSize (2, n, false, false, true);
        detectBuffer.setSize (1, n, false, false, true);
    }

    // ---- instrument hot swap
    if (hasPendingInstrument.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (instrumentLock);
        if (lock.isLocked())
        {
            engine.setInstrument (pendingInstrument);
            pendingInstrument = nullptr;  // graveyard still owns it, so no delete here
            hasPendingInstrument = false;
        }
    }

    // ---- parameters
    TransientDetector::Params dp;
    dp.thresholdDb = param (pid::threshold);
    dp.retriggerMs = param (pid::retrigger);
    dp.adaptivePct = param (pid::adaptive);
    dp.hpfHz = param (pid::detHpf);
    dp.lpfHz = param (pid::detLpf);
    detector.setParams (dp);

    const bool listen = param (pid::listen) > 0.5f;
    const bool align = param (pid::align) > 0.5f;
    const float ceilingDb = std::max (param (pid::ceiling), dp.thresholdDb + 1.0f);
    const float dynamics = param (pid::dynamics) * 0.01f;

    SampleEngine::Settings es;
    for (int c = 0; c < maxChannels; ++c)
    {
        auto& mix = es.channels[(size_t) c];
        const float lvl = chLevelParams[(size_t) c]->load();
        const float g = (lvl <= -59.9f || chMuteParams[(size_t) c]->load() > 0.5f) ? 0.0f : dbToGain (lvl);
        const float pan = chPanParams[(size_t) c]->load();
        mix.gainL = g * std::min (1.0f, 1.0f - pan);   // balance law: centre = unity
        mix.gainR = g * std::min (1.0f, 1.0f + pan);
        mix.mic = juce::roundToInt (chMicParams[(size_t) c]->load());
    }
    es.pitchSemis = param (pid::pitch);
    es.lengthMs = param (pid::length);
    es.fullLength = es.lengthMs >= 1999.0f;
    es.choke = param (pid::choke) > 0.5f;
    es.randomRoundRobin = param (pid::rrMode) > 0.5f;
    es.offsetSamples = (int) std::lround (param (pid::offset) * 0.001 * getSampleRate());

    if (align != lastAlign)
    {
        for (auto& d : delayLine) std::fill (d.begin(), d.end(), 0.0f);
        lastAlign = align;
    }
    const int wantedLatency = align ? windowSamples : 0;
    if (wantedLatency != currentLatency)
    {
        currentLatency = wantedLatency;
        setLatencySamples (currentLatency);
    }

    // ---- input meter + detection
    const float* inL = buffer.getReadPointer (0);
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : nullptr;
    float* det = detectBuffer.getWritePointer (0);
    int numEvents = 0;
    float inPk = 0.0f;
    const float thrLin = dbToGain (dp.thresholdDb);

    for (int i = 0; i < n; ++i)
    {
        const float x = inR != nullptr ? 0.5f * (inL[i] + inR[i]) : inL[i];
        inPk = std::max ({ inPk, std::abs (inL[i]), inR != nullptr ? std::abs (inR[i]) : 0.0f });

        bool hit = false;
        float pk = 0.0f;
        det[i] = detector.process (x, hit, pk);

        if (hit && numEvents < (int) events.size())
            events[(size_t) numEvents++] = { i, pk };

        displayMax = std::max (displayMax, std::abs (det[i]));
        displayHit |= hit;
        if (++displayCount >= displayDecimation)
        {
            display.push ({ displayMax, std::max (thrLin, detector.getEffectiveThreshold()), displayHit });
            displayCount = 0; displayMax = 0.0f; displayHit = false;
        }
    }
    inputPeak = std::max (inputPeak.load(), inPk);

    // ---- render trigger samples, sample-accurately at each event
    triggerBuffer.clear (0, n);
    int pos = 0;
    for (int e = 0; e < numEvents; ++e)
    {
        const auto& ev = events[(size_t) e];
        engine.render (triggerBuffer, pos, ev.index - pos, es);
        pos = ev.index;

        const float norm = juce::jlimit (0.0f, 1.0f, (gainToDb (ev.peak) - dp.thresholdDb) / (ceilingDb - dp.thresholdDb));
        const float velocity = std::max (0.02f, 1.0f - dynamics * (1.0f - norm));
        const int vel127 = juce::jlimit (1, 127, 1 + (int) std::lround (velocity * 126.0f));
        const int layer = engine.trigger (vel127, velocity, es);

        lastVelocity = (float) vel127;
        lastLayer = layer;
        ++hitCounter;
    }
    engine.render (triggerBuffer, pos, n - pos, es);

    float* tL = triggerBuffer.getWritePointer (0);
    float* tR = triggerBuffer.getWritePointer (1);

    // ---- trigger EQ
    if (param (pid::eqOn) > 0.5f)
    {
        TriggerEQ::Params ep;
        ep.hpf = param (pid::eqHpf);
        ep.lowFreq = param (pid::eqLowFreq);   ep.lowGain = param (pid::eqLowGain);
        ep.midFreq = param (pid::eqMidFreq);   ep.midGain = param (pid::eqMidGain);  ep.midQ = param (pid::eqMidQ);
        ep.highFreq = param (pid::eqHighFreq); ep.highGain = param (pid::eqHighGain);
        ep.lpf = param (pid::eqLpf);
        eq.setParams (ep);
        eq.process (tL, tR, n);
    }

    // ---- dbx 160 style compressor
    if (param (pid::compOn) > 0.5f)
    {
        Dbx160Compressor::Params cp;
        cp.thresholdDb = param (pid::compThresh);
        cp.slope = param (pid::compRatio);
        cp.overEasy = param (pid::compOverEasy) > 0.5f;
        cp.outputDb = param (pid::compOutput);
        cp.mix = param (pid::compMix) * 0.01f;
        comp.process (tL, tR, n, cp);
        compGainReduction = std::max (compGainReduction.load(), comp.getGainReductionDb());
    }

    // ---- trigger level / polarity
    {
        const float lvl = param (pid::trigLevel);
        float g = lvl <= -59.9f ? 0.0f : dbToGain (lvl);
        if (param (pid::polarity) > 0.5f) g = -g;
        juce::FloatVectorOperations::multiply (tL, g, n);
        juce::FloatVectorOperations::multiply (tR, g, n);
        triggerPeak = std::max ({ triggerPeak.load(),
                                  juce::FloatVectorOperations::findMaximum (tL, n), -juce::FloatVectorOperations::findMinimum (tL, n),
                                  juce::FloatVectorOperations::findMaximum (tR, n), -juce::FloatVectorOperations::findMinimum (tR, n) });
    }

    // ---- dry path (delayed by the detection window when latency comp is on)
    for (int ch = 0; ch < numIn && ch < 2; ++ch)
    {
        const float* src = buffer.getReadPointer (ch);
        float* dst = dryBuffer.getWritePointer (ch);
        if (align)
        {
            auto& dl = delayLine[(size_t) ch];
            int idx = delayIndex;
            for (int i = 0; i < n; ++i)
            {
                dst[i] = dl[(size_t) idx];
                dl[(size_t) idx] = src[i];
                if (++idx >= windowSamples) idx = 0;
            }
        }
        else
        {
            juce::FloatVectorOperations::copy (dst, src, n);
        }
    }
    if (align)
        delayIndex = (delayIndex + n) % windowSamples;

    // ---- mix
    const float blend = param (pid::blend) * 0.01f;
    const float out = dbToGain (param (pid::outGain));
    const float dryGain = (1.0f - blend) * out;
    const float trigGain = blend * out;
    float outPk = 0.0f;

    for (int ch = 0; ch < numOut; ++ch)
    {
        float* o = buffer.getWritePointer (ch);
        const float* dry = dryBuffer.getReadPointer (std::min (ch, numIn - 1));

        if (listen)
        {
            juce::FloatVectorOperations::copy (o, det, n);
        }
        else
        {
            for (int i = 0; i < n; ++i)
            {
                const float trig = numOut == 1 ? 0.5f * (tL[i] + tR[i]) : (ch == 0 ? tL[i] : tR[i]);
                o[i] = dry[i] * dryGain + trig * trigGain;
            }
        }

        outPk = std::max ({ outPk, juce::FloatVectorOperations::findMaximum (o, n), -juce::FloatVectorOperations::findMinimum (o, n) });
    }
    outputPeak = std::max (outputPeak.load(), outPk);

    renderPreview (buffer, numOut, n);
}

void RedTriggerProcessor::renderPreview (juce::AudioBuffer<float>& buffer, int numOut, int n) noexcept
{
    if (hasPendingPreview.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (previewLock);
        if (lock.isLocked())
        {
            activePreview = pendingPreview;
            pendingPreview = nullptr;
            previewPos = 0.0;
            hasPendingPreview = false;
        }
    }

    if (activePreview == nullptr)
        return;

    const auto& b = activePreview->buffer;
    const double inc = activePreview->sampleRate / getSampleRate();
    const int len = b.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        const int ip = (int) previewPos;
        if (ip + 1 >= len) { activePreview = nullptr; return; }
        const float t = (float) (previewPos - ip);
        for (int ch = 0; ch < numOut; ++ch)
        {
            const int src = std::min (ch, b.getNumChannels() - 1);
            const float* d = b.getReadPointer (src);
            buffer.addSample (ch, i, d[ip] + t * (d[ip + 1] - d[ip]));
        }
        previewPos += inc;
    }
}

void RedTriggerProcessor::previewFile (const juce::File& file)
{
    PreviewSample::Ptr p;
    if (file.existsAsFile())
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        if (std::unique_ptr<juce::AudioFormatReader> reader { fm.createReaderFor (file) })
        {
            p = new PreviewSample();
            const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 20));
            p->buffer.setSize ((int) std::min (2u, reader->numChannels), len);
            reader->read (&p->buffer, 0, len, 0, true, true);
            p->sampleRate = reader->sampleRate;
            previewGraveyard.add (p);
        }
    }

    const juce::SpinLock::ScopedLockType lock (previewLock);
    pendingPreview = p;
    hasPendingPreview = true;
}

bool RedTriggerProcessor::loadInstrument (const juce::File& file, juce::String& message)
{
    Instrument::Ptr inst;
    message.clear();

    if (file != juce::File())
    {
        inst = Instrument::load (file, message);
        if (inst == nullptr)
        {
            lastLoadError = message;
            sendChangeMessage();
            return false;
        }
        graveyard.add (inst);
    }

    {
        const juce::SpinLock::ScopedLockType lock (instrumentLock);
        pendingInstrument = inst;
        hasPendingInstrument = true;
    }

    loadedInstrument = inst;
    instrumentFile = file;
    lastLoadError = message;
    sendChangeMessage();
    return true;
}

void RedTriggerProcessor::timerCallback()
{
    // Free instruments / previews nobody but the graveyard references any more.
    for (int i = graveyard.size(); --i >= 0;)
        if (graveyard.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
            graveyard.remove (i);
    for (int i = previewGraveyard.size(); --i >= 0;)
        if (previewGraveyard.getObjectPointerUnchecked (i)->getReferenceCount() == 1)
            previewGraveyard.remove (i);
}

void RedTriggerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("instrumentPath", instrumentFile.getFullPathName(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void RedTriggerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    const juce::String path = state.getProperty ("instrumentPath").toString();
    apvts.replaceState (state);

    auto load = [this, path]
    {
        if (path.isEmpty())
            return;

        juce::File f (path);
        if (! f.existsAsFile())
        {
            // Session moved to another machine: look for the same file name in the instruments folder.
            auto matches = getDefaultInstrumentFolder().findChildFiles (juce::File::findFiles, true, f.getFileName());
            if (! matches.isEmpty())
                f = matches.getFirst();
        }

        juce::String msg;
        if (! loadInstrument (f, msg))
            instrumentFile = juce::File (path);  // remember so a re-save keeps the reference
    };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        load();
    else
        juce::MessageManager::callAsync ([weak = juce::WeakReference<RedTriggerProcessor> (this), load]
                                         { if (weak != nullptr) load(); });
}

juce::AudioProcessorEditor* RedTriggerProcessor::createEditor()
{
    return new RedTriggerEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RedTriggerProcessor();
}
