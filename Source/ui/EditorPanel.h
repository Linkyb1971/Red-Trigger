#pragma once
#include "Widgets.h"
#include "builder/InstrumentModel.h"

class RedTriggerProcessor;

namespace dt::ui
{
/**
    Grid of velocity layers (rows) x mic channels/mics (columns).
    Drag & drop targets:
      cell            -> files become round robins of that layer/mic
      layer header    -> files go into that layer, channel/mic detected per file
      column header   -> files are spread over the layers of that mic (by loudness / v# tags)
      anywhere else   -> full auto import (channels, mics, layers detected)
*/
class LayerGrid : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit LayerGrid (InstrumentModel& m) : model (m) {}

    std::function<void()> onChanged, onSelectionChanged;
    std::function<InstrumentModel::Layering()> getLayering;
    std::function<int()> getNumLayers;

    int selectedLayer = -1, selectedColumn = -1;

    void updateSize (int minW, int minH);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void fileDragEnter (const juce::StringArray&, int x, int y) override { fileDragMove ({}, x, y); }
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override { hover = {}; repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    static constexpr int rowHeaderW = 130, headerH = 46, cellW = 116, rowH = 36;

private:
    enum class Zone { none, cell, layerHeader, columnHeader, background };
    struct Target { Zone zone = Zone::none; int layer = -1, column = -1; };

    Target targetAt (juce::Point<int> p) const;
    juce::Rectangle<int> cellBounds (int layer, int column) const;
    void showMenu (const Target& t);

    InstrumentModel& model;
    Target hover;
};

/** The Instrument Editor page. */
class EditorPanel : public juce::Component, private juce::ListBoxModel
{
public:
    explicit EditorPanel (RedTriggerProcessor& p);

    std::function<void (const juce::File&)> onInstrumentSaved;

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Adds files as if dropped on the empty part of the grid (used by tests/import buttons). */
    void importFiles (const juce::Array<juce::File>& files);
    InstrumentModel& getModel() { return model; }
    void refresh();

private:
    // ListBoxModel (files in the selected cell)
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    void selectionChanged();
    void openInstrument (const juce::File& f);
    void save();
    juce::Array<juce::File> selectedCellFiles() const;
    std::pair<int, int> selectedChannelMic() const { return model.columnToChannelMic (grid.selectedColumn); }
    InstrumentModel::Layering layering() const;

    RedTriggerProcessor& proc;
    InstrumentModel model;
    LayerGrid grid { model };
    juce::Viewport viewport;

    juce::TextEditor nameEditor;
    juce::TextButton newButton { "New" }, openButton { "Open..." }, editLoadedButton { "Edit Loaded" },
                     saveButton { "Save Instrument..." }, importFilesButton { "Add Files..." }, importFolderButton { "Add Folder..." },
                     addChannelButton { "+ Channel" }, addLayerButton { "+ Layer" }, removeLayerButton { "- Layer" },
                     evenVelButton { "Even Velocities" }, sortButton { "Sort by Loudness" };
    juce::ComboBox layeringBox;
    juce::Slider layersSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label nameLabel, layeringLabel, layersLabel, statusLabel;

    // side panel
    juce::Label selectionLabel, velLabel;
    juce::Slider velLo { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft },
                 velHi { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::ListBox fileList { "files", this };
    juce::TextButton playButton { "Play" }, upButton { "Up" }, downButton { "Down" }, removeButton { "Remove" },
                     clearCellButton { "Clear Cell" };

    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace dt::ui
