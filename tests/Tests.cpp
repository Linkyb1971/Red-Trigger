// Headless tests for Red Trigger: DSP, instrument building/loading, sample engine and the full processor.
#include <juce_audio_formats/juce_audio_formats.h>
#include "dsp/Biquad.h"
#include "dsp/TransientDetector.h"
#include "dsp/Dbx160Compressor.h"
#include "dsp/TriggerEQ.h"
#include "engine/SampleEngine.h"
#include "builder/InstrumentBuilder.h"
#if DT_TEST_PROCESSOR
 #include "PluginProcessor.h"
#endif
#include <iostream>

using namespace dt;

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { ++checks; if (! (cond)) { ++failures; std::cout << "  FAIL: " << msg << "  [" #cond "]\n"; } } while (0)
#define CHECK_NEAR(a, b, tol, msg) do { ++checks; const double _a = (a), _b = (b); if (std::abs (_a - _b) > (tol)) { ++failures; \
    std::cout << "  FAIL: " << msg << "  got " << _a << " expected " << _b << " +/- " << (tol) << "\n"; } } while (0)

static constexpr double fs = 44100.0;

/** Decaying-sine drum-like hit added at 'onset'. */
static void addHit (std::vector<float>& x, int onset, float amp, float freq = 180.0f, float decayMs = 60.0f)
{
    const int len = (int) (fs * 0.4);
    for (int i = 0; i < len && onset + i < (int) x.size(); ++i)
    {
        const float t = (float) i / (float) fs;
        x[(size_t) (onset + i)] += amp * std::sin (2.0f * juce::MathConstants<float>::pi * freq * t) * std::exp (-t * 1000.0f / decayMs);
    }
}

static void testBiquad()
{
    std::cout << "Biquad\n";
    Biquad lp, hp;
    lp.setCoeffs (BiquadCoeffs::make (BiquadCoeffs::Type::lowPass, fs, 1000, 0.707));
    hp.setCoeffs (BiquadCoeffs::make (BiquadCoeffs::Type::highPass, fs, 1000, 0.707));
    float yl = 0, yh = 0;
    for (int i = 0; i < 20000; ++i) { yl = lp.process (1.0f); yh = hp.process (1.0f); }
    CHECK_NEAR (yl, 1.0, 1e-3, "LPF DC gain");
    CHECK_NEAR (yh, 0.0, 1e-3, "HPF DC gain");

    // peak filter: +6 dB at centre
    Biquad pk;
    pk.setCoeffs (BiquadCoeffs::make (BiquadCoeffs::Type::peak, fs, 1000, 1.0, 6.0));
    float maxOut = 0;
    for (int i = 0; i < 44100; ++i)
    {
        const float y = pk.process (std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * (float) i / (float) fs));
        if (i > 22050) maxOut = std::max (maxOut, std::abs (y));
    }
    CHECK_NEAR (gainToDb (maxOut), 6.0, 0.1, "Peak EQ +6 dB at centre");
}

static void testDetector()
{
    std::cout << "TransientDetector\n";
    std::vector<float> x ((size_t) (fs * 3), 0.0f);
    juce::Random rnd (1);
    for (auto& s : x) s = (rnd.nextFloat() * 2 - 1) * 0.0005f;   // -66 dB noise floor

    const int onsets[] = { 10000, 40000, 70000, 100000 };
    const float amps[] = { 0.9f, 0.3f, 0.6f, 0.08f };
    for (int i = 0; i < 4; ++i) addHit (x, onsets[i], amps[i]);

    TransientDetector det;
    det.prepare (fs);
    TransientDetector::Params p;
    p.thresholdDb = -30.0f;
    det.setParams (p);

    std::vector<std::pair<int, float>> hits;
    for (int i = 0; i < (int) x.size(); ++i)
    {
        bool hit; float pk;
        det.process (x[(size_t) i], hit, pk);
        if (hit) hits.push_back ({ i, pk });
    }

    CHECK (hits.size() == 4, "4 hits detected (got " << hits.size() << ")");
    const int W = det.getWindowSamples();
    for (size_t i = 0; i < std::min<size_t> (hits.size(), 4); ++i)
    {
        const int crossing = hits[i].first - W;
        CHECK (crossing >= onsets[i] && crossing - onsets[i] < 20,
               "hit " << i << " crossing within 20 samples of onset (delta " << crossing - onsets[i] << ")");
        CHECK_NEAR (hits[i].second, amps[i], amps[i] * 0.1, "hit " << i << " peak");
    }

    // Long ringing hit must not machine-gun retrigger
    std::vector<float> ring ((size_t) fs, 0.0f);
    addHit (ring, 1000, 0.8f, 120.0f, 400.0f);
    det.reset();
    int n = 0;
    for (auto s : ring) { bool h; float pk; det.process (s, h, pk); n += h ? 1 : 0; }
    CHECK (n == 1, "ringing hit triggers once (got " << n << ")");

    // Retrigger time: two hits 30 ms apart, retrigger 40ms -> 1 ; retrigger 10ms -> 2
    for (float rt : { 40.0f, 10.0f })
    {
        std::vector<float> flam ((size_t) fs / 2, 0.0f);
        addHit (flam, 1000, 0.5f, 200.0f, 3.0f);
        addHit (flam, 1000 + (int) (0.030 * fs), 0.5f, 200.0f, 3.0f);
        p.retriggerMs = rt; p.adaptivePct = 0; det.setParams (p); det.reset();
        int c = 0;
        for (auto s : flam) { bool h; float pk; det.process (s, h, pk); c += h ? 1 : 0; }
        CHECK (c == (rt > 30 ? 1 : 2), "retrigger " << rt << "ms -> " << c << " hits");
    }
}

