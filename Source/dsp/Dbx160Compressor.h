#pragma once
#include "Biquad.h"

namespace dt
{
/**
    dbx 160-style feed-forward VCA compressor.

    Modelled behaviour:
    - True-RMS, stereo-linked level detection.
    - Program-dependent attack: the bigger the overshoot the faster it clamps
      (~15 ms for 10 dB, ~5 ms for 20 dB, ~3 ms for 30 dB over threshold).
    - Constant-rate release of 120 dB/second.
    - "Slope" control from 1:1 through infinity:1 to -1:1 (negative ratios), like the
      original's ratio knob. slope = 1 - 1/ratio; 0 = 1:1, 1 = inf:1, 2 = -1:1.
    - Optional OverEasy soft knee.
    - Output gain and a parallel (dry/wet) mix.
*/
class Dbx160Compressor
{
public:
    struct Params
    {
        float thresholdDb = -20.0f;
        float slope = 0.75f;       // 4:1
        float outputDb = 0.0f;
        float mix = 1.0f;          // 0..1
        bool overEasy = false;
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        rmsCoef = (float) std::exp (-1.0 / (0.0015 * fs));
        releaseStep = (float) (120.0 / fs);
        reset();
    }

    void reset() { meanSquare = 0.0f; grDb = 0.0f; }

    static float computeGainReduction (float overDb, float slope, bool overEasy) noexcept
    {
        if (! overEasy)
            return overDb > 0.0f ? slope * overDb : 0.0f;

        constexpr float knee = 10.0f;
        if (overDb <= -knee * 0.5f) return 0.0f;
        if (overDb >=  knee * 0.5f) return slope * overDb;
        const float t = overDb + knee * 0.5f;
        return slope * t * t / (2.0f * knee);
    }

    /** Processes in place. right may be nullptr for mono. */
    void process (float* left, float* right, int n, const Params& p) noexcept
    {
        const float outGain = dbToGain (p.outputDb);
        const float mix = std::clamp (p.mix, 0.0f, 1.0f);
        maxGrInBlock = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float l = left[i];
            const float r = right != nullptr ? right[i] : l;
            const float sq = 0.5f * (l * l + r * r);

            meanSquare = rmsCoef * meanSquare + (1.0f - rmsCoef) * sq;
            const float levelDb = 10.0f * std::log10 (meanSquare + 1.0e-12f);
            const float overDb = levelDb - p.thresholdDb;
            const float target = computeGainReduction (overDb, p.slope, p.overEasy);

            if (target > grDb)
            {
                // attack speed depends on how far the signal is over threshold (dbx 160 spec:
                // 15 ms @ +10 dB, 5 ms @ +20 dB, 3 ms @ +30 dB)
                const float over = std::max (overDb, 1.0f);
                const float tau = std::clamp (0.015f * std::pow (10.0f / over, 1.47f), 0.0008f, 0.05f);
                const float c = 1.0f - std::exp (-1.0f / (tau * (float) fs));
                grDb += (target - grDb) * c;
            }
            else
            {
                grDb = std::max (target, grDb - releaseStep);
            }

            maxGrInBlock = std::max (maxGrInBlock, grDb);
            const float g = dbToGain (-grDb) * outGain;
            const float wet = mix * g + (1.0f - mix);

            left[i] = l * wet;
            if (right != nullptr)
                right[i] = r * wet;
        }
    }

    float getGainReductionDb() const noexcept { return maxGrInBlock; }
    float getCurrentGainReductionDb() const noexcept { return grDb; }

private:
    double fs = 44100.0;
    float rmsCoef = 0.98f, releaseStep = 0.0027f;
    float meanSquare = 0.0f, grDb = 0.0f, maxGrInBlock = 0.0f;
};
} // namespace dt
