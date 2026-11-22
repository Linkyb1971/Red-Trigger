#include "Params.h"

namespace dt
{
juce::String ratioToText (float slope)
{
    if (slope < 0.999f)
    {
        const float r = 1.0f / (1.0f - slope);
        return (r < 10.0f ? juce::String (r, 1) : juce::String (juce::roundToInt (r))) + ":1";
    }
    if (slope <= 1.001f)
        return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e:1"));
    return "-" + juce::String (1.0f / (slope - 1.0f), 1) + ":1";
}

namespace
{
    using Attr = juce::AudioParameterFloatAttributes;

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre, float step = 0.0f)
    {
        juce::NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    }

    Attr dbAttr (float offBelow = -1000.0f)
    {
        return Attr().withLabel ("dB").withStringFromValueFunction ([offBelow] (float v, int)
        {
            if (v <= offBelow) return juce::String ("Off");
            return juce::String (v, 1) + " dB";
        });
    }

    Attr hzAttr (float offAtOrBelow = -1.0f, float offAtOrAbove = 1.0e9f)
    {
        return Attr().withLabel ("Hz").withStringFromValueFunction ([=] (float v, int)
        {
            if (v <= offAtOrBelow || v >= offAtOrAbove) return juce::String ("Off");
            return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
        });
    }

    Attr msAttr (float fullAtOrAbove = 1.0e9f)
    {
        return Attr().withLabel ("ms").withStringFromValueFunction ([=] (float v, int)
        {
            if (v >= fullAtOrAbove) return juce::String ("Full");
            return juce::String (v, v < 10.0f ? 1 : 0) + " ms";
        });
    }