static float runCompSine (Dbx160Compressor& c, const Dbx160Compressor::Params& p, float rmsDb, double seconds)
{
    const float amp = dbToGain (rmsDb) * std::sqrt (2.0f);
    std::vector<float> l ((size_t) (fs * seconds)), r;
    for (size_t i = 0; i < l.size(); ++i) l[i] = amp * std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * (float) i / (float) fs);
    r = l;
    c.process (l.data(), r.data(), (int) l.size(), p);
    return c.getCurrentGainReductionDb();
}

static void testCompressor()
{
    std::cout << "Dbx160Compressor\n";
    Dbx160Compressor c;
    c.prepare (fs);
    Dbx160Compressor::Params p;
    p.thresholdDb = -20.0f;

    struct Case { float slope; float expect; const char* name; };
    for (auto cs : { Case { 0.75f, 7.5f, "4:1" }, Case { 1.0f, 10.0f, "inf:1" }, Case { 2.0f, 20.0f, "-1:1" }, Case { 0.0f, 0.0f, "1:1" } })
    {
        c.reset(); p.slope = cs.slope;
        CHECK_NEAR (runCompSine (c, p, -10.0f, 1.0), cs.expect, 0.6, "static GR at " << cs.name << " (10 dB over)");
    }

    // OverEasy: gentle reduction right at threshold
    CHECK_NEAR (Dbx160Compressor::computeGainReduction (0.0f, 0.75f, true), 0.9375, 1e-4, "OverEasy at threshold");
    CHECK_NEAR (Dbx160Compressor::computeGainReduction (10.0f, 0.75f, true), 7.5, 1e-4, "OverEasy above knee");

    // Release ~120 dB/s : from 7.5 dB GR, after 40 ms of silence -> ~2.7 dB
    c.reset(); p.slope = 0.75f;
    runCompSine (c, p, -10.0f, 0.5);
    std::vector<float> silence ((size_t) (0.040 * fs), 0.0f), s2 = silence;
    c.process (silence.data(), s2.data(), (int) silence.size(), p);
    CHECK_NEAR (c.getCurrentGainReductionDb(), 7.5 - 4.8, 0.6, "release rate 120 dB/s");

    // Program-dependent attack: time to reach 63% of target is shorter for bigger overshoot
    auto attackTime = [&] (float overDb)
    {
        c.reset(); p.slope = 1.0f;
        const float amp = dbToGain (-20.0f + overDb) * std::sqrt (2.0f);
        for (int i = 0; i < (int) fs; ++i)
        {
            float l = amp * std::sin (2.0f * juce::MathConstants<float>::pi * 1000.0f * (float) i / (float) fs), r = l;
            c.process (&l, &r, 1, p);
            if (c.getCurrentGainReductionDb() >= 0.632f * overDb) return 1000.0 * i / fs;
        }
        return 1000.0;
    };
    const double a10 = attackTime (10), a20 = attackTime (20), a30 = attackTime (30);
    std::cout << "    attack (63%): 10dB " << a10 << " ms, 20dB " << a20 << " ms, 30dB " << a30 << " ms\n";
    CHECK (a10 > a20 && a20 > a30, "attack gets faster with more overshoot");
    CHECK (a10 > 10 && a10 < 22 && a20 > 3 && a20 < 9 && a30 < 5, "attack times in dbx 160 ballpark");
}

