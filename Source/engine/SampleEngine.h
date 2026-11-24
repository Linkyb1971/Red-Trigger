#pragma once
#include "Instrument.h"
#include <climits>

namespace dt
{
/** Polyphonic one-shot sample player with velocity layers, round robin and up to 8 mic
    channels, each with a selectable mic, level and pan.
    Audio-thread safe: no allocation after prepare(). */
class SampleEngine
{
public:
    static constexpr int maxVoices = 32;
    static constexpr int maxLayers = 128;

    struct Settings
    {
        struct ChannelMix { int mic = 0; float gainL = 1.0f, gainR = 1.0f; };
        std::array<ChannelMix, maxChannels> channels {};
        float pitchSemis = 0.0f;
        bool fullLength = true;
        float lengthMs = 2000.0f;
        bool choke = false;
        bool randomRoundRobin = false;
        int offsetSamples = 0;
    };

    void prepare (double sampleRate)
    {
        hostRate = sampleRate;
        lengthFadeSamples = std::max (1, (int) (0.010 * sampleRate));
        chokeFadeSamples  = std::max (1, (int) (0.003 * sampleRate));
        stopAll();
    }

    void setInstrument (Instrument::Ptr newInstrument)
    {
        stopAll();
        instrument = std::move (newInstrument);
        rrCounter.fill (0);
        lastRandom.fill (-1);
    }

    const Instrument* getInstrument() const noexcept { return instrument.get(); }

    void stopAll() noexcept
    {
        for (auto& v : voices)
            v.active = false;
    }

    int getNumActiveVoices() const noexcept
    {
        int n = 0;
        for (auto& v : voices) n += v.active ? 1 : 0;
        return n;
    }

    /** Starts a hit at the engine's current render position.
        Returns the chosen layer index, or -1 if nothing could be played. */
    int trigger (int velocity127, float gain, const Settings& s) noexcept
    {
        if (instrument == nullptr || instrument->layers.empty())
            return -1;

        const int li = instrument->findLayer (velocity127);
        const auto& layer = instrument->layers[(size_t) li];
        const int nh = (int) layer.hits.size();
        if (nh == 0)
            return -1;

        const size_t slot = (size_t) (li % maxLayers);
        int hi = 0;
        if (s.randomRoundRobin && nh > 1)
        {
            const int last = lastRandom[slot];
            if (last < 0) hi = random.nextInt (nh);
            else          { hi = random.nextInt (nh - 1); if (hi >= last) ++hi; }
            lastRandom[slot] = hi;
        }
        else
        {
            hi = rrCounter[slot] % nh;
            rrCounter[slot] = (hi + 1) % nh;
        }

        if (s.choke)
            for (auto& v : voices)
                if (v.active)
                    v.fadeStep = std::max (v.fadeStep, 1.0f / (float) chokeFadeSamples);

        auto& v = findFreeVoice();
        const auto& hit = layer.hits[(size_t) hi];
        v.hit = &hit;
        v.pos = 0.0;
        v.inc = hit.sampleRate / hostRate * std::pow (2.0, (double) s.pitchSemis / 12.0);
        v.gain = gain;
        v.delay = std::max (0, s.offsetSamples);
        v.played = 0;
        v.lengthSamples = s.fullLength ? INT_MAX : std::max (1, (int) (s.lengthMs * 0.001 * hostRate));
        v.fade = 1.0f;
        v.fadeStep = 0.0f;
        v.active = true;
        v.age = ++ageCounter;
        return li;
    }

    /** Adds voices into out (must have >= 2 channels) over [start, start + num). */
    void render (juce::AudioBuffer<float>& out, int start, int num, const Settings& s) noexcept
    {
        if (num <= 0)
            return;

        float* L = out.getWritePointer (0);
        float* R = out.getWritePointer (1);

        for (auto& v : voices)
        {
            if (! v.active)
                continue;

            const Hit& h = *v.hit;
            const float* micL[maxChannels] {};
            const float* micR[maxChannels] {};
            int micLen[maxChannels] {};
            float gL[maxChannels] {}, gR[maxChannels] {};
            int numUsed = 0;

            for (int c = 0; c < instrument->numChannels() && c < maxChannels; ++c)
            {
                const auto& mix = s.channels[(size_t) c];
                if (mix.gainL <= 0.0f && mix.gainR <= 0.0f)
                    continue;

                // selected mic; if this hit lacks it, fall back to the first mic that exists
                const int numMics = instrument->channels[(size_t) c].mics.size();
                const juce::AudioBuffer<float>* b = &h.slots[(size_t) instrument->slotIndex (c, juce::jlimit (0, numMics - 1, mix.mic))];
                for (int m = 0; b->getNumSamples() == 0 && m < numMics; ++m)
                    b = &h.slots[(size_t) instrument->slotIndex (c, m)];
                if (b->getNumSamples() == 0)
                    continue;

                micL[numUsed] = b->getReadPointer (0);
                micR[numUsed] = b->getNumChannels() > 1 ? b->getReadPointer (1) : micL[numUsed];
                micLen[numUsed] = b->getNumSamples();
                gL[numUsed] = mix.gainL;
                gR[numUsed] = mix.gainR;
                ++numUsed;
            }

            for (int i = start; i < start + num; ++i)
            {
                if (v.delay > 0) { --v.delay; continue; }

                const int ip = (int) v.pos;
                if (ip >= h.length) { v.active = false; break; }
                const float t = (float) (v.pos - (double) ip);

                float l = 0.0f, r = 0.0f;
                for (int k = 0; k < numUsed; ++k)
                {
                    const float sl = hermite (micL[k], micLen[k], ip, t);
                    const float sr = micR[k] == micL[k] ? sl : hermite (micR[k], micLen[k], ip, t);
                    l += sl * gL[k];
                    r += sr * gR[k];
                }

                const float g = v.gain * v.fade;
                L[i] += l * g;
                R[i] += r * g;

                v.pos += v.inc;
                if (++v.played >= v.lengthSamples && v.fadeStep <= 0.0f)
                    v.fadeStep = 1.0f / (float) lengthFadeSamples;

                if (v.fadeStep > 0.0f)
                {
                    v.fade -= v.fadeStep;
                    if (v.fade <= 0.0f) { v.active = false; break; }
                }
            }
        }
    }

private:
    struct Voice
    {
        const Hit* hit = nullptr;
        double pos = 0.0, inc = 1.0;
        float gain = 0.0f, fade = 1.0f, fadeStep = 0.0f;
        int delay = 0, played = 0, lengthSamples = INT_MAX;
        bool active = false;
        juce::uint32 age = 0;
    };

    static inline float at (const float* d, int len, int i) noexcept { return (i >= 0 && i < len) ? d[i] : 0.0f; }

    static inline float hermite (const float* d, int len, int i, float t) noexcept
    {
        if (t <= 0.0f)
            return at (d, len, i);
        const float xm1 = at (d, len, i - 1), x0 = at (d, len, i), x1 = at (d, len, i + 1), x2 = at (d, len, i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    Voice& findFreeVoice() noexcept
    {
        Voice* oldest = &voices[0];
        for (auto& v : voices)
        {
            if (! v.active) return v;
            if (v.age < oldest->age) oldest = &v;
        }
        return *oldest;
    }

    Instrument::Ptr instrument;
    std::array<Voice, maxVoices> voices;
    std::array<int, maxLayers> rrCounter {};
    std::array<int, maxLayers> lastRandom {};
    juce::Random random;
    double hostRate = 44100.0;
    int lengthFadeSamples = 441, chokeFadeSamples = 132;
    juce::uint32 ageCounter = 0;
};
} // namespace dt
