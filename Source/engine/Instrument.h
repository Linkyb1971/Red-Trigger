#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <vector>
#include <functional>

namespace dt
{
constexpr int maxChannels = 8;          // mic positions per instrument (Top, Bottom, OH, ...)
constexpr int maxMicsPerChannel = 8;    // alternative mics recorded at the same position

/** A mic position (e.g. "Top") and the mic types recorded there (e.g. "SM57", "MD421"). */
struct ChannelDef
{
    juce::String name;
    juce::StringArray mics;
};

/**
    The structure of an instrument with sample *paths* (no audio). Shared by the
    loader and the editor.

    instrument.xml (version 2):
      <RedTriggerInstrument name="Snare" version="2">
        <Channel name="Top"><Mic name="SM57"/><Mic name="MD421"/></Channel>
        <Channel name="Bottom"><Mic name="SM57"/></Channel>
        <Channel name="OH"><Mic name="C414"/></Channel>
        <Layer velLo="1" velHi="42">
          <Hit peakDb="-12.0">
            <Sample channel="0" mic="0" file="samples/snare_top_sm57_v1_rr1.wav"/>
            <Sample channel="0" mic="1" file="samples/snare_top_md421_v1_rr1.wav"/>
            ...
          </Hit>
        </Layer>
      </RedTriggerInstrument>

    Version 1 files (<Hit direct=".." overhead=".." room=".."/>) are still read.
*/
struct InstrumentDescription
{
    struct DHit
    {
        std::vector<std::vector<juce::String>> paths;   // [channel][mic], empty = none
        float peakDb = -100.0f;
    };

    struct DLayer
    {
        int velLo = -1, velHi = -1;   // -1 = not specified
        std::vector<DHit> hits;
    };

    juce::String name;
    std::vector<ChannelDef> channels;
    std::vector<DLayer> layers;

    static bool fromXml (const juce::XmlElement& xml, InstrumentDescription& out, juce::String& error);
    std::unique_ptr<juce::XmlElement> toXml() const;
};

/** One recorded strike: one buffer per (channel, mic) slot, all at the same sample rate. */
struct Hit
{
    std::vector<juce::AudioBuffer<float>> slots;
    double sampleRate = 44100.0;
    int length = 0;          // longest slot, in samples
    float peakDb = -100.0f;
};

struct Layer
{
    int velLo = 1, velHi = 127;
    std::vector<Hit> hits;   // round robins
};

/** A loaded multisample trigger instrument (.dtz = zip of instrument.xml + samples, or a bare .xml). */
class Instrument : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<Instrument>;
    using StreamOpener = std::function<std::unique_ptr<juce::InputStream> (const juce::String& path)>;

    juce::String name;
    juce::File sourceFile;
    std::vector<ChannelDef> channels;
    std::vector<int> slotBase;   // first slot index of each channel
    int numSlots = 0;
    std::vector<Layer> layers;

    int slotIndex (int channel, int mic) const noexcept { return slotBase[(size_t) channel] + mic; }
    int numChannels() const noexcept { return (int) channels.size(); }

    /** Returns the layer index for a 1..127 velocity (nearest layer if no exact range match). */
    int findLayer (int velocity127) const noexcept;
    int totalHits() const noexcept;
    juce::String getSummary() const;

    static Ptr load (const juce::File& file, juce::String& error);
    static Ptr loadFromXml (const juce::XmlElement& xml, const StreamOpener& opener, juce::String& error);

    /** Opens a .dtz/.xml and returns its XML plus a stream opener for its sample paths. */
    static std::unique_ptr<juce::XmlElement> openPackage (const juce::File& file, StreamOpener& opener, juce::String& error);
};

juce::File getDefaultInstrumentFolder();
juce::Array<juce::File> findInstrumentsInDefaultFolder();
} // namespace dt