static void writeWav (const juce::File& f, double rate, int chans, float amp, float freq, float decayMs)
{
    const int len = (int) (rate * 0.3);
    juce::AudioBuffer<float> b (chans, len);
    for (int i = 0; i < len; ++i)
    {
        const float t = (float) i / (float) rate;
        const float v = amp * std::sin (2.0f * juce::MathConstants<float>::pi * freq * t) * std::exp (-t * 1000.0f / decayMs);
        for (int c = 0; c < chans; ++c) b.setSample (c, i, v);
    }
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), rate, (unsigned) chans, 24, {}, 0));
    if (w != nullptr) { os.release(); w->writeFromAudioSampleBuffer (b, 0, len); }
}

/** Builds a test kit: 4 velocities x 3 round robins, direct (44.1k mono) + OH (48k stereo), room in sub-folder. */
static juce::File makeTestKit (const juce::File& dir)
{
    dir.deleteRecursively();
    dir.createDirectory();
    dir.getChildFile ("Room").createDirectory();
    for (int v = 1; v <= 4; ++v)
        for (int rr = 1; rr <= 3; ++rr)
        {
            const float amp = 0.15f * (float) v + 0.01f * (float) rr;
            const auto base = "Snare_v" + juce::String (v) + "_rr" + juce::String (rr);
            writeWav (dir.getChildFile (base + ".wav"), 44100.0, 1, amp, 200.0f, 80.0f);
            writeWav (dir.getChildFile (base + "_OH.wav"), 48000.0, 2, amp * 0.5f, 200.0f, 150.0f);
            writeWav (dir.getChildFile ("Room").getChildFile (base + ".wav"), 44100.0, 2, amp * 0.3f, 200.0f, 300.0f);
        }
    return dir;
}

static Instrument::Ptr testBuilderAndLoader (const juce::File& tempRoot)
{
    std::cout << "InstrumentBuilder + Instrument loader\n";

    juce::String key; int vel, rr;
    CHECK (InstrumentBuilder::parseFilename ("Kick_V3_RR2_OH", key, vel, rr) == micOverhead && vel == 3 && rr == 2, "parse OH/v/rr");
    CHECK (InstrumentBuilder::parseFilename ("kick v3 rr2 Room", key, vel, rr) == micRoom, "parse Room");
    CHECK (InstrumentBuilder::parseFilename ("kick-hard-05", key, vel, rr) == micDirect && vel == -1, "parse plain");
    juce::String k2;
    InstrumentBuilder::parseFilename ("Kick_V3_RR2_DI", k2, vel, rr);
    InstrumentBuilder::parseFilename ("Kick_V3_RR2_OH", key, vel, rr);
    CHECK (key == k2, "mic variants share a hit key");

    const auto kitDir = makeTestKit (tempRoot.getChildFile ("kit"));
    InstrumentBuilder b;
    const int added = b.addFiles ({ kitDir });
    CHECK (added == 36, "36 files added (got " << added << ")");

    auto plan = b.makePlan (InstrumentBuilder::Mode::byLoudness, 4);
    std::cout << InstrumentBuilder::describePlan (plan);
    CHECK (plan.layers.size() == 4, "4 layers by loudness");
    CHECK (plan.totalHits() == 12, "12 multi-mic hits (got " << plan.totalHits() << ")");
    bool allMics = true;
    for (auto& l : plan.layers) for (auto& h : l.hits) for (auto& f : h.files) allMics &= f != juce::File();
    CHECK (allMics, "every hit has direct + OH + room");
    CHECK (plan.layers.front().velLo == 1 && plan.layers.back().velHi == 127, "velocity ranges span 1..127");
    CHECK (plan.layers.front().hits.front().peakDb < plan.layers.back().hits.front().peakDb, "layers sorted soft->loud");

    auto planF = b.makePlan (InstrumentBuilder::Mode::byFilename, 1);
    CHECK (planF.layers.size() == 4 && planF.layers[0].hits.size() == 3, "filename mode: 4 layers x 3 rr");

    const auto dtz = tempRoot.getChildFile ("Test Snare.dtz");
    juce::String err;
    CHECK (InstrumentBuilder::writePackage (plan, "Test Snare", dtz, err), "writePackage: " << err);

    auto inst = Instrument::load (dtz, err);
    CHECK (inst != nullptr, "load .dtz: " << err);
    if (inst == nullptr) return nullptr;
    CHECK (inst->name == "Test Snare", "name");
    CHECK (inst->layers.size() == 4 && inst->totalHits() == 12, "loaded layer/hit counts");
    CHECK (inst->micPresent[0] && inst->micPresent[1] && inst->micPresent[2], "all mics present");
    const auto& h = inst->layers[0].hits[0];
    CHECK (h.mic[micOverhead].getNumChannels() == 2 && h.mic[micDirect].getNumChannels() == 1, "channel counts kept");
    CHECK_NEAR (h.mic[micOverhead].getNumSamples(), h.mic[micDirect].getNumSamples(), 3, "48k OH resampled to 44.1k");

    // bare XML variant with relative paths
    auto xml = InstrumentBuilder::createXml (plan, "Loose");
    juce::String xmlText = xml->toString();
    // point the generated "samples/x.wav" paths at real files by writing the xml next to a samples folder copy
    auto looseDir = tempRoot.getChildFile ("loose");
    looseDir.deleteRecursively();
    looseDir.getChildFile ("samples").createDirectory();
    std::map<juce::File, juce::String> names;
    InstrumentBuilder::createXml (plan, "Loose", &names);
    for (auto& [file, stored] : names) file.copyFileTo (looseDir.getChildFile (stored));
    looseDir.getChildFile ("instrument.xml").replaceWithText (xmlText);
    auto loose = Instrument::load (looseDir.getChildFile ("instrument.xml"), err);
    CHECK (loose != nullptr && loose->totalHits() == 12, "load loose instrument.xml");

    CHECK (inst->findLayer (1) == 0 && inst->findLayer (127) == 3 && inst->findLayer (64) == 2, "findLayer");
    return inst;
}

