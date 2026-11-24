#include "Instrument.h"
#include <climits>

namespace dt
{
namespace
{
    constexpr double maxSampleSeconds = 30.0;

    bool readAudio (juce::AudioFormatManager& fm, std::unique_ptr<juce::InputStream> stream,
                    juce::AudioBuffer<float>& dest, double& sampleRate)
    {
        if (stream == nullptr)
            return false;

        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (std::move (stream)));
        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0)
            return false;

        const auto len = (int) std::min<juce::int64> (reader->lengthInSamples,
                                                      (juce::int64) (maxSampleSeconds * reader->sampleRate));
        const int chans = (int) std::min (2u, reader->numChannels);
        dest.setSize (chans, len);
        reader->read (&dest, 0, len, 0, true, chans > 1);
        sampleRate = reader->sampleRate;
        return true;
    }

    void resample (juce::AudioBuffer<float>& buffer, double fromRate, double toRate)
    {
        if (std::abs (fromRate - toRate) < 0.5 || buffer.getNumSamples() == 0)
            return;

        const double ratio = fromRate / toRate;
        const int outLen = (int) std::floor (buffer.getNumSamples() / ratio);
        juce::AudioBuffer<float> out (buffer.getNumChannels(), outLen);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio, buffer.getReadPointer (ch), out.getWritePointer (ch), outLen,
                            buffer.getNumSamples(), 0);
        }
        buffer = std::move (out);
    }
}

//==============================================================================
bool InstrumentDescription::fromXml (const juce::XmlElement& xml, InstrumentDescription& out, juce::String& error)
{
    if (! xml.hasTagName ("RedTriggerInstrument") && ! xml.hasTagName ("DrumTriggerInstrument"))
    {
        error = "Unexpected XML root <" + xml.getTagName() + ">";
        return false;
    }

    out = {};
    out.name = xml.getStringAttribute ("name");
    const int version = xml.getIntAttribute ("version", 1);

    auto readVel = [] (const juce::XmlElement& l, InstrumentDescription::DLayer& layer)
    {
        if (l.hasAttribute ("velLo")) layer.velLo = juce::jlimit (1, 127, l.getIntAttribute ("velLo"));
        if (l.hasAttribute ("velHi")) layer.velHi = juce::jlimit (1, 127, l.getIntAttribute ("velHi"));
    };

    if (version >= 2 || xml.getChildByName ("Channel") != nullptr)
    {
        for (auto* c : xml.getChildWithTagNameIterator ("Channel"))
        {
            if ((int) out.channels.size() >= maxChannels)
                break;
            ChannelDef def;
            def.name = c->getStringAttribute ("name", "Channel " + juce::String ((int) out.channels.size() + 1));
            for (auto* m : c->getChildWithTagNameIterator ("Mic"))
                if (def.mics.size() < maxMicsPerChannel)
                    def.mics.add (m->getStringAttribute ("name", "Mic " + juce::String (def.mics.size() + 1)));
            if (def.mics.isEmpty())
                def.mics.add ("Default");
            out.channels.push_back (def);
        }

        for (auto* l : xml.getChildWithTagNameIterator ("Layer"))
        {
            DLayer layer;
            readVel (*l, layer);
            for (auto* h : l->getChildWithTagNameIterator ("Hit"))
            {
                DHit hit;
                hit.peakDb = (float) h->getDoubleAttribute ("peakDb", -100.0);
                hit.paths.resize (out.channels.size());
                for (size_t c = 0; c < out.channels.size(); ++c)
                    hit.paths[c].resize ((size_t) out.channels[c].mics.size());

                for (auto* s : h->getChildWithTagNameIterator ("Sample"))
                {
                    const int c = s->getIntAttribute ("channel", -1), m = s->getIntAttribute ("mic", 0);
                    if (c >= 0 && c < (int) out.channels.size() && m >= 0 && m < out.channels[(size_t) c].mics.size())
                        hit.paths[(size_t) c][(size_t) m] = s->getStringAttribute ("file");
                }
                layer.hits.push_back (std::move (hit));
            }
            out.layers.push_back (std::move (layer));
        }
    }
    else
    {
        // version 1: fixed direct / overhead / room attributes
        static const char* attrs[] = { "direct", "overhead", "room" };
        static const char* names[] = { "Direct", "OH", "Room" };
        int channelFor[3] = { -1, -1, -1 };

        for (auto* l : xml.getChildWithTagNameIterator ("Layer"))
            for (auto* h : l->getChildWithTagNameIterator ("Hit"))
                for (int k = 0; k < 3; ++k)
                    if (h->getStringAttribute (attrs[k]).isNotEmpty() && channelFor[k] < 0)
                        channelFor[k] = 0;

        for (int k = 0; k < 3; ++k)
            if (channelFor[k] == 0)
            {
                channelFor[k] = (int) out.channels.size();
                out.channels.push_back ({ names[k], juce::StringArray { "Default" } });
            }

        for (auto* l : xml.getChildWithTagNameIterator ("Layer"))
        {
            DLayer layer;
            readVel (*l, layer);
            for (auto* h : l->getChildWithTagNameIterator ("Hit"))
            {
                DHit hit;
                hit.peakDb = (float) h->getDoubleAttribute ("peakDb", -100.0);
                hit.paths.assign (out.channels.size(), std::vector<juce::String> (1));
                for (int k = 0; k < 3; ++k)
                    if (channelFor[k] >= 0)
                        hit.paths[(size_t) channelFor[k]][0] = h->getStringAttribute (attrs[k]);
                layer.hits.push_back (std::move (hit));
            }
            out.layers.push_back (std::move (layer));
        }
    }

    if (out.channels.empty())
    {
        error = "Instrument has no mic channels";
        return false;
    }
    return true;
}

