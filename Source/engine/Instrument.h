#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <vector>
#include <functional>

namespace dt
{
enum Mic { micDirect = 0, micOverhead = 1, micRoom = 2, numMics = 3 };

inline const char* micAttributeName (int mic)
{
    static const char* names[] = { "direct", "overhead", "room" };
    return names[juce::jlimit (0, numMics - 1, mic)];
}

/** One recorded strike: up to three mic perspectives, all at the same sample rate. */
struct Hit
{
    std::array<juce::AudioBuffer<float>, numMics> mic;
    double sampleRate = 44100.0;
    int length = 0;          // longest mic, in samples
    float peakDb = -100.0f;  // analysed loudness (informational)

    bool hasMic (int m) const noexcept { return mic[(size_t) m].getNumSamples() > 0; }
};

/** A velocity layer: a velocity range (1..127) and its round-robin hits. */
struct Layer
{
    int velLo = 1, velHi = 127;
    std::vector<Hit> hits;
};

/**
    A multisample trigger instrument.

    File formats:
      .dtz  — a zip containing instrument.xml + the audio files (what the builder writes)
      .xml  — a bare instrument.xml whose sample paths are relative to the xml's folder

    instrument.xml:
      <DrumTriggerInstrument name="Snare A" version="1">
        <Layer velLo="1" velHi="42">
          <Hit direct="samples/snr_v1_rr1.wav" overhead="samples/snr_v1_rr1_oh.wav" room="..."/>
          <Hit direct="samples/snr_v1_rr2.wav"/>
        </Layer>
        ...
      </DrumTriggerInstrument>
*/
class Instrument : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<Instrument>;
    using StreamOpener = std::function<std::unique_ptr<juce::InputStream> (const juce::String& path)>;

    juce::String name;
    juce::File sourceFile;
    std::vector<Layer> layers;
    std::array<bool, numMics> micPresent {};

    /** Returns the layer index for a 1..127 velocity (nearest layer if no exact range match). */
    int findLayer (int velocity127) const noexcept;
    int totalHits() const noexcept;
    juce::String getSummary() const;

    static Ptr load (const juce::File& file, juce::String& error);
    static Ptr loadFromXml (const juce::XmlElement& xml, const StreamOpener& opener, juce::String& error);
};

juce::File getDefaultInstrumentFolder();
juce::Array<juce::File> findInstrumentsInDefaultFolder();
} // namespace dt