static void testEngine (Instrument::Ptr inst)
{
    std::cout << "SampleEngine\n";
    if (inst == nullptr) { CHECK (false, "no instrument"); return; }

    SampleEngine e;
    e.prepare (fs);
    e.setInstrument (inst);
    SampleEngine::Settings s;
    s.micGain = { 1.0f, 0.0f, 0.0f };

    auto renderPeak = [&] (int vel)
    {
        juce::AudioBuffer<float> out (2, 4096);
        out.clear();
        e.trigger (vel, 1.0f, s);
        e.render (out, 0, 4096, s);
        e.stopAll();
        return out.getMagnitude (0, 0, 4096);
    };

    // Layer 1 hits: amps 0.16, 0.17, 0.18 (cycled), layer 4: 0.61..0.63
    const float a = renderPeak (10), b = renderPeak (10), c = renderPeak (10), d = renderPeak (10);
    std::cout << "    layer-1 round robin peaks: " << a << " " << b << " " << c << " " << d << "\n";
    CHECK (std::abs (a - b) > 1e-4f && std::abs (b - c) > 1e-4f && std::abs (a - d) < 1e-6f, "round robin cycles through 3 hits");
    CHECK (renderPeak (127) > 0.55f && a < 0.2f, "velocity picks layer");

    // pitch +12 halves the length
    juce::AudioBuffer<float> out (2, 30000);
    out.clear();
    s.pitchSemis = 12.0f;
    e.trigger (127, 1.0f, s);
    e.render (out, 0, 30000, s);
    CHECK (e.getNumActiveVoices() == 0, "voice finishes (pitch up = shorter)");
    s.pitchSemis = 0.0f;

    // length + choke
    s.fullLength = false; s.lengthMs = 20.0f;
    out.clear();
    e.trigger (127, 1.0f, s);
    e.render (out, 0, 4096, s);
    CHECK (e.getNumActiveVoices() == 0 && out.getMagnitude (0, 2000, 2000) < 1e-6f, "length truncates with fade");
    s.fullLength = true;

    s.choke = true;
    e.trigger (127, 1.0f, s);
    e.render (out, 0, 64, s);
    e.trigger (127, 1.0f, s);
    e.render (out, 0, 1000, s);
    CHECK (e.getNumActiveVoices() == 1, "choke stops previous voice");
    e.stopAll();
}

