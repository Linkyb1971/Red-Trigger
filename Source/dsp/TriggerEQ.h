#pragma once
#include "Biquad.h"
#include <tuple>

namespace dt
{
/** 5-band trigger EQ: HPF, low shelf, mid bell, high shelf, LPF (stereo). */
class TriggerEQ
{
public:
    struct Params
    {
        float hpf = 20.0f, lowFreq = 100.0f, lowGain = 0.0f,
              midFreq = 1000.0f, midGain = 0.0f, midQ = 1.0f,
              highFreq = 8000.0f, highGain = 0.0f, lpf = 20000.0f;

        auto tie() const { return std::tie (hpf, lowFreq, lowGain, midFreq, midGain, midQ, highFreq, highGain, lpf); }
        bool operator== (const Params& o) const { return tie() == o.tie(); }
    };

    void prepare (double sampleRate) { fs = sampleRate; dirty = true; setParams (current); reset(); }

    void reset()
    {
        for (auto& band : filters)
            for (auto& f : band)
                f.reset();
    }

    void setParams (const Params& p)
    {
        if (! dirty && p == current)
            return;

        using T = BiquadCoeffs::Type;
        const BiquadCoeffs c[numBands] = {
            BiquadCoeffs::make (T::highPass,  fs, p.hpf, 0.7071),
            BiquadCoeffs::make (T::lowShelf,  fs, p.lowFreq, 0.7071, p.lowGain),
            BiquadCoeffs::make (T::peak,      fs, p.midFreq, p.midQ, p.midGain),
            BiquadCoeffs::make (T::highShelf, fs, p.highFreq, 0.7071, p.highGain),
            BiquadCoeffs::make (T::lowPass,   fs, p.lpf, 0.7071)
        };

        for (int b = 0; b < numBands; ++b)
            for (auto& f : filters[b])
                f.setCoeffs (c[b]);

        active[0] = p.hpf > 20.5f;
        active[1] = std::abs (p.lowGain) > 0.01f;
        active[2] = std::abs (p.midGain) > 0.01f;
        active[3] = std::abs (p.highGain) > 0.01f;
        active[4] = p.lpf < 19999.0f;

        current = p;
        dirty = false;
    }

    void process (float* left, float* right, int n) noexcept
    {
        for (int b = 0; b < numBands; ++b)
        {
            if (! active[b])
                continue;
            filters[b][0].process (left, n);
            if (right != nullptr)
                filters[b][1].process (right, n);
        }
    }

private:
    static constexpr int numBands = 5;
    double fs = 44100.0;
    Biquad filters[numBands][2];
    bool active[numBands] {};
    Params current;
    bool dirty = true;
};
} // namespace dt