std::unique_ptr<juce::XmlElement> InstrumentDescription::toXml() const
{
    auto root = std::make_unique<juce::XmlElement> ("RedTriggerInstrument");
    root->setAttribute ("name", name);
    root->setAttribute ("version", 2);

    for (auto& c : channels)
    {
        auto* cx = root->createNewChildElement ("Channel");
        cx->setAttribute ("name", c.name);
        for (auto& m : c.mics)
            cx->createNewChildElement ("Mic")->setAttribute ("name", m);
    }

    for (auto& l : layers)
    {
        auto* lx = root->createNewChildElement ("Layer");
        if (l.velLo > 0) lx->setAttribute ("velLo", l.velLo);
        if (l.velHi > 0) lx->setAttribute ("velHi", l.velHi);

        for (auto& h : l.hits)
        {
            auto* hx = lx->createNewChildElement ("Hit");
            hx->setAttribute ("peakDb", juce::String (h.peakDb, 2));
            for (size_t c = 0; c < h.paths.size(); ++c)
                for (size_t m = 0; m < h.paths[c].size(); ++m)
                    if (h.paths[c][m].isNotEmpty())
                    {
                        auto* sx = hx->createNewChildElement ("Sample");
                        sx->setAttribute ("channel", (int) c);
                        sx->setAttribute ("mic", (int) m);
                        sx->setAttribute ("file", h.paths[c][m]);
                    }
        }
    }
    return root;
}

//==============================================================================
int Instrument::findLayer (int vel) const noexcept
{
    int best = 0, bestDist = INT_MAX;
    for (int i = 0; i < (int) layers.size(); ++i)
    {
        const auto& l = layers[(size_t) i];
        if (vel >= l.velLo && vel <= l.velHi)
            return i;
        const int d = vel < l.velLo ? l.velLo - vel : vel - l.velHi;
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return best;
}

int Instrument::totalHits() const noexcept
{
    int n = 0;
    for (auto& l : layers) n += (int) l.hits.size();
    return n;
}

juce::String Instrument::getSummary() const
{
    juce::StringArray ch;
    for (auto& c : channels)
        ch.add (c.name + (c.mics.size() > 1 ? " (" + juce::String (c.mics.size()) + " mics)" : juce::String()));
    return juce::String ((int) layers.size()) + " vel layers, " + juce::String (totalHits()) + " hits  |  "
           + ch.joinIntoString (", ");
}

std::unique_ptr<juce::XmlElement> Instrument::openPackage (const juce::File& file, StreamOpener& opener, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "File not found: " + file.getFullPathName();
        return nullptr;
    }

    std::unique_ptr<juce::XmlElement> xml;

    if (file.hasFileExtension ("dtz") || file.hasFileExtension ("zip"))
    {
        auto zip = std::make_shared<juce::ZipFile> (file);
        const int xmlIndex = zip->getIndexOfFileName ("instrument.xml", true);
        if (xmlIndex < 0)
        {
            error = "Not a Red Trigger instrument (no instrument.xml inside): " + file.getFileName();
            return nullptr;
        }

        std::unique_ptr<juce::InputStream> xmlStream (zip->createStreamForEntry (xmlIndex));
        if (xmlStream != nullptr)
            xml = juce::parseXML (xmlStream->readEntireStreamAsString());

        opener = [zip] (const juce::String& path) -> std::unique_ptr<juce::InputStream>
        {
            const int idx = zip->getIndexOfFileName (path.replaceCharacter ('\\', '/'));
            if (idx < 0)
                return {};
            std::unique_ptr<juce::InputStream> entry (zip->createStreamForEntry (idx));
            if (entry == nullptr)
                return {};
            juce::MemoryBlock mb;
            entry->readIntoMemoryBlock (mb);
            return std::make_unique<juce::MemoryInputStream> (mb, true);
        };
    }
    else
    {
        xml = juce::parseXML (file);
        const auto dir = file.getParentDirectory();
        opener = [dir] (const juce::String& path) -> std::unique_ptr<juce::InputStream>
        {
            auto f = dir.getChildFile (path.replaceCharacter ('\\', '/'));
            if (! f.existsAsFile())
                return {};
            return f.createInputStream();
        };
    }

    if (xml == nullptr)
        error = "Couldn't parse instrument XML in " + file.getFileName();
    return xml;
}