#if DT_TEST_PROCESSOR
static void testProcessor (const juce::File& dtz)
{
    std::cout << "RedTriggerProcessor\n";
    RedTriggerProcessor proc;
    proc.setPlayConfigDetails (1, 2, fs, 512);
    proc.prepareToPlay (fs, 512);

    juce::String msg;
    CHECK (proc.loadInstrument (dtz, msg), "processor loads instrument " << msg);

    auto set = [&] (const char* id, float v) { auto* p = proc.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    set (dt::pid::threshold, -30.0f);
    set (dt::pid::blend, 100.0f);   // trigger only
    set (dt::pid::eqLowGain, 3.0f);
    set (dt::pid::compOn, 1.0f);

    const int W = (int) std::lround (TransientDetector::velocityWindowMs * 0.001 * fs);
    CHECK (proc.getLatencySamples() == W, "latency = velocity window (" << proc.getLatencySamples() << ")");

    std::vector<float> in ((size_t) fs * 2, 0.0f);
    const int onsets[] = { 5000, 30000, 60000 };
    for (int o : onsets) addHit (in, o, 0.7f);

    std::vector<float> outL;
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos < in.size(); pos += 512)
    {
        const int n = (int) std::min<size_t> (512, in.size() - pos);
        juce::AudioBuffer<float> buf (2, n);
        buf.clear();
        buf.copyFrom (0, 0, in.data() + pos, n);
        proc.processBlock (buf, midi);
        for (int i = 0; i < n; ++i) outL.push_back (buf.getSample (0, i));
    }

    CHECK (proc.hitCounter.load() == 3, "3 hits through processor (" << proc.hitCounter.load() << ")");

    // With latency compensation the trigger must start where the dry hit lands in the delayed output.
    for (int o : onsets)
    {
        int first = -1;
        for (int i = o - 200; i < o + 600; ++i)
            if (std::abs (outL[(size_t) i]) > 1e-4f) { first = i; break; }
        const int expected = o + W;   // dry onset in output timeline
        CHECK (first >= expected - 2 && first <= expected + 25,
               "trigger aligned to delayed dry onset (first " << first << " expected ~" << expected << ")");
    }

    // State round trip
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    RedTriggerProcessor proc2;
    proc2.setStateInformation (state.getData(), (int) state.getSize());
    CHECK_NEAR (proc2.apvts.getRawParameterValue (dt::pid::threshold)->load(), -30.0, 0.05, "threshold restored");
    CHECK (proc2.getInstrumentFile() == dtz && proc2.getLoadedInstrument() != nullptr, "instrument restored from state");

    // Dry-only blend reproduces input delayed by W
    set (dt::pid::blend, 0.0f);
    juce::AudioBuffer<float> buf (2, 512);
    buf.clear();
    buf.setSample (0, 10, 1.0f);
    proc.processBlock (buf, midi);
    CHECK_NEAR (buf.getSample (0, 10 + W), 1.0, 1e-6, "dry path delayed by W at blend 0");
    CHECK_NEAR (buf.getSample (1, 10 + W), 1.0, 1e-6, "mono in -> both outputs");
}
#endif

#if DT_TEST_PROCESSOR
 #include "PluginEditor.h"
/** Renders the editor (main page) to a PNG so the layout can be checked without a host. */
static void snapshotEditor (const juce::File& dtz, const juce::File& png)
{
    RedTriggerProcessor proc;
    proc.setPlayConfigDetails (2, 2, fs, 512);
    proc.prepareToPlay (fs, 512);
    juce::String msg;
    proc.loadInstrument (dtz, msg);

    // feed some hits so the detector display and meters show something
    std::vector<float> in ((size_t) fs, 0.0f);
    for (int i = 0; i < 5; ++i) addHit (in, 2000 + i * 8000, 0.15f + 0.15f * (float) i);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    juce::MidiBuffer midi;
    for (size_t pos = 0; pos + 512 <= in.size(); pos += 512)
    {
        juce::AudioBuffer<float> buf (2, 512);
        buf.copyFrom (0, 0, in.data() + pos, 512);
        buf.copyFrom (1, 0, in.data() + pos, 512);
        proc.processBlock (buf, midi);
    }
    if (auto* e = dynamic_cast<RedTriggerEditor*> (ed.get()))
        for (auto* c : e->getChildren())
            if (auto* d = dynamic_cast<dt::ui::DetectorDisplay*> (c)) d->tick();

    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    png.deleteFile();
    juce::FileOutputStream os (png);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::cout << "Snapshot written: " << png.getFullPathName() << "\n";
}
#endif

int main (int argc, char** argv)
{
#if DT_TEST_PROCESSOR
    juce::ScopedJuceInitialiser_GUI init;
#endif
    auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("RedTriggerTests");
    temp.deleteRecursively();
    temp.createDirectory();

    testBiquad();
    testDetector();
    testCompressor();
    auto inst = testBuilderAndLoader (temp);
    testEngine (inst);
#if DT_TEST_PROCESSOR
    testProcessor (temp.getChildFile ("Test Snare.dtz"));
    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
        snapshotEditor (temp.getChildFile ("Test Snare.dtz"), juce::File (argv[2]));
#else
    juce::ignoreUnused (argc, argv);
#endif

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