    Attr pctAttr()
    {
        return Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int)
        { return juce::String (juce::roundToInt (v)) + "%"; });
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using P = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using C = juce::AudioParameterChoice;
    using ID = juce::ParameterID;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // --- Detection
    p.push_back (std::make_unique<P> (ID { pid::threshold, 1 }, "Threshold", juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -24.0f, dbAttr()));
    p.push_back (std::make_unique<P> (ID { pid::ceiling, 1 }, "Ceiling", juce::NormalisableRange<float> (-48.0f, 0.0f, 0.1f), -3.0f, dbAttr()));
    p.push_back (std::make_unique<P> (ID { pid::retrigger, 1 }, "Retrigger", skewed (5.0f, 300.0f, 50.0f, 0.1f), 40.0f, msAttr()));
    p.push_back (std::make_unique<P> (ID { pid::adaptive, 1 }, "Adaptive", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 30.0f, pctAttr()));
    p.push_back (std::make_unique<P> (ID { pid::detHpf, 1 }, "Detect HPF", skewed (20.0f, 2000.0f, 200.0f, 1.0f), 20.0f, hzAttr (20.0f)));
    p.push_back (std::make_unique<P> (ID { pid::detLpf, 1 }, "Detect LPF", skewed (200.0f, 20000.0f, 2500.0f, 1.0f), 20000.0f, hzAttr (-1.0f, 19999.0f)));
    p.push_back (std::make_unique<B> (ID { pid::listen, 1 }, "Listen Detector", false));
    p.push_back (std::make_unique<B> (ID { pid::align, 1 }, "Latency Comp", true));

    // --- Sample
    p.push_back (std::make_unique<P> (ID { pid::dynamics, 1 }, "Dynamics", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 75.0f, pctAttr()));
    p.push_back (std::make_unique<P> (ID { pid::pitch, 1 }, "Pitch", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
                                      Attr().withLabel ("st").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " st"; })));
    p.push_back (std::make_unique<P> (ID { pid::length, 1 }, "Length", skewed (10.0f, 2000.0f, 300.0f, 1.0f), 2000.0f, msAttr (1999.0f)));
    p.push_back (std::make_unique<P> (ID { pid::offset, 1 }, "Offset", juce::NormalisableRange<float> (0.0f, 20.0f, 0.01f), 0.0f,
                                      Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " ms"; })));
    p.push_back (std::make_unique<P> (ID { pid::micDirect, 1 }, "Direct", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f, dbAttr (-59.9f)));
    p.push_back (std::make_unique<P> (ID { pid::micOverhead, 1 }, "Overhead", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f, dbAttr (-59.9f)));
    p.push_back (std::make_unique<P> (ID { pid::micRoom, 1 }, "Room", juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f, dbAttr (-59.9f)));
    p.push_back (std::make_unique<C> (ID { pid::rrMode, 1 }, "Round Robin", juce::StringArray { "Cycle", "Random" }, 0));
    p.push_back (std::make_unique<B> (ID { pid::choke, 1 }, "Choke", false));
    p.push_back (std::make_unique<B> (ID { pid::polarity, 1 }, "Trigger Polarity", false));

    // --- EQ
    p.push_back (std::make_unique<B> (ID { pid::eqOn, 1 }, "EQ On", true));
    p.push_back (std::make_unique<P> (ID { pid::eqHpf, 1 }, "EQ HPF", skewed (20.0f, 1000.0f, 150.0f, 1.0f), 20.0f, hzAttr (20.0f)));
    p.push_back (std::make_unique<P> (ID { pid::eqLowFreq, 1 }, "Low Freq", skewed (30.0f, 600.0f, 120.0f, 1.0f), 100.0f, hzAttr()));
    p.push_back (std::make_unique<P> (ID { pid::eqLowGain, 1 }, "Low Gain", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, dbAttr()));
    p.push_back (std::make_unique<P> (ID { pid::eqMidFreq, 1 }, "Mid Freq", skewed (150.0f, 10000.0f, 1200.0f, 1.0f), 1000.0f, hzAttr()));
    p.push_back (std::make_unique<P> (ID { pid::eqMidGain, 1 }, "Mid Gain", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, dbAttr()));
    p.push_back (std::make_unique<P> (ID { pid::eqMidQ, 1 }, "Mid Q", skewed (0.3f, 8.0f, 1.2f, 0.01f), 1.0f,
                                      Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); })));
    p.push_back (std::make_unique<P> (ID { pid::eqHighFreq, 1 }, "High Freq", skewed (1500.0f, 16000.0f, 6000.0f, 1.0f), 8000.0f, hzAttr()));
    p.push_back (std::make_unique<P> (ID { pid::eqHighGain, 1 }, "High Gain", juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f, dbAttr()));
    p.push_back (std::make_unique<P> (ID { pid::eqLpf, 1 }, "EQ LPF", skewed (1000.0f, 20000.0f, 6000.0f, 1.0f), 20000.0f, hzAttr (-1.0f, 19999.0f)));

    // --- Compressor (dbx 160 style)
    p.push_back (std::make_unique<B> (ID { pid::compOn, 1 }, "Comp On", false));
    p.push_back (std::make_unique<P> (ID { pid::compThresh, 1 }, "Comp Threshold", juce::NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -20.0f, dbAttr()));
    p.push_back (std::make_unique<P> (ID { pid::compRatio, 1 }, "Comp Ratio", juce::NormalisableRange<float> (0.0f, 2.0f, 0.001f), 0.75f,
                                      Attr().withStringFromValueFunction ([] (float v, int) { return ratioToText (v); })));
    p.push_back (std::make_unique<B> (ID { pid::compOverEasy, 1 }, "OverEasy", false));
    p.push_back (std::make_unique<P> (ID { pid::compOutput, 1 }, "Comp Output", juce::NormalisableRange<float> (-20.0f, 20.0f, 0.1f), 0.0f, dbAttr()));
    p.push_back (std::make_unique<P> (ID { pid::compMix, 1 }, "Comp Mix", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, pctAttr()));

    // --- Output
    p.push_back (std::make_unique<P> (ID { pid::trigLevel, 1 }, "Trigger Level", juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f), 0.0f, dbAttr (-59.9f)));
    p.push_back (std::make_unique<P> (ID { pid::blend, 1 }, "Blend", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
                                      Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int)
                                      {
                                          const int t = juce::roundToInt (v);
                                          if (t == 0) return juce::String ("Dry");
                                          if (t == 100) return juce::String ("Trigger");
                                          return juce::String (100 - t) + " / " + juce::String (t);
                                      })));
    p.push_back (std::make_unique<P> (ID { pid::outGain, 1 }, "Output", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, dbAttr()));

    return { p.begin(), p.end() };
}
} // namespace dt
