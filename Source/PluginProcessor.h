#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "dsp/TransientDetector.h"
#include "dsp/Dbx160Compressor.h"
#include "dsp/TriggerEQ.h"
#include "engine/SampleEngine.h"

/** Lock-free FIFO of decimated detector levels for the scrolling display. */
struct DisplayFifo
{
    struct Point { float level = 0.0f; float threshold = 0.0f; bool hit = false; };
    static constexpr int capacity = 8192;

    void push (const Point& p) noexcept
    {
        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0) data[(size_t) scope.startIndex1] = p;
    }

    int pull (Point* dest, int maxNum) noexcept
    {
        const auto scope = fifo.read (std::min (maxNum, fifo.getNumReady()));
        int k = 0;
        scope.forEach ([&] (int index) { dest[k++] = data[(size_t) index]; });
        return k;
    }

    juce::AbstractFifo fifo { capacity };
    std::array<Point, capacity> data;
};

class DrumTriggerProcessor : public juce::AudioProcessor,
                             public juce::ChangeBroadcaster,
                             private juce::Timer
{
public:
    DrumTriggerProcessor();
    ~DrumTriggerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DrumTrigger"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Message thread. Loads and hot-swaps the instrument. Pass an empty File to unload. */
    bool loadInstrument (const juce::File& file, juce::String& errorOrWarning);
    dt::Instrument::Ptr getLoadedInstrument() const { return loadedInstrument; }
    juce::File getInstrumentFile() const { return instrumentFile; }
    juce::String getLastLoadError() const { return lastLoadError; }

    juce::AudioProcessorValueTreeState apvts;

    // Metering (written on audio thread, read by the UI)
    std::atomic<float> inputPeak { 0.0f }, triggerPeak { 0.0f }, outputPeak { 0.0f }, compGainReduction { 0.0f };
    std::atomic<float> lastVelocity { 0.0f };
    std::atomic<int> hitCounter { 0 }, lastLayer { -1 };
    DisplayFifo display;

private:
    void timerCallback() override;
    float param (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    // DSP
    dt::TransientDetector detector;
    dt::SampleEngine engine;
    dt::TriggerEQ eq;
    dt::Dbx160Compressor comp;

    juce::AudioBuffer<float> triggerBuffer, dryBuffer, detectBuffer;
    std::vector<float> delayLine[2];
    int delayIndex = 0, windowSamples = 1, currentLatency = -1;
    bool lastAlign = true;

    struct Event { int index; float peak; };
    std::array<Event, 256> events;

    int displayDecimation = 64, displayCount = 0;
    float displayMax = 0.0f;
    bool displayHit = false;

    // Instrument hot-swap
    juce::SpinLock instrumentLock;
    dt::Instrument::Ptr pendingInstrument;
    std::atomic<bool> hasPendingInstrument { false };
    juce::ReferenceCountedArray<dt::Instrument> graveyard;   // keeps instruments alive until audio thread lets go
    dt::Instrument::Ptr loadedInstrument;
    juce::File instrumentFile;
    juce::String lastLoadError;

    JUCE_DECLARE_WEAK_REFERENCEABLE (DrumTriggerProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumTriggerProcessor)
};