Instrument::Ptr Instrument::load (const juce::File& file, juce::String& error)
{
    StreamOpener opener;
    auto xml = openPackage (file, opener, error);
    if (xml == nullptr)
        return nullptr;

    auto inst = loadFromXml (*xml, opener, error);
    if (inst != nullptr)
    {
        inst->sourceFile = file;
        if (inst->name.isEmpty())
            inst->name = file.getFileNameWithoutExtension();
    }
    return inst;
}

Instrument::Ptr Instrument::loadFromXml (const juce::XmlElement& xml, const StreamOpener& opener, juce::String& error)
{
    InstrumentDescription desc;
    if (! InstrumentDescription::fromXml (xml, desc, error))
        return nullptr;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    Ptr inst = new Instrument();
    inst->name = desc.name;
    inst->channels = desc.channels;
    for (auto& c : inst->channels)
    {
        inst->slotBase.push_back (inst->numSlots);
        inst->numSlots += c.mics.size();
    }

    bool anyRanges = false;
    juce::StringArray missing;

    for (auto& dl : desc.layers)
    {
        Layer layer;
        if (dl.velLo > 0 || dl.velHi > 0)
        {
            anyRanges = true;
            layer.velLo = dl.velLo > 0 ? dl.velLo : 1;
            layer.velHi = juce::jmax (layer.velLo, dl.velHi > 0 ? dl.velHi : 127);
        }

        for (auto& dh : dl.hits)
        {
            Hit hit;
            hit.slots.resize ((size_t) inst->numSlots);
            hit.peakDb = dh.peakDb;
            bool haveRate = false;

            for (int c = 0; c < (int) dh.paths.size(); ++c)
                for (int m = 0; m < (int) dh.paths[(size_t) c].size(); ++m)
                {
                    const auto& path = dh.paths[(size_t) c][(size_t) m];
                    if (path.isEmpty())
                        continue;

                    auto& buf = hit.slots[(size_t) inst->slotIndex (c, m)];
                    double rate = 0.0;
                    if (! readAudio (fm, opener (path), buf, rate))
                    {
                        missing.addIfNotAlreadyThere (path);
                        buf.setSize (0, 0);
                        continue;
                    }

                    if (! haveRate) { hit.sampleRate = rate; haveRate = true; }
                    else            resample (buf, rate, hit.sampleRate);

                    hit.length = std::max (hit.length, buf.getNumSamples());
                }

            if (hit.length > 0)
                layer.hits.push_back (std::move (hit));
        }

        if (! layer.hits.empty())
            inst->layers.push_back (std::move (layer));
    }

    if (inst->layers.empty())
    {
        error = "Instrument contains no playable samples"
                + (missing.isEmpty() ? juce::String() : " (missing: " + missing.joinIntoString (", ") + ")");
        return nullptr;
    }

    const int n = (int) inst->layers.size();
    if (! anyRanges)
        for (int i = 0; i < n; ++i)
        {
            inst->layers[(size_t) i].velLo = 1 + (i * 127) / n;
            inst->layers[(size_t) i].velHi = ((i + 1) * 127) / n;
        }

    std::stable_sort (inst->layers.begin(), inst->layers.end(),
                      [] (const Layer& a, const Layer& b) { return a.velLo < b.velLo; });

    if (! missing.isEmpty())
        error = "Loaded, but some samples were missing: " + missing.joinIntoString (", ");

    return inst;
}

juce::File getDefaultInstrumentFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                   .getChildFile ("Red Trigger").getChildFile ("Instruments");
    dir.createDirectory();
    return dir;
}

juce::Array<juce::File> findInstrumentsInDefaultFolder()
{
    auto files = getDefaultInstrumentFolder().findChildFiles (juce::File::findFiles, true, "*.dtz");
    files.sort();
    return files;
}
} // namespace dt
