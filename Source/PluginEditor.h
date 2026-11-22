#pragma once
#include "PluginProcessor.h"
#include "ui/Widgets.h"
#include "ui/DetectorDisplay.h"
#include "ui/BuilderPanel.h"

class RedTriggerEditor : public juce::AudioProcessorEditor,
                          private juce::Timer,
                          private juce::ChangeListener
{
public:
    explicit RedTriggerEditor (RedTriggerProcessor&);
    ~RedTriggerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshInstrumentList();
    void updateInstrumentInfo();
    void showBuilder (bool show);
    void loadFile (const juce::File& f);

    RedTriggerProcessor& proc;
    dt::ui::LookAndFeel lnf;

    // header
    juce::ComboBox instrumentBox;
    juce::TextButton loadButton { "Load..." }, folderButton { "Folder" }, builderButton { "Instrument Builder" };
    juce::Label infoLabel, velocityLabel;
    juce::Array<juce::File> instrumentFiles;
    std::unique_ptr<juce::FileChooser> chooser;
    float ledLevel = 0.0f;
    int lastHitCount = 0;

    // panels
    dt::ui::Panel detectPanel { "DETECTION" }, samplePanel { "SAMPLE" }, eqPanel { "TRIGGER EQ" },
                  compPanel { "COMPRESSOR", "dbx 160 style" }, outPanel { "MIX" };
    dt::ui::DetectorDisplay display { proc };
    dt::ui::BuilderPanel builder;

    using Knob = dt::ui::Knob;
    using Toggle = dt::ui::Toggle;
    std::vector<std::unique_ptr<Knob>> detectKnobs, sampleKnobs, eqKnobs, compKnobs, outKnobs;
    Toggle listenToggle, alignToggle, chokeToggle, polarityToggle, eqOnToggle, compOnToggle, overEasyToggle;
    juce::ComboBox rrBox;
    juce::Label rrLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rrAttachment;

    dt::ui::Meter inMeter { "IN" }, trigMeter { "TRIG" }, outMeter { "OUT" }, grMeter { "GR", true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RedTriggerEditor)
};
