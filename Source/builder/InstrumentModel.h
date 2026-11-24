#pragma once
#include "engine/Instrument.h"
#include <map>

namespace dt
{
/**
    Editable instrument used by the Instrument Editor page.

    Grid: rows = velocity layers, columns = (channel, mic) pairs.
    Each layer holds hits (round robins); each hit has at most one file per (channel, mic).
    Round robin k of one mic is the same physical strike as round robin k of every other
    mic in that layer, so switching mics in the plugin keeps the performance identical.
*/
class InstrumentModel
{
public:
    struct ModelHit { std::vector<std::vector<juce::File>> files; };   // [channel][mic]
    struct ModelLayer { int velLo = 1, velHi = 127; std::vector<ModelHit> hits; };
    enum class Layering { byLoudness, byFilename };

    /** What the filename/folder parser found for one file. */
    struct Detected
    {
        juce::String channel, mic, key;
        int velocity = -1, roundRobin = -1;
    };

    InstrumentModel();

    juce::String name;
    std::vector<ChannelDef> channels;
    std::vector<ModelLayer> layers;
    juce::StringArray messages;   // warnings from the last operation

    void clear() { channels.clear(); layers.clear(); messages.clear(); }
    bool isEmpty() const;
    int totalHits() const;

    // ---- columns
    int numColumns() const;
    std::pair<int, int> columnToChannelMic (int column) const;
    int columnFor (int channel, int mic) const;

    // ---- channels / mics
    int findOrAddChannel (const juce::String& channelName);
    int findOrAddMic (int channel, const juce::String& micName);
    void renameChannel (int channel, const juce::String& newName);
    void renameMic (int channel, int mic, const juce::String& newName);
    void removeChannel (int channel);
    void removeMic (int channel, int mic);

    // ---- layers
    int addLayer();
    void removeLayer (int layer);
    void evenVelocitySplit();
    void sortLayersByLoudness();

    // ---- cells
    int cellCount (int layer, int channel, int mic) const;
    juce::Array<juce::File> cellFiles (int layer, int channel, int mic) const;
    void addToCell (int layer, int channel, int mic, juce::Array<juce::File> files);
    void removeFromCell (int layer, int channel, int mic, int index);
    void moveInCell (int layer, int channel, int mic, int index, int delta);
    void clearCell (int layer, int channel, int mic);

    // ---- drag & drop imports
    /** Adds the files to the grid, detecting channel/mic per file, and re-layers everything. */
    void autoImport (const juce::Array<juce::File>& filesOrFolders, Layering, int numLayers);
    /** Adds the files to one layer, detecting channel/mic per file. */
    void importToLayer (int layer, const juce::Array<juce::File>& filesOrFolders);
    /** Spreads the files over the layers of one (channel, mic) column. */
    void importToColumn (int channel, int mic, const juce::Array<juce::File>& filesOrFolders, Layering, int numLayers);

    Detected detect (const juce::File& file, const juce::File& root) const;
    float peakDb (const juce::File& f) const;

    // ---- persistence
    bool writePackage (const juce::File& dest, juce::String& error) const;
    /** Opens a saved instrument for editing (.dtz is unpacked into cacheDir). */
    bool loadFromFile (const juce::File& instrumentFile, const juce::File& cacheDir, juce::String& error);

    static bool isAudioFile (const juce::File& f);
    static juce::StringArray tokenise (const juce::String& s);

private:
    struct Source { juce::File file, root; };
    struct ProtoHit { ModelHit hit; int velocity = -1, roundRobin = -1; juce::String key; };

    static std::vector<Source> collect (const juce::Array<juce::File>& filesOrFolders);
    void shapeHit (ModelHit& h) const;
    void compactLayer (ModelLayer& l);
    void setColumn (ModelLayer& l, int channel, int mic, const juce::Array<juce::File>& files);
    float hitLoudness (const ModelHit& h) const;
    std::vector<ProtoHit> groupIntoHits (const std::vector<Source>& sources, int forceChannel, int forceMic);
    void arrangeIntoLayers (std::vector<ProtoHit>& hits, Layering, int numLayers, std::vector<ModelLayer>& out);

    juce::AudioFormatManager formatManager;
    mutable std::map<juce::File, float> peakCache;
};
} // namespace dt
