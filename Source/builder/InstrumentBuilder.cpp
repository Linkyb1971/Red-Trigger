#include "InstrumentBuilder.h"
#include <map>
#include <set>

namespace dt
{
InstrumentBuilder::InstrumentBuilder()
{
    formatManager.registerBasicFormats();
}

bool InstrumentBuilder::isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;ogg");
}

int InstrumentBuilder::parseFilename (const juce::String& nameNoExt, juce::String& key, int& vel, int& rr)
{
    static const juce::StringArray ohTokens   { "oh", "ohs", "ovh", "overhead", "overheads" };
    static const juce::StringArray roomTokens { "room", "rooms", "rm", "amb", "ambient", "ambience" };
    static const juce::StringArray dirTokens  { "di", "dir", "direct", "close" };

    juce::StringArray tokens;
    tokens.addTokens (nameNoExt.toLowerCase(), " _-.()[]", "");
    tokens.removeEmptyStrings();

    int mic = micDirect;
    vel = -1; rr = -1;
    juce::StringArray keyTokens;

    auto numberAfter = [] (const juce::String& t, const juce::String& prefix) -> int
    {
        if (! t.startsWith (prefix)) return -1;
        auto rest = t.substring (prefix.length());
        if (rest.isEmpty() || ! rest.containsOnly ("0123456789")) return -1;
        return rest.getIntValue();
    };

    for (auto& t : tokens)
    {
        if (ohTokens.contains (t))   { mic = micOverhead; continue; }
        if (roomTokens.contains (t)) { mic = micRoom;     continue; }
        if (dirTokens.contains (t))  { mic = micDirect;   continue; }

        if (int v = numberAfter (t, "vel"); v >= 0)     vel = v;
        else if (int v2 = numberAfter (t, "v"); v2 >= 0) vel = v2;
        else if (int r = numberAfter (t, "rr"); r >= 0)  rr = r;

        keyTokens.add (t);
    }

    key = keyTokens.joinIntoString ("_");
    return mic;
}

bool InstrumentBuilder::analyse (SourceSample& s)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (s.file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    s.sampleRate = reader->sampleRate;
    s.lengthSeconds = (double) reader->lengthInSamples / reader->sampleRate;

    const int chans = (int) std::min (2u, reader->numChannels);
    juce::Range<float> ranges[2];
    reader->readMaxLevels (0, reader->lengthInSamples, ranges, chans);

    float peak = 0.0f;
    for (int c = 0; c < chans; ++c)
        peak = std::max ({ peak, std::abs (ranges[c].getStart()), std::abs (ranges[c].getEnd()) });

    s.peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
    return true;
}

