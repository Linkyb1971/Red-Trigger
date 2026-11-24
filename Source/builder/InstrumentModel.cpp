#include "InstrumentModel.h"
#include <set>

namespace dt
{
namespace
{
    struct ChannelAlias { const char* name; std::initializer_list<const char*> tokens; };

    // Common mic-position names found in sample libraries.
    const ChannelAlias channelAliases[] = {
        { "OH",     { "oh", "ohs", "ovh", "overhead", "overheads" } },
        { "Room",   { "room", "rooms", "rm", "amb", "ambient", "ambience" } },
        { "Top",    { "top" } },
        { "Bottom", { "bottom", "btm", "bot" } },
        { "In",     { "in", "inside" } },
        { "Out",    { "out", "outside" } },
        { "Sub",    { "sub", "subkick" } },
        { "Direct", { "di", "dir", "direct", "close" } },
    };

    // Common mic models: any of these tokens becomes the mic name.
    const std::set<juce::String> micTokens {
        "sm57", "57", "sm7", "sm7b", "md421", "421", "md441", "441", "e906", "906", "e602", "602", "i5",
        "beta57", "beta91", "b91", "beta52", "b52", "d112", "d6", "re20", "c414", "414", "km184", "184", "km84",
        "u47", "fet47", "u87", "u67", "r121", "121", "m160", "c12", "4038", "m88", "m201", "sm81", "c451", "451",
        "kms105", "nu47", "ribbon", "dyn", "dynamic", "condenser", "tube", "fet", "akg", "neumann", "coles"
    };

    juce::String micDisplayName (const juce::String& token)
    {
        if (token.containsOnly ("0123456789"))
            return token;
        return token.length() <= 6 ? token.toUpperCase() : token.substring (0, 1).toUpperCase() + token.substring (1);
    }

    int numberAfter (const juce::String& t, const juce::String& prefix)
    {
        if (! t.startsWith (prefix)) return -1;
        auto rest = t.substring (prefix.length());
        if (rest.isEmpty() || ! rest.containsOnly ("0123456789")) return -1;
        return rest.getIntValue();
    }

    /** True if every token of 'name' appears in 'tokens'; adds them to 'used'. */
    bool nameMatches (const juce::String& name, const juce::StringArray& tokens, juce::StringArray& used)
    {
        auto nameTokens = InstrumentModel::tokenise (name);
        if (nameTokens.isEmpty())
            return false;
        for (auto& t : nameTokens)
            if (! tokens.contains (t))
                return false;
        used.addArray (nameTokens);
        return true;
    }
}

InstrumentModel::InstrumentModel()
{
    formatManager.registerBasicFormats();
}

bool InstrumentModel::isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;ogg");
}

juce::StringArray InstrumentModel::tokenise (const juce::String& s)
{
    juce::StringArray t;
    t.addTokens (s.toLowerCase(), " _-.()[]{},+&", "");
    t.removeEmptyStrings();
    return t;
}

bool InstrumentModel::isEmpty() const { return totalHits() == 0; }

int InstrumentModel::totalHits() const
{
    int n = 0;
    for (auto& l : layers) n += (int) l.hits.size();
    return n;
}

//==============================================================================
int InstrumentModel::numColumns() const
{
    int n = 0;
    for (auto& c : channels) n += c.mics.size();
    return n;
}

std::pair<int, int> InstrumentModel::columnToChannelMic (int column) const
{
    for (int c = 0; c < (int) channels.size(); ++c)
    {
        if (column < channels[(size_t) c].mics.size())
            return { c, column };
        column -= channels[(size_t) c].mics.size();
    }
    return { -1, -1 };
}

int InstrumentModel::columnFor (int channel, int mic) const
{
    int col = 0;
    for (int c = 0; c < channel; ++c) col += channels[(size_t) c].mics.size();
    return col + mic;
}

void InstrumentModel::shapeHit (ModelHit& h) const
{
    h.files.resize (channels.size());
    for (size_t c = 0; c < channels.size(); ++c)
        h.files[c].resize ((size_t) channels[c].mics.size());
}

