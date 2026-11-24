#pragma once
#include "Widgets.h"
#include "PluginProcessor.h"

namespace dt::ui
{
/** One strip per instrument channel: name, mic selector, level, pan, mute. */
class MicMixer : public juce::Component
{
public:
    explicit MicMixer (RedTriggerProcessor& p) : proc (p)
    {
        emptyLabel.setText ("Load an instrument to see its mic channels.", juce::dontSendNotification);
        emptyLabel.setColour (juce::Label::textColourId, colours::dimText);
        emptyLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (emptyLabel);
    }

    void setInstrument (const Instrument* inst)
    {
        rows.clear();
        const int n = inst != nullptr ? std::min (inst->numChannels(), maxChannels) : 0;
        for (int c = 0; c < n; ++c)
            addAndMakeVisible (rows.add (new Row (proc.apvts, c, inst->channels[(size_t) c])));
        emptyLabel.setVisible (n == 0);
        resized();
    }

    void paint (juce::Graphics& g) override
    {
        if (rows.isEmpty())
            return;
        g.setColour (colours::dimText);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        auto h = getLocalBounds().removeFromTop (headerH);
        for (auto [text, w] : { std::pair<const char*, int> { "CHANNEL", nameW }, { "MIC", micW } })
        {
            g.drawText (text, h.removeFromLeft (w), juce::Justification::centredLeft);
            h.removeFromLeft (gap);
        }
        auto right = h.removeFromRight (muteW);
        g.drawText ("MUTE", right, juce::Justification::centred);
        h.removeFromRight (gap);
        g.drawText ("PAN", h.removeFromRight (panW), juce::Justification::centredLeft);
        h.removeFromRight (gap);
        g.drawText ("LEVEL", h, juce::Justification::centredLeft);
    }

    void resized() override
    {
        emptyLabel.setBounds (getLocalBounds());
        auto r = getLocalBounds();
        r.removeFromTop (headerH);
        const int rowH = juce::jlimit (20, 28, rows.isEmpty() ? 24 : r.getHeight() / std::max (4, rows.size()));
        for (auto* row : rows)
            row->setBounds (r.removeFromTop (rowH).reduced (0, 2));
    }

private:
    static constexpr int headerH = 14, nameW = 80, micW = 120, panW = 96, muteW = 36, gap = 6;

    struct Row : public juce::Component
    {
        Row (APVTS& state, int channel, const ChannelDef& def)
            : level (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight),
              pan (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight)
        {
            name.setText (def.name, juce::dontSendNotification);
            name.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            name.setColour (juce::Label::textColourId, colours::text);
            addAndMakeVisible (name);

            for (int m = 0; m < def.mics.size(); ++m)
                mic.addItem (def.mics[m], m + 1);
            mic.setEnabled (def.mics.size() > 1);
            mic.setTooltip ("Choose which microphone is used for this position");
            addAndMakeVisible (mic);

            // Item k <-> mic parameter value k (JUCE's ComboBoxAttachment would rescale by item count).
            if (auto* p = state.getParameter (chMic (channel)))
            {
                const int numMics = def.mics.size();
                micAttachment = std::make_unique<juce::ParameterAttachment> (*p, [this, numMics] (float v)
                {
                    mic.setSelectedItemIndex (juce::jlimit (0, numMics - 1, juce::roundToInt (v)), juce::dontSendNotification);
                });
                mic.onChange = [this] { micAttachment->setValueAsCompleteGesture ((float) mic.getSelectedItemIndex()); };
                micAttachment->sendInitialUpdate();
            }

            for (auto* s : { &level, &pan })
            {
                s->setColour (juce::Slider::trackColourId, colours::accent2);
                s->setColour (juce::Slider::thumbColourId, colours::text);
                addAndMakeVisible (s);
            }
            level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 18);
            pan.setTextBoxStyle (juce::Slider::TextBoxRight, false, 36, 18);
            levelAttachment = std::make_unique<APVTS::SliderAttachment> (state, chLevel (channel), level);
            panAttachment = std::make_unique<APVTS::SliderAttachment> (state, chPan (channel), pan);
            level.setDoubleClickReturnValue (true, 0.0);
            pan.setDoubleClickReturnValue (true, 0.0);

            mute.setButtonText ("M");
            addAndMakeVisible (mute);
            muteAttachment = std::make_unique<APVTS::ButtonAttachment> (state, chMute (channel), mute);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            name.setBounds (r.removeFromLeft (nameW));
            r.removeFromLeft (gap);
            mic.setBounds (r.removeFromLeft (micW));
            r.removeFromLeft (gap);
            mute.setBounds (r.removeFromRight (muteW));
            r.removeFromRight (gap);
            pan.setBounds (r.removeFromRight (panW));
            r.removeFromRight (gap);
            level.setBounds (r);
        }

        juce::Label name;
        juce::ComboBox mic;
        juce::Slider level, pan;
        juce::ToggleButton mute;
        std::unique_ptr<juce::ParameterAttachment> micAttachment;
        std::unique_ptr<APVTS::SliderAttachment> levelAttachment, panAttachment;
        std::unique_ptr<APVTS::ButtonAttachment> muteAttachment;
    };

    RedTriggerProcessor& proc;
    juce::OwnedArray<Row> rows;
    juce::Label emptyLabel;
};
} // namespace dt::ui
