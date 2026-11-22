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
    juce::StringArray mics;
    if (micPresent[micDirect])   mics.add ("Direct");
    if (micPresent[micOverhead]) mics.add ("OH");
    if (micPresent[micRoom])     mics.add ("Room");
    return juce::String ((int) layers.size()) + " vel layers, " + juce::String (totalHits()) + " hits  |  mics: "
           + mics.joinIntoString ("/");
}

Instrument::Ptr Instrument::load (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "File not found: " + file.getFullPathName();
        return nullptr;
    }

    std::unique_ptr<juce::XmlElement> xml;
    StreamOpener opener;

    if (file.hasFileExtension ("dtz") || file.hasFileExtension ("zip"))
    {
        auto zip = std::make_shared<juce::ZipFile> (file);
        const int xmlIndex = zip->getIndexOfFileName ("instrument.xml", true);
        if (xmlIndex < 0)
        {
            error = "Not a DrumTrigger instrument (no instrument.xml inside): " + file.getFileName();
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
    {
        error = "Couldn't parse instrument XML in " + file.getFileName();
        return nullptr;
    }

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
    if (! xml.hasTagName ("DrumTriggerInstrument"))
    {
        error = "Unexpected XML root <" + xml.getTagName() + ">";
        return nullptr;
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    Ptr inst = new Instrument();
    inst->name = xml.getStringAttribute ("name");
    bool anyRanges = false;
    juce::StringArray missing;

    for (auto* layerXml : xml.getChildWithTagNameIterator ("Layer"))
    {
        Layer layer;
        if (layerXml->hasAttribute ("velLo") || layerXml->hasAttribute ("velHi"))
        {
            anyRanges = true;
            layer.velLo = juce::jlimit (1, 127, layerXml->getIntAttribute ("velLo", 1));
            layer.velHi = juce::jlimit (layer.velLo, 127, layerXml->getIntAttribute ("velHi", 127));
        }

        for (auto* hitXml : layerXml->getChildWithTagNameIterator ("Hit"))
        {
            Hit hit;
            bool haveRate = false;

            for (int m = 0; m < numMics; ++m)
            {
                const auto path = hitXml->getStringAttribute (micAttributeName (m)).trim();
                if (path.isEmpty())
                    continue;

                double rate = 0.0;
                auto& buf = hit.mic[(size_t) m];
                if (! readAudio (fm, opener (path), buf, rate))
                {
                    missing.addIfNotAlreadyThere (path);
                    buf.setSize (0, 0);
                    continue;
                }

                if (! haveRate) { hit.sampleRate = rate; haveRate = true; }
                else            resample (buf, rate, hit.sampleRate);

                hit.length = std::max (hit.length, buf.getNumSamples());
                inst->micPresent[(size_t) m] = true;
            }

            hit.peakDb = (float) hitXml->getDoubleAttribute ("peakDb", -100.0);
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
    {
        for (int i = 0; i < n; ++i)
        {
            inst->layers[(size_t) i].velLo = 1 + (i * 127) / n;
            inst->layers[(size_t) i].velHi = ((i + 1) * 127) / n;
        }
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
                   .getChildFile ("DrumTrigger").getChildFile ("Instruments");
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
