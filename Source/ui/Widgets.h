#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "LookAndFeel.h"
#include "dsp/Biquad.h"

namespace dt::ui
{
using APVTS = juce::AudioProcessorValueTreeState;

/** Rotary knob + caption, bound to a parameter. Double-click resets to default. */
class Knob : public juce::Component
{
public:
    Knob (APVTS& state, const juce::String& paramId, const juce::String& caption,
          juce::Colour colour = colours::accent)
        : slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow)
    {
        label.setText (caption, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, colours::dimText);
        addAndMakeVisible (label);

        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
        slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (slider);
        attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);

        if (auto* p = state.getParameter (paramId))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (16));
        slider.setBounds (r);
    }

    juce::Slider slider;

private:
    juce::Label label;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

class Toggle : public juce::ToggleButton
{
public:
    Toggle (APVTS& state, const juce::String& paramId, const juce::String& buttonText)
    {
        setButtonText (buttonText);
        attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, *this);
    }
private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

/** Titled rounded panel. */
class Panel : public juce::Component
{
public:
    explicit Panel (juce::String t, juce::String sub = {}) : title (std::move (t)), subtitle (std::move (sub)) {}

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

        g.setColour (colours::accent);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (title, 10, 4, getWidth() - 20, 18, juce::Justification::centredLeft);
        if (subtitle.isNotEmpty())
        {
            g.setColour (colours::dimText);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (subtitle, 10, 4, getWidth() - 20, 18, juce::Justification::centredRight);
        }
    }

    juce::Rectangle<int> content() const { return getLocalBounds().reduced (8).withTrimmedTop (18); }

private:
    juce::String title, subtitle;
};

/** Vertical peak meter with decay; or a gain-reduction meter that grows downwards. */
class Meter : public juce::Component
{
public:
    Meter (juce::String caption, bool isGainReduction = false)
        : name (std::move (caption)), gr (isGainReduction) {}

    /** value: linear peak (level meter) or dB of reduction (GR meter). */
    void update (float value)
    {
        const float target = gr ? std::clamp (value / 24.0f, 0.0f, 1.0f)
                                : std::clamp ((gainToDb (value) + 60.0f) / 66.0f, 0.0f, 1.0f);
        level = target > level ? target : level * 0.85f + target * 0.15f;
        if (! gr)
            clip = value > 1.0f ? 30 : std::max (0, clip - 1);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto cap = r.removeFromBottom (14);
        g.setColour (colours::dimText);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (name, cap, juce::Justification::centred);

        auto bar = r.reduced (r.getWidth() * 0.2f, 1.0f);
        g.setColour (juce::Colour (0xff0e1013));
        g.fillRoundedRectangle (bar, 2.0f);

        if (gr)
        {
            g.setColour (colours::accent2);
            g.fillRoundedRectangle (bar.withHeight (bar.getHeight() * level), 2.0f);
        }
        else
        {
            auto filled = bar.withTrimmedTop (bar.getHeight() * (1.0f - level));
            g.setGradientFill (juce::ColourGradient (colours::meterRed, bar.getX(), bar.getY(),
                                                     colours::meterGreen, bar.getX(), bar.getBottom(), false));
            g.fillRoundedRectangle (filled, 2.0f);
            if (clip > 0)
            {
                g.setColour (colours::meterRed);
                g.fillRect (bar.withHeight (4.0f));
            }
        }
    }

private:
    juce::String name;
    bool gr;
    float level = 0.0f;
    int clip = 0;
};
} // namespace dt::ui
