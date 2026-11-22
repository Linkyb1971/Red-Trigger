#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace dt::pid
{
// Detection
inline constexpr auto threshold   = "det_threshold";
inline constexpr auto ceiling     = "det_ceiling";
inline constexpr auto retrigger   = "det_retrigger";
inline constexpr auto adaptive    = "det_adaptive";
inline constexpr auto detHpf      = "det_hpf";
inline constexpr auto detLpf      = "det_lpf";
inline constexpr auto listen      = "det_listen";
inline constexpr auto align       = "det_align";
// Sample
inline constexpr auto dynamics    = "smp_dynamics";
inline constexpr auto pitch       = "smp_pitch";
inline constexpr auto length      = "smp_length";
inline constexpr auto offset      = "smp_offset";
inline constexpr auto micDirect   = "smp_direct";
inline constexpr auto micOverhead = "smp_overhead";
inline constexpr auto micRoom     = "smp_room";
inline constexpr auto rrMode      = "smp_rrmode";
inline constexpr auto choke       = "smp_choke";
inline constexpr auto polarity    = "trig_polarity";
// EQ
inline constexpr auto eqOn        = "eq_on";
inline constexpr auto eqHpf       = "eq_hpf";
inline constexpr auto eqLowFreq   = "eq_low_freq";
inline constexpr auto eqLowGain   = "eq_low_gain";
inline constexpr auto eqMidFreq   = "eq_mid_freq";
inline constexpr auto eqMidGain   = "eq_mid_gain";
inline constexpr auto eqMidQ      = "eq_mid_q";
inline constexpr auto eqHighFreq  = "eq_high_freq";
inline constexpr auto eqHighGain  = "eq_high_gain";
inline constexpr auto eqLpf       = "eq_lpf";
// Compressor
inline constexpr auto compOn      = "comp_on";
inline constexpr auto compThresh  = "comp_threshold";
inline constexpr auto compRatio   = "comp_ratio";
inline constexpr auto compOverEasy= "comp_overeasy";
inline constexpr auto compOutput  = "comp_output";
inline constexpr auto compMix     = "comp_mix";
// Output
inline constexpr auto trigLevel   = "trig_level";
inline constexpr auto blend       = "mix_blend";
inline constexpr auto outGain     = "out_gain";
} // namespace dt::pid

namespace dt
{
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
juce::String ratioToText (float slope);
}
