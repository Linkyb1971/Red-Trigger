#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace dt::ui
{
namespace colours
{
    const juce::Colour background { 0xff15171b };
    const juce::Colour panel      { 0xff1f2228 };
    const juce::Colour panelEdge  { 0xff2d3139 };
    const juce::Colour text       { 0xffd8dce3 };
    const juce::Colour dimText    { 0xff8a919c };
    const juce::Colour accent     { 0xffff8a1f };   // orange
    const juce::Colour accent2    { 0xff3fc1c9 };   // teal
    const juce::Colour knobBody   { 0xff2a2e35 };
    const juce::Colour meterGreen { 0xff52d273 };
    const juce::Colour meterRed   { 0xffff4d4d };
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, colours::background);
        setColour (juce::Label::textColourId, colours::text);
        setColour (juce::Slider::textBoxTextColourId, colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::rotarySliderFillColourId, colours::accent);
        setColour (juce::Slider::trackColourId, colours::accent);
        setColour (juce::Slider::backgroundColourId, colours::knobBody);
        setColour (juce::Slider::thumbColourId, colours::text);
        setColour (juce::ComboBox::backgroundColourId, colours::knobBody);
        setColour (juce::ComboBox::outlineColourId, colours::panelEdge);
        setColour (juce::ComboBox::textColourId, colours::text);
        setColour (juce::ComboBox::arrowColourId, colours::accent);
        setColour (juce::PopupMenu::backgroundColourId, colours::panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha (0.35f));
        setColour (juce::TextButton::buttonColourId, colours::knobBody);
        setColour (juce::TextButton::buttonOnColourId, colours::accent.darker (0.3f));
        setColour (juce::TextButton::textColourOffId, colours::text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ToggleButton::textColourId, colours::text);
        setColour (juce::ToggleButton::tickColourId, colours::accent);
        setColour (juce::ToggleButton::tickDisabledColourId, colours::dimText);
        setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff101215));
        setColour (juce::TextEditor::textColourId, colours::text);
        setColour (juce::TextEditor::outlineColourId, colours::panelEdge);
        setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float lineW = std::max (2.5f, radius * 0.14f);
        const float arcR = radius - lineW * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const auto fill = slider.findColour (juce::Slider::rotarySliderFillColourId)
                              .withMultipliedAlpha (slider.isEnabled() ? 1.0f : 0.35f);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (colours::knobBody.brighter (0.15f));
        g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // bipolar knobs (range symmetric around 0) fill from the centre
        const auto range = slider.getRange();
        const bool bipolar = range.getStart() < 0.0 && std::abs (range.getStart() + range.getEnd()) < 1.0e-6;
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, std::min (from, angle), std::max (from, angle), true);
        g.setColour (fill);
        g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float bodyR = arcR - lineW * 1.2f;
        g.setGradientFill (juce::ColourGradient (colours::knobBody.brighter (0.25f), centre.x, centre.y - bodyR,
                                                 colours::knobBody.darker (0.4f), centre.x, centre.y + bodyR, false));
        g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (centre));

        juce::Path pointer;
        pointer.addRoundedRectangle (-1.5f, -bodyR, 3.0f, bodyR * 0.5f, 1.5f);
        g.setColour (colours::text);
        g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const bool on = b.getToggleState();
        g.setColour (on ? colours::accent.withAlpha (0.85f) : colours::knobBody.brighter (highlighted ? 0.12f : 0.0f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? colours::accent : colours::panelEdge.brighter (0.2f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setColour (on ? juce::Colours::black : colours::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (b.getButtonText(), r, juce::Justification::centred);
    }
};
} // namespace dt::ui