int InstrumentModel::findOrAddChannel (const juce::String& channelName)
{
    for (int c = 0; c < (int) channels.size(); ++c)
        if (channels[(size_t) c].name.equalsIgnoreCase (channelName))
            return c;

    if ((int) channels.size() >= maxChannels)
    {
        messages.addIfNotAlreadyThere ("Maximum of " + juce::String (maxChannels) + " channels reached - '" + channelName + "' skipped.");
        return -1;
    }
    channels.push_back ({ channelName, {} });
    for (auto& l : layers) for (auto& h : l.hits) shapeHit (h);
    return (int) channels.size() - 1;
}

int InstrumentModel::findOrAddMic (int channel, const juce::String& micName)
{
    auto& mics = channels[(size_t) channel].mics;
    for (int m = 0; m < mics.size(); ++m)
        if (mics[m].equalsIgnoreCase (micName))
            return m;

    if (mics.size() >= maxMicsPerChannel)
    {
        messages.addIfNotAlreadyThere ("Maximum of " + juce::String (maxMicsPerChannel) + " mics per channel reached.");
        return -1;
    }
    mics.add (micName);
    for (auto& l : layers) for (auto& h : l.hits) shapeHit (h);
    return mics.size() - 1;
}

void InstrumentModel::renameChannel (int c, const juce::String& n)
{
    if (c >= 0 && c < (int) channels.size() && n.isNotEmpty())
        channels[(size_t) c].name = n;
}

void InstrumentModel::renameMic (int c, int m, const juce::String& n)
{
    if (c >= 0 && c < (int) channels.size() && m >= 0 && m < channels[(size_t) c].mics.size() && n.isNotEmpty())
        channels[(size_t) c].mics.set (m, n);
}

void InstrumentModel::removeChannel (int c)
{
    if (c < 0 || c >= (int) channels.size()) return;
    channels.erase (channels.begin() + c);
    for (auto& l : layers)
    {
        for (auto& h : l.hits)
            if (c < (int) h.files.size()) h.files.erase (h.files.begin() + c);
        compactLayer (l);
    }
}

void InstrumentModel::removeMic (int c, int m)
{
    if (c < 0 || c >= (int) channels.size() || m < 0 || m >= channels[(size_t) c].mics.size()) return;
    if (channels[(size_t) c].mics.size() == 1) { removeChannel (c); return; }
    channels[(size_t) c].mics.remove (m);
    for (auto& l : layers)
    {
        for (auto& h : l.hits)
            if (m < (int) h.files[(size_t) c].size()) h.files[(size_t) c].erase (h.files[(size_t) c].begin() + m);
        compactLayer (l);
    }
}

//==============================================================================
int InstrumentModel::addLayer()
{
    layers.push_back ({});
    evenVelocitySplit();
    return (int) layers.size() - 1;
}

void InstrumentModel::removeLayer (int i)
{
    if (i < 0 || i >= (int) layers.size()) return;
    layers.erase (layers.begin() + i);
    evenVelocitySplit();
}

void InstrumentModel::evenVelocitySplit()
{
    const int n = (int) layers.size();
    for (int i = 0; i < n; ++i)
    {
        layers[(size_t) i].velLo = 1 + (i * 127) / n;
        layers[(size_t) i].velHi = ((i + 1) * 127) / n;
    }
}

float InstrumentModel::peakDb (const juce::File& f) const
{
    if (auto it = peakCache.find (f); it != peakCache.end())
        return it->second;

    float result = -100.0f;
    auto& fm = const_cast<juce::AudioFormatManager&> (formatManager);
    if (std::unique_ptr<juce::AudioFormatReader> reader { fm.createReaderFor (f) })
    {
        const int chans = (int) std::min (2u, reader->numChannels);
        juce::Range<float> ranges[2];
        reader->readMaxLevels (0, reader->lengthInSamples, ranges, chans);
        float peak = 0.0f;
        for (int c = 0; c < chans; ++c)
            peak = std::max ({ peak, std::abs (ranges[c].getStart()), std::abs (ranges[c].getEnd()) });
        result = juce::Decibels::gainToDecibels (peak, -100.0f);
    }
    peakCache[f] = result;
    return result;
}

float InstrumentModel::hitLoudness (const ModelHit& h) const
{
    // loudness of the first channel/mic that has a file (normally the main close mic)
    for (auto& c : h.files)
        for (auto& f : c)
            if (f != juce::File())
                return peakDb (f);
    return -100.0f;
}

