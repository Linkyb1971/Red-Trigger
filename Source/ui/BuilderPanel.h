#pragma once
#include "Widgets.h"
#include "builder/InstrumentBuilder.h"

namespace dt::ui
{
/** In-plugin page for building .dtz multisample instruments from a folder of one-shots. */
class BuilderPanel : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    BuilderPanel();

    std::function<void (const juce::File&)> onInstrumentSaved;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void addFiles (const juce::Array<juce::File>& files);
    void refresh();
    void save();

    InstrumentBuilder builder;
    BuildPlan plan;

    juce::TextButton addFilesButton { "Add Files..." }, addFolderButton { "Add Folder..." },
                     clearButton { "Clear" }, saveButton { "Save Instrument..." };
    juce::ComboBox modeBox;
    juce::Slider layersSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label modeLabel, layersLabel, nameLabel, helpLabel;
    juce::TextEditor nameEditor, summary;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragOver = false;
};
} // namespace dt::ui
