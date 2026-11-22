#pragma once
#include "engine/Instrument.h"
#include <map>

namespace dt
{
/** One source audio file the user dropped into the builder. */
struct SourceSample
{
    juce::File file;
    int mic = micDirect;
    juce::String hitKey;       // filename with mic tokens removed: files sharing a key are one hit
    int fileVelocity = -1;     // parsed from "v3" / "vel3" tokens
    int fileRoundRobin = -1;   // parsed from "rr2" tokens
    float peakDb = -100.0f;
    double sampleRate = 0.0;
    double lengthSeconds = 0.0;
};

struct PlannedHit
{
    std::array<juce::File, numMics> files;
    float peakDb = -100.0f;
    int fileVelocity = -1, fileRoundRobin = -1;
    juce::String key;
};

struct PlannedLayer
{
    int velLo = 1, velHi = 127;
    std::vector<PlannedHit> hits;
};

struct BuildPlan
{
    std::vector<PlannedLayer> layers;
    juce::StringArray warnings;
    int totalHits() const { int n = 0; for (auto& l : layers) n += (int) l.hits.size(); return n; }
};

/**
    Turns a pile of drum one-shots into a multisample trigger instrument (.dtz).

    Mic detection by filename token (case-insensitive, tokens split on space _ - . ( ) ):
        overhead: oh, ohs, ovh, overhead, overheads
        room:     room, rooms, rm, amb, ambient, ambience
        direct:   di, dir, direct, close   (anything without a mic token is direct)
    Files whose names are identical apart from the mic token become one multi-mic hit.

    Layering modes:
        byLoudness — hits are sorted by peak level and split into N velocity layers;
                     the hits inside each layer become round robins.
        byFilename — "v1", "v2"... (or "vel1") tokens choose the layer and "rr1", "rr2"...
                     the round-robin order.
*/
class InstrumentBuilder
{
public:
    enum class Mode { byLoudness, byFilename };

    InstrumentBuilder();

    /** Adds audio files (folders are searched recursively). Returns number added. */
    int addFiles (const juce::Array<juce::File>& filesOrFolders);
    void clear() { samples.clear(); }
    const std::vector<SourceSample>& getSamples() const noexcept { return samples; }

    BuildPlan makePlan (Mode mode, int requestedLayers) const;

    static juce::String describePlan (const BuildPlan& plan);
    static std::unique_ptr<juce::XmlElement> createXml (const BuildPlan& plan, const juce::String& name,
                                                        std::map<juce::File, juce::String>* storedNames = nullptr);
    static bool writePackage (const BuildPlan& plan, const juce::String& name, const juce::File& dest, juce::String& error);

    /** Parses a filename (without extension). Returns the mic and fills key/vel/rr. */
    static int parseFilename (const juce::String& nameNoExt, juce::String& key, int& vel, int& rr);

    static bool isAudioFile (const juce::File& f);

private:
    bool analyse (SourceSample& s);

    juce::AudioFormatManager formatManager;
    std::vector<SourceSample> samples;
};
} // namespace dt