void InstrumentModel::sortLayersByLoudness()
{
    std::vector<float> avg;
    for (auto& l : layers)
    {
        float sum = 0.0f;
        for (auto& h : l.hits) sum += hitLoudness (h);
        avg.push_back (l.hits.empty() ? -200.0f : sum / (float) l.hits.size());
    }
    std::vector<size_t> order (layers.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort (order.begin(), order.end(), [&] (size_t a, size_t b) { return avg[a] < avg[b]; });

    std::vector<ModelLayer> sorted;
    for (auto i : order) sorted.push_back (layers[i]);
    layers = std::move (sorted);
    evenVelocitySplit();
}

//==============================================================================
int InstrumentModel::cellCount (int layer, int c, int m) const
{
    return cellFiles (layer, c, m).size();
}

juce::Array<juce::File> InstrumentModel::cellFiles (int layer, int c, int m) const
{
    juce::Array<juce::File> r;
    if (layer >= 0 && layer < (int) layers.size() && c >= 0 && c < (int) channels.size()
        && m >= 0 && m < channels[(size_t) c].mics.size())
        for (auto& h : layers[(size_t) layer].hits)
            if (auto& f = h.files[(size_t) c][(size_t) m]; f != juce::File()) r.add (f);
    return r;
}

void InstrumentModel::compactLayer (ModelLayer& l)
{
    l.hits.erase (std::remove_if (l.hits.begin(), l.hits.end(), [] (const ModelHit& h)
    {
        for (auto& c : h.files) for (auto& f : c) if (f != juce::File()) return false;
        return true;
    }), l.hits.end());
}

void InstrumentModel::setColumn (ModelLayer& l, int c, int m, const juce::Array<juce::File>& files)
{
    for (auto& h : l.hits) h.files[(size_t) c][(size_t) m] = juce::File();
    while ((int) l.hits.size() < files.size())
    {
        l.hits.push_back ({});
        shapeHit (l.hits.back());
    }
    for (int i = 0; i < files.size(); ++i)
        l.hits[(size_t) i].files[(size_t) c][(size_t) m] = files[i];
    compactLayer (l);
}

void InstrumentModel::addToCell (int layer, int c, int m, juce::Array<juce::File> files)
{
    if (layer < 0 || layer >= (int) layers.size() || c < 0 || c >= (int) channels.size()
        || m < 0 || m >= channels[(size_t) c].mics.size())
        return;

    files.sort();
    auto all = cellFiles (layer, c, m);
    for (auto& f : files)
        if (isAudioFile (f) && ! all.contains (f))
            all.add (f);
    setColumn (layers[(size_t) layer], c, m, all);
}

void InstrumentModel::removeFromCell (int layer, int c, int m, int index)
{
    auto files = cellFiles (layer, c, m);
    if (index < 0 || index >= files.size()) return;
    files.remove (index);
    setColumn (layers[(size_t) layer], c, m, files);
}

void InstrumentModel::moveInCell (int layer, int c, int m, int index, int delta)
{
    auto files = cellFiles (layer, c, m);
    const int j = index + delta;
    if (index < 0 || index >= files.size() || j < 0 || j >= files.size()) return;
    files.swap (index, j);
    setColumn (layers[(size_t) layer], c, m, files);
}

void InstrumentModel::clearCell (int layer, int c, int m)
{
    if (layer < 0 || layer >= (int) layers.size()) return;
    setColumn (layers[(size_t) layer], c, m, {});
}

//==============================================================================
std::vector<InstrumentModel::Source> InstrumentModel::collect (const juce::Array<juce::File>& filesOrFolders)
{
    std::vector<Source> out;
    juce::Array<juce::File> loose;
    for (auto& f : filesOrFolders)
    {
        if (f.isDirectory())
        {
            auto found = f.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg");
            found.sort();
            for (auto& x : found) out.push_back ({ x, f });
        }
        else if (isAudioFile (f))
            loose.add (f);
    }

    // Loose files: folder names below their common parent count too, so files picked from
    // ".../Top/SM57" and ".../Top/MD421" are told apart.
    if (! loose.isEmpty())
    {
        auto root = loose.getFirst().getParentDirectory();
        for (auto& f : loose)
            while (f.getParentDirectory() != root && ! f.isAChildOf (root) && root.getParentDirectory() != root)
                root = root.getParentDirectory();
        loose.sort();
        for (auto& f : loose) out.push_back ({ f, root });
    }
    return out;
}

InstrumentModel::Detected InstrumentModel::detect (const juce::File& file, const juce::File& root) const
{
    // tokens from sub-folders (between root and file) + the file name
    juce::StringArray all;
    for (auto dir = file.getParentDirectory(); dir != root && dir.isAChildOf (root); dir = dir.getParentDirectory())
        all.addArray (tokenise (dir.getFileName()), 0);
    all.addArray (tokenise (file.getFileNameWithoutExtension()));

    Detected d;
    juce::StringArray used;

    // 1. channels already defined in the editor (the most specific name wins)
    int bestTokens = 0;
    for (auto& c : channels)
    {
        juce::StringArray u;
        if (nameMatches (c.name, all, u) && u.size() > bestTokens)
        {
            bestTokens = u.size();
            d.channel = c.name;
            used = u;
        }
    }

    // 2. well-known position names
    if (d.channel.isEmpty())
        for (auto& alias : channelAliases)
        {
            for (auto* t : alias.tokens)
                if (all.contains (t)) { d.channel = alias.name; used.add (t); break; }
            if (d.channel.isNotEmpty()) break;
        }

    if (d.channel.isEmpty())
        d.channel = channels.empty() ? juce::String ("Direct") : channels.front().name;

    // 3. mic: names already defined for that channel, then known mic models
    for (auto& c : channels)
        if (c.name.equalsIgnoreCase (d.channel))
            for (auto& m : c.mics)
                if (nameMatches (m, all, used)) { d.mic = m; break; }

    if (d.mic.isEmpty())
        for (auto& t : all)
            if (micTokens.count (t) > 0 && ! used.contains (t)) { d.mic = micDisplayName (t); used.add (t); break; }

    if (d.mic.isEmpty())
    {
        d.mic = "Default";
        for (auto& c : channels)
            if (c.name.equalsIgnoreCase (d.channel) && c.mics.size() == 1)
                d.mic = c.mics[0];
    }

    // 4. velocity / round-robin tags; the hit key is every token not used for channel/mic
    juce::StringArray keyTokens;
    for (auto& t : all)
    {
        if (used.contains (t)) continue;
        if (int v = numberAfter (t, "vel"); v >= 0)      d.velocity = v;
        else if (int v2 = numberAfter (t, "v"); v2 >= 0) d.velocity = v2;
        else if (int r = numberAfter (t, "rr"); r >= 0)  d.roundRobin = r;
        keyTokens.add (t);
    }
    d.key = keyTokens.joinIntoString ("_");
    return d;
}

std::vector<InstrumentModel::ProtoHit> InstrumentModel::groupIntoHits (const std::vector<Source>& sources, int forceChannel, int forceMic)
{
    std::vector<ProtoHit> hits;
    std::map<juce::String, size_t> byKey;

    for (auto& s : sources)
    {
        auto d = detect (s.file, s.root);
        int c = forceChannel, m = forceMic;
        if (c < 0)
        {
            c = findOrAddChannel (d.channel);
            if (c < 0) continue;
            m = findOrAddMic (c, d.mic);
            if (m < 0) continue;
        }

        // dropping on a single column: every file is its own hit
        const auto key = forceChannel >= 0 ? s.file.getFullPathName() : d.key;
        auto it = byKey.find (key);
        if (it == byKey.end())
        {
            ProtoHit p;
            p.key = key;
            p.velocity = d.velocity;
            p.roundRobin = d.roundRobin;
            it = byKey.emplace (key, hits.size()).first;
            hits.push_back (p);
        }

        auto& h = hits[it->second].hit;
        shapeHit (h);
        auto& slot = h.files[(size_t) c][(size_t) m];
        if (slot != juce::File())
        {
            messages.add ("Two files for the same hit and mic (" + channels[(size_t) c].name + " / "
                          + channels[(size_t) c].mics[m] + "): kept " + slot.getFileName() + ", skipped " + s.file.getFileName());
            continue;
        }
        slot = s.file;
    }

    for (auto& p : hits) shapeHit (p.hit);
    return hits;
}

void InstrumentModel::arrangeIntoLayers (std::vector<ProtoHit>& hits, Layering mode, int numLayers, std::vector<ModelLayer>& out)
{
    out.clear();
    if (hits.empty()) return;

    if (mode == Layering::byFilename)
    {
        const bool allTagged = std::all_of (hits.begin(), hits.end(), [] (const ProtoHit& h) { return h.velocity >= 0; });
        if (! allTagged)
        {
            messages.add ("Some files have no v# tag - sorted by loudness instead.");
            mode = Layering::byLoudness;
        }
        else
        {
            std::map<int, std::vector<ProtoHit*>> groups;
            for (auto& h : hits) groups[h.velocity].push_back (&h);
            for (auto& [vel, group] : groups)
            {
                std::stable_sort (group.begin(), group.end(), [] (auto* a, auto* b) { return a->roundRobin < b->roundRobin; });
                ModelLayer layer;
                for (auto* p : group) layer.hits.push_back (p->hit);
                out.push_back (std::move (layer));
            }
        }
    }

    if (mode == Layering::byLoudness)
    {
        std::vector<float> loud;
        for (auto& h : hits) loud.push_back (hitLoudness (h.hit));
        std::vector<size_t> order (hits.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort (order.begin(), order.end(), [&] (size_t a, size_t b) { return loud[a] < loud[b]; });

        const int n = (int) hits.size();
        const int L = juce::jlimit (1, n, numLayers);
        out.resize ((size_t) L);
        for (int i = 0; i < n; ++i)
            out[(size_t) ((i * L) / n)].hits.push_back (hits[order[(size_t) i]].hit);
    }

    if (out.size() > 127)
    {
        messages.add ("More than 127 layers - extra layers merged into the top layer.");
        for (size_t i = 127; i < out.size(); ++i)
            for (auto& h : out[i].hits) out[126].hits.push_back (h);
        out.resize (127);
    }
}

void InstrumentModel::autoImport (const juce::Array<juce::File>& filesOrFolders, Layering mode, int numLayers)
{
    messages.clear();
    auto hits = groupIntoHits (collect (filesOrFolders), -1, -1);

    // anything already in the grid is kept and re-layered together with the new hits
    for (auto& l : layers)
        for (auto& h : l.hits)
        {
            ProtoHit p;
            p.hit = h;
            shapeHit (p.hit);
            hits.push_back (p);
        }

    arrangeIntoLayers (hits, mode, numLayers, layers);
    for (auto& l : layers) for (auto& h : l.hits) shapeHit (h);
    evenVelocitySplit();
}

void InstrumentModel::importToLayer (int layer, const juce::Array<juce::File>& filesOrFolders)
{
    messages.clear();
    if (layer < 0 || layer >= (int) layers.size()) return;

    auto hits = groupIntoHits (collect (filesOrFolders), -1, -1);
    std::stable_sort (hits.begin(), hits.end(), [] (const ProtoHit& a, const ProtoHit& b) { return a.roundRobin < b.roundRobin; });

    auto& l = layers[(size_t) layer];
    for (auto& h : l.hits) shapeHit (h);
    for (auto& p : hits)
    {
        l.hits.push_back (p.hit);   // keeps all mics of one strike on the same round robin
        shapeHit (l.hits.back());
    }
}

void InstrumentModel::importToColumn (int c, int m, const juce::Array<juce::File>& filesOrFolders, Layering mode, int numLayers)
{
    messages.clear();
    if (c < 0 || c >= (int) channels.size() || m < 0 || m >= channels[(size_t) c].mics.size()) return;

    auto hits = groupIntoHits (collect (filesOrFolders), c, m);
    std::vector<ModelLayer> arranged;
    arrangeIntoLayers (hits, mode, layers.empty() ? numLayers : (int) layers.size(), arranged);

    const bool hadLayers = ! layers.empty();
    while (layers.size() < arranged.size())
        layers.push_back ({});

    for (size_t i = 0; i < arranged.size(); ++i)
    {
        juce::Array<juce::File> files;
        for (auto& h : arranged[i].hits) files.add (h.files[(size_t) c][(size_t) m]);
        setColumn (layers[i], c, m, files);
    }
    if (! hadLayers || layers.size() != arranged.size())
        evenVelocitySplit();
}

//==============================================================================
bool InstrumentModel::writePackage (const juce::File& dest, juce::String& error) const
{
    if (isEmpty())
    {
        error = "Nothing to save - add some samples first.";
        return false;
    }

    InstrumentDescription desc;
    desc.name = name.isNotEmpty() ? name : dest.getFileNameWithoutExtension();
    desc.channels = channels;

    std::map<juce::File, juce::String> stored;
    std::set<juce::String> used;
    auto storedName = [&] (const juce::File& f)
    {
        if (auto it = stored.find (f); it != stored.end()) return it->second;
        auto base = f.getFileNameWithoutExtension().replaceCharacters (" \\/:", "____");
        auto ext = f.getFileExtension().toLowerCase();
        auto candidate = "samples/" + base + ext;
        for (int n = 2; used.count (candidate.toLowerCase()) > 0; ++n)
            candidate = "samples/" + base + "_" + juce::String (n) + ext;
        used.insert (candidate.toLowerCase());
        stored[f] = candidate;
        return candidate;
    };

    for (auto& l : layers)
    {
        if (l.hits.empty()) continue;
        InstrumentDescription::DLayer dl;
        dl.velLo = l.velLo;
        dl.velHi = l.velHi;
        for (auto& h : l.hits)
        {
            InstrumentDescription::DHit dh;
            dh.peakDb = hitLoudness (h);
            dh.paths.resize (channels.size());
            for (size_t c = 0; c < channels.size(); ++c)
            {
                dh.paths[c].resize ((size_t) channels[c].mics.size());
                for (size_t m = 0; m < dh.paths[c].size(); ++m)
                    if (c < h.files.size() && m < h.files[c].size() && h.files[c][m] != juce::File())
                        dh.paths[c][m] = storedName (h.files[c][m]);
            }
            dl.hits.push_back (std::move (dh));
        }
        desc.layers.push_back (std::move (dl));
    }

    juce::ZipFile::Builder zip;
    for (auto& [file, path] : stored)
    {
        if (! file.existsAsFile())
        {
            error = "Source file is missing: " + file.getFullPathName();
            return false;
        }
        zip.addFile (file, 0, path);   // stored uncompressed: fast loading, audio barely compresses
    }

    const auto xmlText = desc.toXml()->toString();
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
            out.flush();
            temp.deleteFile();
            error = "Failed while writing the instrument package.";
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

bool InstrumentModel::loadFromFile (const juce::File& instrumentFile, const juce::File& cacheDir, juce::String& error)
{
    juce::File baseDir = instrumentFile.getParentDirectory();
    std::unique_ptr<juce::XmlElement> xml;

    if (instrumentFile.hasFileExtension ("dtz") || instrumentFile.hasFileExtension ("zip"))
    {
        // Samples must exist as real files to be edited and re-saved, so unpack into a cache folder.
        baseDir = cacheDir.getChildFile (juce::File::createLegalFileName (instrumentFile.getFileNameWithoutExtension()));
        baseDir.deleteRecursively();
        baseDir.createDirectory();
        juce::ZipFile zip (instrumentFile);
        if (zip.getNumEntries() == 0 || zip.uncompressTo (baseDir).failed())
        {
            error = "Couldn't unpack " + instrumentFile.getFileName();
            return false;
        }
        xml = juce::parseXML (baseDir.getChildFile ("instrument.xml"));
    }
    else
    {
        xml = juce::parseXML (instrumentFile);
    }

    if (xml == nullptr)
    {
        error = "Couldn't read the instrument description in " + instrumentFile.getFileName();
        return false;
    }

    InstrumentDescription desc;
    if (! InstrumentDescription::fromXml (*xml, desc, error))
        return false;

    clear();
    name = desc.name.isNotEmpty() ? desc.name : instrumentFile.getFileNameWithoutExtension();
    channels = desc.channels;
    for (auto& dl : desc.layers)
    {
        ModelLayer l;
        l.velLo = dl.velLo > 0 ? dl.velLo : 1;
        l.velHi = dl.velHi > 0 ? dl.velHi : 127;
        for (auto& dh : dl.hits)
        {
            ModelHit h;
            shapeHit (h);
            for (size_t c = 0; c < dh.paths.size() && c < h.files.size(); ++c)
                for (size_t m = 0; m < dh.paths[c].size() && m < h.files[c].size(); ++m)
                    if (dh.paths[c][m].isNotEmpty())
                        h.files[c][m] = baseDir.getChildFile (dh.paths[c][m]);
            l.hits.push_back (std::move (h));
        }
        layers.push_back (std::move (l));
    }
    if (std::all_of (desc.layers.begin(), desc.layers.end(), [] (auto& l) { return l.velLo < 0; }))
        evenVelocitySplit();
    return true;
}
} // namespace dt