int InstrumentBuilder::addFiles (const juce::Array<juce::File>& filesOrFolders)
{
    juce::Array<juce::File> files;
    for (auto& f : filesOrFolders)
    {
        if (f.isDirectory())
            files.addArray (f.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg"));
        else if (isAudioFile (f))
            files.add (f);
    }
    files.sort();

    int added = 0;
    for (auto& f : files)
    {
        const bool already = std::any_of (samples.begin(), samples.end(),
                                          [&] (const SourceSample& s) { return s.file == f; });
        if (already)
            continue;

        SourceSample s;
        s.file = f;
        s.mic = parseFilename (f.getFileNameWithoutExtension(), s.hitKey, s.fileVelocity, s.fileRoundRobin);
        if (analyse (s))
        {
            samples.push_back (s);
            ++added;
        }
    }
    return added;
}

BuildPlan InstrumentBuilder::makePlan (Mode mode, int requestedLayers) const
{
    BuildPlan plan;

    // 1. group mic files into hits
    std::vector<PlannedHit> hits;
    std::map<juce::String, size_t> byKey;
    for (auto& s : samples)
    {
        // Files in a folder named after a mic (".../OH/kick_v1.wav") take that mic and merge
        // with their twins in the parent folder. Otherwise the folder is part of the key so
        // identically named files from different folders stay separate hits.
        const auto parent = s.file.getParentDirectory();
        juce::String folderToken; int fv, fr;
        const int folderMic = parseFilename (parent.getFileName(), folderToken, fv, fr);
        const bool folderIsMic = folderToken.isEmpty() && folderMic != micDirect;

        const auto folderKey = (folderIsMic ? parent.getParentDirectory() : parent).getFullPathName();
        const int mic = (folderIsMic && s.mic == micDirect) ? folderMic : s.mic;

        const auto key = folderKey + "|" + s.hitKey;
        auto it = byKey.find (key);
        if (it == byKey.end())
        {
            PlannedHit h;
            h.key = s.hitKey;
            h.fileVelocity = s.fileVelocity;
            h.fileRoundRobin = s.fileRoundRobin;
            byKey[key] = hits.size();
            hits.push_back (h);
            it = byKey.find (key);
        }

        auto& h = hits[it->second];
        if (h.files[(size_t) mic] != juce::File())
        {
            plan.warnings.add ("Duplicate " + juce::String (micAttributeName (mic)) + " mic for hit '" + s.hitKey
                               + "' - ignored " + s.file.getFileName());
            continue;
        }
        h.files[(size_t) mic] = s.file;
        if (mic == micDirect || h.peakDb <= -100.0f)
            h.peakDb = s.peakDb;
    }

    if (hits.empty())
        return plan;

    // 2. arrange into layers
    if (mode == Mode::byFilename)
    {
        const bool allTagged = std::all_of (hits.begin(), hits.end(), [] (const PlannedHit& h) { return h.fileVelocity >= 0; });
        if (! allTagged)
        {
            plan.warnings.add ("Some files have no v# token - falling back to loudness sorting.");
            mode = Mode::byLoudness;
        }
        else
        {
            std::map<int, std::vector<PlannedHit>> groups;
            for (auto& h : hits)
                groups[h.fileVelocity].push_back (h);

            for (auto& [vel, group] : groups)
            {
                std::stable_sort (group.begin(), group.end(), [] (const PlannedHit& a, const PlannedHit& b)
                                  { return a.fileRoundRobin < b.fileRoundRobin; });
                PlannedLayer layer;
                layer.hits = group;
                plan.layers.push_back (layer);
            }
        }
    }

    if (mode == Mode::byLoudness)
    {
        std::stable_sort (hits.begin(), hits.end(), [] (const PlannedHit& a, const PlannedHit& b)
                          { return a.peakDb < b.peakDb; });

        const int n = (int) hits.size();
        const int numLayers = juce::jlimit (1, n, requestedLayers);
        plan.layers.resize ((size_t) numLayers);
        for (int i = 0; i < n; ++i)
            plan.layers[(size_t) ((i * numLayers) / n)].hits.push_back (hits[(size_t) i]);
    }

    // 3. velocity ranges: even split of 1..127
    const int L = (int) plan.layers.size();
    if (L > 127)
    {
        plan.warnings.add ("More than 127 layers - extra layers merged into the top layer.");
        for (int i = 127; i < L; ++i)
            for (auto& h : plan.layers[(size_t) i].hits)
                plan.layers[126].hits.push_back (h);
        plan.layers.resize (127);
    }
    const int numL = (int) plan.layers.size();
    for (int i = 0; i < numL; ++i)
    {
        plan.layers[(size_t) i].velLo = 1 + (i * 127) / numL;
        plan.layers[(size_t) i].velHi = ((i + 1) * 127) / numL;
    }

    return plan;
}

juce::String InstrumentBuilder::describePlan (const BuildPlan& plan)
{
    juce::String s;
    if (plan.layers.empty())
        return "No samples yet.\n\nDrop audio files or folders here (WAV/AIFF/FLAC).\n\n"
               "Name mic files with tokens like _OH / _Room / _DI so they\n"
               "merge into multi-mic hits, e.g.  snare_v3_rr2_OH.wav";

    s << "Velocity layers: " << (int) plan.layers.size() << "    Hits: " << plan.totalHits() << "\n\n";
    s << "Layer  Vel      RR  Mics          Peak (dB)\n";
    s << "-----  -------  --  ------------  ---------------\n";

    for (size_t i = 0; i < plan.layers.size(); ++i)
    {
        const auto& l = plan.layers[i];
        bool mics[numMics] {};
        float lo = 0.0f, hi = -200.0f;
        bool first = true;
        for (auto& h : l.hits)
        {
            for (int m = 0; m < numMics; ++m)
                mics[m] |= h.files[(size_t) m] != juce::File();
            if (first) { lo = hi = h.peakDb; first = false; }
            lo = std::min (lo, h.peakDb); hi = std::max (hi, h.peakDb);
        }

        juce::String micStr;
        micStr << (mics[0] ? "D " : "- ") << (mics[1] ? "OH " : "-- ") << (mics[2] ? "Rm" : "--");

        s << juce::String ((int) i + 1).paddedLeft (' ', 5) << "  "
          << (juce::String (l.velLo) + "-" + juce::String (l.velHi)).paddedRight (' ', 7) << "  "
          << juce::String ((int) l.hits.size()).paddedLeft (' ', 2) << "  "
          << micStr.paddedRight (' ', 12) << "  "
          << juce::String (lo, 1) << " .. " << juce::String (hi, 1) << "\n";
    }

    if (! plan.warnings.isEmpty())
        s << "\nWarnings:\n  " << plan.warnings.joinIntoString ("\n  ") << "\n";

    return s;
}

std::unique_ptr<juce::XmlElement> InstrumentBuilder::createXml (const BuildPlan& plan, const juce::String& name,
                                                                std::map<juce::File, juce::String>* storedNames)
{
    auto root = std::make_unique<juce::XmlElement> ("DrumTriggerInstrument");
    root->setAttribute ("name", name);
    root->setAttribute ("version", 1);

    std::map<juce::File, juce::String> localNames;
    auto& names = storedNames != nullptr ? *storedNames : localNames;
    std::set<juce::String> used;

    auto storedNameFor = [&] (const juce::File& f) -> juce::String
    {
        if (auto it = names.find (f); it != names.end())
            return it->second;

        auto base = f.getFileNameWithoutExtension().replaceCharacters (" \\/:", "____");
        auto ext = f.getFileExtension().toLowerCase();
        auto candidate = "samples/" + base + ext;
        for (int n = 2; used.count (candidate.toLowerCase()) > 0; ++n)
            candidate = "samples/" + base + "_" + juce::String (n) + ext;
        used.insert (candidate.toLowerCase());
        names[f] = candidate;
        return candidate;
    };

    for (auto& l : plan.layers)
    {
        auto* lx = root->createNewChildElement ("Layer");
        lx->setAttribute ("velLo", l.velLo);
        lx->setAttribute ("velHi", l.velHi);

        for (auto& h : l.hits)
        {
            auto* hx = lx->createNewChildElement ("Hit");
            for (int m = 0; m < numMics; ++m)
                if (h.files[(size_t) m] != juce::File())
                    hx->setAttribute (micAttributeName (m), storedNameFor (h.files[(size_t) m]));
            hx->setAttribute ("peakDb", juce::String (h.peakDb, 2));
        }
    }
    return root;
}

bool InstrumentBuilder::writePackage (const BuildPlan& plan, const juce::String& name, const juce::File& dest,
                                      juce::String& error)
{
    if (plan.layers.empty())
    {
        error = "Nothing to save - add some samples first.";
        return false;
    }

    std::map<juce::File, juce::String> storedNames;
    auto xml = createXml (plan, name, &storedNames);

    juce::ZipFile::Builder zip;
    for (auto& [file, stored] : storedNames)
    {
        if (! file.existsAsFile())
        {
            error = "Source file vanished: " + file.getFullPathName();
            return false;
        }
        zip.addFile (file, 0, stored);   // stored uncompressed: fast loading, audio barely compresses
    }

    const auto xmlText = xml->toString();
    zip.addEntry (new juce::MemoryInputStream (xmlText.toRawUTF8(), xmlText.getNumBytesAsUTF8(), true),
                  6, "instrument.xml", juce::Time::getCurrentTime());

    dest.getParentDirectory().createDirectory();
    auto temp = dest.getSiblingFile (dest.getFileName() + ".tmp");
    temp.deleteFile();
    {
        juce::FileOutputStream out (temp);
        if (! out.openedOk())
        {
            error = "Couldn't write to " + dest.getFullPathName();
            return false;
        }
        if (! zip.writeToStream (out, nullptr))
        {
            error = "Failed while writing the instrument package.";
            out.flush();
            temp.deleteFile();
            return false;
        }
        out.flush();
    }

    if (! temp.moveFileTo (dest))
    {
        temp.deleteFile();
        error = "Couldn't replace " + dest.getFullPathName();
        return false;
    }
    return true;
}
} // namespace dt
