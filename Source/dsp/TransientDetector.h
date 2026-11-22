#pragma once
#include "Biquad.h"
#include <climits>
#include <array>
#include <juce_core/juce_core.h>

namespace dt
{
/**
    Transient detector for drum triggering.

    - The (mono) input runs through optional HPF/LPF "detection" filters so a
      kick mic can ignore snare bleed etc.
    - A hit starts when |x| crosses the effective threshold while the detector is armed
      and the retrigger time has elapsed.
    - The peak is measured over a short window (velocityWindowMs) after the crossing,
      and the hit is reported at the END of that window. The plugin delays the dry
      signal by exactly that window ("latency compensation") so samples land
      sample-accurately on the original transient.
    - "Adaptive" raises the threshold relative to the last hit's peak and lets it
      decay, which rejects flams/ringing from the same hit.
    - The detector re-arms only once the envelope (the peak over the last 12 ms, long
      enough to span a half-cycle of a 40 Hz kick) falls 3 dB below the effective
      threshold, so a long sustain can't machine-gun retriggers.
*/
class TransientDetector
{
public:
    struct Params
    {
        float thresholdDb = -24.0f;
        float retriggerMs = 40.0f;
        float adaptivePct = 30.0f;
        float hpfHz = 20.0f;      // <= 20 Hz means off
        float lpfHz = 20000.0f;   // >= 20 kHz means off
    };

    static constexpr double velocityWindowMs = 3.0;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        windowSamples = std::max (1, (int) std::lround (velocityWindowMs * 0.001 * fs));
        subBlockLen = std::max (1, (int) std::lround (0.001 * fs));
        adaptDecay  = (float) std::exp (-1.0 / (0.060 * fs));
        filtersDirty = true;
        setParams (params);
        reset();
    }

    void reset()
    {
        hp.reset(); lp.reset();
        subMax.fill (0.0f); subIndex = 0; subCount = 0; currentSubMax = 0.0f; ringMax = 0.0f;
        adaptiveLevel = 0.0f;
        measuring = false; count = 0; peak = 0.0f;
        sinceTrigger = INT_MAX; armed = true;
    }

    int getWindowSamples() const noexcept { return windowSamples; }

    void setParams (const Params& p)
    {
        if (filtersDirty || ! juce::exactlyEqual (p.hpfHz, params.hpfHz))
            hp.setCoeffs (BiquadCoeffs::make (BiquadCoeffs::Type::highPass, fs, p.hpfHz, 0.7071));
        if (filtersDirty || ! juce::exactlyEqual (p.lpfHz, params.lpfHz))
            lp.setCoeffs (BiquadCoeffs::make (BiquadCoeffs::Type::lowPass, fs, p.lpfHz, 0.7071));
        filtersDirty = false;

        params = p;
        hpOn = p.hpfHz > 20.5f;
        lpOn = p.lpfHz < 19999.0f;
        thresholdLin = dbToGain (p.thresholdDb);
        retriggerSamples = (int) (p.retriggerMs * 0.001f * (float) fs);
        adaptAmount = std::clamp (p.adaptivePct * 0.01f, 0.0f, 1.0f);
    }

    /** Processes one sample. Returns the filtered detection signal.
        Sets hit=true (and hitPeak) on the sample where a hit is finalised. */
    inline float process (float x, bool& hit, float& hitPeak) noexcept
    {
        float y = x;
        if (hpOn) y = hp.process (y);
        if (lpOn) y = lp.process (y);

        const float a = std::abs (y);

        // sliding 12 ms peak envelope, built from 1 ms sub-block maxima
        currentSubMax = std::max (currentSubMax, a);
        if (++subCount >= subBlockLen)
        {
            subMax[(size_t) subIndex] = currentSubMax;
            subIndex = (subIndex + 1) % numSubBlocks;
            ringMax = *std::max_element (subMax.begin(), subMax.end());
            currentSubMax = 0.0f;
            subCount = 0;
        }
        const float env = std::max (ringMax, currentSubMax);

        adaptiveLevel *= adaptDecay;
        const float effThr = std::max (thresholdLin, adaptiveLevel);

        if (sinceTrigger < INT_MAX)
            ++sinceTrigger;

        hit = false;

        if (measuring)
        {
            peak = std::max (peak, a);
            if (++count >= windowSamples)
            {
                measuring = false;
                hit = true;
                hitPeak = peak;
                adaptiveLevel = std::max (adaptiveLevel, peak * adaptAmount);
            }
        }
        else
        {
            if (! armed && env < effThr * 0.7079f)
                armed = true;

            if (armed && a >= effThr && sinceTrigger >= retriggerSamples)
            {
                measuring = true;
                armed = false;
                count = 0;
                peak = a;
                sinceTrigger = 0;
            }
        }

        return y;
    }

    float getEffectiveThreshold() const noexcept { return std::max (thresholdLin, adaptiveLevel); }

private:
    double fs = 44100.0;
    Params params;
    Biquad hp, lp;
    bool hpOn = false, lpOn = false, filtersDirty = true;

    int windowSamples = 132, retriggerSamples = 1764, count = 0, sinceTrigger = INT_MAX;
    float thresholdLin = 0.06f, adaptAmount = 0.3f;
    static constexpr int numSubBlocks = 12;
    std::array<float, numSubBlocks> subMax {};
    int subBlockLen = 44, subIndex = 0, subCount = 0;
    float currentSubMax = 0.0f, ringMax = 0.0f;
    float adaptiveLevel = 0.0f, adaptDecay = 0.9996f, peak = 0.0f;
    bool measuring = false, armed = true;
};
} // namespace dt
