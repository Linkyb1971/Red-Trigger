#include "EditorPanel.h"
#include "PluginProcessor.h"

namespace dt::ui
{
namespace
{
    void askForText (juce::Component* parent, const juce::String& title, const juce::String& initial,
                     std::function<void (const juce::String&)> onOk)
    {
        auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon, parent);
        w->addTextEditor ("text", initial);
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, onOk] (int result)
        {
            const auto text = w->getTextEditorContents ("text").trim();
            if (result == 1 && text.isNotEmpty())
                onOk (text);
        }), true);
    }

    juce::Array<juce::File> toFiles (const juce::StringArray& paths)
    {
        juce::Array<juce::File> files;
        for (auto& p : paths) files.add (juce::File (p));
        return files;
    }

    juce::File editorCacheFolder()
    {
        return getDefaultInstrumentFolder().getParentDirectory().getChildFile ("Editor Cache");
    }
}

//==============================================================================
void LayerGrid::updateSize (int minW, int minH)
{
    const int w = rowHeaderW + model.numColumns() * cellW + 8;
    const int h = headerH + (int) model.layers.size() * rowH + rowH * 2;
    setSize (std::max (w, minW), std::max (h, minH));
}

juce::Rectangle<int> LayerGrid::cellBounds (int layer, int column) const
{
    return { rowHeaderW + column * cellW, headerH + layer * rowH, cellW, rowH };
}

LayerGrid::Target LayerGrid::targetAt (juce::Point<int> p) const
{
    const int cols = model.numColumns(), rows = (int) model.layers.size();
    const int col = p.x >= rowHeaderW ? (p.x - rowHeaderW) / cellW : -1;
    const int row = p.y >= headerH ? (p.y - headerH) / rowH : -1;

    if (p.y < headerH && col >= 0 && col < cols)          return { Zone::columnHeader, -1, col };
    if (p.x < rowHeaderW && row >= 0 && row < rows)        return { Zone::layerHeader, row, -1 };
    if (row >= 0 && row < rows && col >= 0 && col < cols)  return { Zone::cell, row, col };
    return { Zone::background, -1, -1 };
}

void LayerGrid::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff121417));
    const int cols = model.numColumns(), rows = (int) model.layers.size();

    if (cols == 0 || rows == 0)
    {
        g.setColour (hover.zone != Zone::none ? colours::accent : colours::dimText);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawFittedText ("Drag a folder of drum samples here\n\n"
                          "Mic positions (Top, Bottom, OH, Room, In, Out, Sub...) and mic models\n"
                          "(SM57, MD421, C414...) are detected from folder and file names.\n"
                          "Hits are split into velocity layers by loudness or by v1/v2 tags,\n"
                          "and the hits in each layer become round robins.",
                          getLocalBounds().reduced (30), juce::Justification::centred, 8);
        if (hover.zone != Zone::none)
        {
            g.setColour (colours::accent);
            g.drawRect (getLocalBounds().reduced (4), 2);
        }
        return;
    }

    // column headers: channel name spanning its mics, mic names beneath
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    int col = 0;
    for (int c = 0; c < (int) model.channels.size(); ++c)
    {
        const auto& ch = model.channels[(size_t) c];
        juce::Rectangle<int> span (rowHeaderW + col * cellW, 0, ch.mics.size() * cellW, headerH / 2);
        g.setColour (colours::panel.brighter (0.08f));
        g.fillRect (span.reduced (1, 1));
        g.setColour (colours::accent);
        g.drawText (ch.name, span, juce::Justification::centred);

        for (int m = 0; m < ch.mics.size(); ++m, ++col)
        {
            juce::Rectangle<int> r (rowHeaderW + col * cellW, headerH / 2, cellW, headerH / 2);
            const bool sel = col == selectedColumn && selectedLayer < 0;
            const bool hov = hover.zone == Zone::columnHeader && hover.column == col;
            g.setColour (hov ? colours::accent.withAlpha (0.35f) : sel ? colours::accent2.withAlpha (0.3f) : colours::panel);
            g.fillRect (r.reduced (1, 1));
            g.setColour (colours::text);
            g.drawText (ch.mics[m], r.reduced (4, 0), juce::Justification::centred);
        }
    }

    // rows
    for (int l = 0; l < rows; ++l)
    {
        const auto& layer = model.layers[(size_t) l];
        juce::Rectangle<int> header (0, headerH + l * rowH, rowHeaderW, rowH);
        const bool hovRow = hover.zone == Zone::layerHeader && hover.layer == l;
        g.setColour (hovRow ? colours::accent.withAlpha (0.35f) : (l == selectedLayer && selectedColumn < 0) ? colours::accent2.withAlpha (0.3f) : colours::panel);
        g.fillRect (header.reduced (1, 1));
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("Layer " + juce::String (l + 1), header.reduced (8, 0).removeFromTop (rowH / 2 + 2), juce::Justification::bottomLeft);
        g.setColour (colours::dimText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("vel " + juce::String (layer.velLo) + "-" + juce::String (layer.velHi),
                    header.reduced (8, 0).removeFromBottom (rowH / 2), juce::Justification::topLeft);

        for (int c = 0; c < cols; ++c)
        {
            const auto [ch, mic] = model.columnToChannelMic (c);
            const int n = model.cellCount (l, ch, mic);
            auto r = cellBounds (l, c).reduced (2);
            const bool sel = l == selectedLayer && c == selectedColumn;
            const bool hov = hover.zone == Zone::cell && hover.layer == l && hover.column == c;

            g.setColour (hov ? colours::accent.withAlpha (0.4f) : n > 0 ? colours::knobBody.brighter (0.1f) : colours::knobBody.darker (0.3f));
            g.fillRoundedRectangle (r.toFloat(), 4.0f);
            if (sel)
            {
                g.setColour (colours::accent);
                g.drawRoundedRectangle (r.toFloat(), 4.0f, 2.0f);
            }

            g.setFont (juce::FontOptions (12.0f, n > 0 ? juce::Font::bold : juce::Font::plain));
            g.setColour (n > 0 ? colours::text : colours::dimText.withAlpha (0.5f));
            g.drawText (n > 0 ? juce::String (n) + (n == 1 ? " hit" : " RR") : juce::String ("drop"), r, juce::Justification::centred);
        }
    }

    // background drop hint below the rows
    auto below = getLocalBounds().withTrimmedTop (headerH + rows * rowH + 6);
    g.setColour (hover.zone == Zone::background ? colours::accent : colours::dimText.withAlpha (0.6f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Drop here to auto-import  |  on a mic header: spread over layers  |  on a layer: add to that layer  |  on a cell: add round robins",
                below.removeFromTop (24).withTrimmedLeft (8), juce::Justification::centredLeft);
}

void LayerGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto t = targetAt (e.getPosition());
    switch (t.zone)
    {
        case Zone::cell:         selectedLayer = t.layer; selectedColumn = t.column; break;
        case Zone::layerHeader:  selectedLayer = t.layer; selectedColumn = -1; break;
        case Zone::columnHeader: selectedLayer = -1; selectedColumn = t.column; break;
        case Zone::none: case Zone::background: break;
    }
    repaint();
    if (onSelectionChanged) onSelectionChanged();

    if (e.mods.isPopupMenu())
        showMenu (t);
}

void LayerGrid::showMenu (const Target& t)
{
    juce::PopupMenu menu;
    juce::Component::SafePointer<LayerGrid> safe (this);
    auto changed = [safe] { if (safe != nullptr && safe->onChanged) safe->onChanged(); };

    if (t.zone == Zone::columnHeader)
    {
        const auto [c, m] = model.columnToChannelMic (t.column);
        const auto chName = model.channels[(size_t) c].name;
        const auto micName = model.channels[(size_t) c].mics[m];
        menu.addSectionHeader (chName + " / " + micName);
        menu.addItem ("Rename channel '" + chName + "'...", [this, c = c, chName, changed]
                      { askForText (this, "Rename channel", chName, [this, c, changed] (auto s) { model.renameChannel (c, s); changed(); }); });
        menu.addItem ("Rename mic '" + micName + "'...", [this, c = c, m = m, micName, changed]
                      { askForText (this, "Rename mic", micName, [this, c, m, changed] (auto s) { model.renameMic (c, m, s); changed(); }); });
        menu.addItem ("Add another mic to '" + chName + "'...", model.channels[(size_t) c].mics.size() < maxMicsPerChannel,
                      false, [this, c = c, changed]
                      { askForText (this, "New mic name (e.g. MD421)", {}, [this, c, changed] (auto s) { model.findOrAddMic (c, s); changed(); }); });
        menu.addSeparator();
        menu.addItem ("Remove mic '" + micName + "' (and its samples)", [this, c = c, m = m, changed] { model.removeMic (c, m); changed(); });
        menu.addItem ("Remove channel '" + chName + "' (and its samples)", [this, c = c, changed] { model.removeChannel (c); changed(); });
    }
    else if (t.zone == Zone::layerHeader)
    {
        menu.addItem ("Remove layer " + juce::String (t.layer + 1), [this, l = t.layer, changed] { model.removeLayer (l); changed(); });
    }
    else if (t.zone == Zone::cell)
    {
        const auto [c, m] = model.columnToChannelMic (t.column);
        menu.addItem ("Clear cell", [this, l = t.layer, c = c, m = m, changed] { model.clearCell (l, c, m); changed(); });
    }
    else
        return;

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void LayerGrid::fileDragMove (const juce::StringArray&, int x, int y)
{
    hover = targetAt ({ x, y });
    if (model.numColumns() == 0 || model.layers.empty())
        hover.zone = Zone::background;
    repaint();
}

void LayerGrid::filesDropped (const juce::StringArray& paths, int x, int y)
{
    auto t = targetAt ({ x, y });
    if (model.numColumns() == 0 || model.layers.empty())
        t.zone = Zone::background;
    hover = {};

    const auto files = toFiles (paths);
    const auto mode = getLayering ? getLayering() : InstrumentModel::Layering::byLoudness;
    const int numLayers = getNumLayers ? getNumLayers() : 5;

    switch (t.zone)
    {
        case Zone::cell:
        {
            const auto [c, m] = model.columnToChannelMic (t.column);
            juce::Array<juce::File> audio;
            for (auto& f : files)
            {
                if (f.isDirectory()) audio.addArray (f.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg"));
                else audio.add (f);
            }
            model.messages.clear();
            model.addToCell (t.layer, c, m, audio);
            selectedLayer = t.layer; selectedColumn = t.column;
            break;
        }
        case Zone::layerHeader:   model.importToLayer (t.layer, files); selectedLayer = t.layer; selectedColumn = -1; break;
        case Zone::columnHeader:
        {
            const auto [c, m] = model.columnToChannelMic (t.column);
            model.importToColumn (c, m, files, mode, numLayers);
            break;
        }
        case Zone::none: case Zone::background: model.autoImport (files, mode, numLayers); break;
    }

    if (onChanged) onChanged();
    if (onSelectionChanged) onSelectionChanged();
}

//==============================================================================
EditorPanel::EditorPanel (RedTriggerProcessor& p) : proc (p)
{
    viewport.setViewedComponent (&grid, false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);

    grid.onChanged = [this] { refresh(); };
    grid.onSelectionChanged = [this] { selectionChanged(); };
    grid.getLayering = [this] { return layering(); };
    grid.getNumLayers = [this] { return (int) layersSlider.getValue(); };

    auto label = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, colours::dimText);
        addAndMakeVisible (l);
    };
    label (nameLabel, "NAME");
    label (layeringLabel, "AUTO LAYERING");
    label (layersLabel, "LAYERS");
    label (selectionLabel, "Nothing selected");
    label (velLabel, "VELOCITY RANGE");
    selectionLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    selectionLabel.setColour (juce::Label::textColourId, colours::text);
    statusLabel.setColour (juce::Label::textColourId, colours::dimText);
    statusLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (statusLabel);

    nameEditor.setTextToShowWhenEmpty ("Instrument name", colours::dimText);
    nameEditor.onTextChange = [this] { model.name = nameEditor.getText().trim(); };
    addAndMakeVisible (nameEditor);

    layeringBox.addItem ("By loudness", 1);
    layeringBox.addItem ("By v#/rr# tags", 2);
    layeringBox.setSelectedId (1, juce::dontSendNotification);
    addAndMakeVisible (layeringBox);

    for (auto* s : { &layersSlider, &velLo, &velHi })
    {
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 22);
        s->setColour (juce::Slider::textBoxOutlineColourId, colours::panelEdge);
        addAndMakeVisible (s);
    }
    layersSlider.setRange (1, 32, 1);
    layersSlider.setValue (5, juce::dontSendNotification);
    velLo.setRange (1, 127, 1);
    velHi.setRange (1, 127, 1);
    velLo.onValueChange = [this]
    {
        if (grid.selectedLayer >= 0 && grid.selectedLayer < (int) model.layers.size())
        {
            auto& l = model.layers[(size_t) grid.selectedLayer];
            l.velLo = (int) velLo.getValue();
            l.velHi = std::max (l.velHi, l.velLo);
            velHi.setValue (l.velHi, juce::dontSendNotification);
            grid.repaint();
        }
    };
    velHi.onValueChange = [this]
    {
        if (grid.selectedLayer >= 0 && grid.selectedLayer < (int) model.layers.size())
        {
            auto& l = model.layers[(size_t) grid.selectedLayer];
            l.velHi = (int) velHi.getValue();
            l.velLo = std::min (l.velHi, l.velLo);
            velLo.setValue (l.velLo, juce::dontSendNotification);
            grid.repaint();
        }
    };

    fileList.setRowHeight (22);
    fileList.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff101215));
    fileList.setOutlineThickness (1);
    fileList.setColour (juce::ListBox::outlineColourId, colours::panelEdge);
    addAndMakeVisible (fileList);

    for (auto* b : { &newButton, &openButton, &editLoadedButton, &saveButton, &importFilesButton, &importFolderButton,
                     &addChannelButton, &addLayerButton, &removeLayerButton, &evenVelButton, &sortButton,
                     &playButton, &upButton, &downButton, &removeButton, &clearCellButton })
        addAndMakeVisible (b);
    saveButton.setColour (juce::TextButton::buttonColourId, colours::accent.darker (0.2f));

    newButton.onClick = [this] { model.clear(); nameEditor.clear(); model.name = {}; grid.selectedLayer = grid.selectedColumn = -1; refresh(); };

    openButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open instrument to edit", getDefaultInstrumentFolder(), "*.dtz;*.xml");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) openInstrument (fc.getResult()); });
    };

    editLoadedButton.onClick = [this]
    {
        if (proc.getInstrumentFile().existsAsFile())
            openInstrument (proc.getInstrumentFile());
        else
            statusLabel.setText ("No instrument is loaded in the plugin.", juce::dontSendNotification);
    };

    importFilesButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Add samples", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc) { importFiles (fc.getResults()); });
    };

    importFolderButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Add a folder of samples");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc) { importFiles (fc.getResults()); });
    };

    addChannelButton.onClick = [this]
    {
        askForText (this, "New mic channel (e.g. Top, Bottom, OH, Room)", {}, [this] (const juce::String& n)
        {
            if (const int c = model.findOrAddChannel (n); c >= 0 && model.channels[(size_t) c].mics.isEmpty())
                model.findOrAddMic (c, "Default");
            if (model.layers.empty())
                model.addLayer();
            refresh();
        });
    };

    addLayerButton.onClick = [this] { model.addLayer(); refresh(); };
    removeLayerButton.onClick = [this]
    {
        if (! model.layers.empty())
            model.removeLayer (grid.selectedLayer >= 0 ? grid.selectedLayer : (int) model.layers.size() - 1);
        grid.selectedLayer = -1;
        refresh();
    };
    evenVelButton.onClick = [this] { model.evenVelocitySplit(); refresh(); };
    sortButton.onClick = [this] { model.sortLayersByLoudness(); refresh(); };

    playButton.onClick = [this]
    {
        const auto files = selectedCellFiles();
        const int row = fileList.getSelectedRow();
        if (! files.isEmpty())
            proc.previewFile (files[juce::jlimit (0, files.size() - 1, row)]);
    };
    upButton.onClick = [this]
    {
        const auto [c, m] = selectedChannelMic();
        const int row = fileList.getSelectedRow();
        model.moveInCell (grid.selectedLayer, c, m, row, -1);
        refresh();
        fileList.selectRow (std::max (0, row - 1));
    };
    downButton.onClick = [this]
    {
        const auto [c, m] = selectedChannelMic();
        const int row = fileList.getSelectedRow();
        model.moveInCell (grid.selectedLayer, c, m, row, 1);
        refresh();
        fileList.selectRow (std::min (getNumRows() - 1, row + 1));
    };
    removeButton.onClick = [this]
    {
        const auto [c, m] = selectedChannelMic();
        model.removeFromCell (grid.selectedLayer, c, m, fileList.getSelectedRow());
        refresh();
    };
    clearCellButton.onClick = [this]
    {
        const auto [c, m] = selectedChannelMic();
        model.clearCell (grid.selectedLayer, c, m);
        refresh();
    };

    saveButton.onClick = [this] { save(); };
    refresh();
}

InstrumentModel::Layering EditorPanel::layering() const
{
    return layeringBox.getSelectedId() == 2 ? InstrumentModel::Layering::byFilename : InstrumentModel::Layering::byLoudness;
}

void EditorPanel::importFiles (const juce::Array<juce::File>& files)
{
    if (files.isEmpty()) return;
    const bool wasEmpty = model.isEmpty();
    model.autoImport (files, layering(), (int) layersSlider.getValue());
    if (wasEmpty && nameEditor.isEmpty())
    {
        const auto& f = files.getFirst();
        nameEditor.setText (f.isDirectory() ? f.getFileName() : f.getParentDirectory().getFileName());
    }
    refresh();
}

void EditorPanel::openInstrument (const juce::File& f)
{
    juce::String error;
    if (! model.loadFromFile (f, editorCacheFolder(), error))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't open instrument", error);
        return;
    }
    nameEditor.setText (model.name, juce::dontSendNotification);
    grid.selectedLayer = grid.selectedColumn = -1;
    refresh();
    statusLabel.setText ("Editing " + f.getFileName() + " - save to update it or save under a new name.", juce::dontSendNotification);
}

void EditorPanel::save()
{
    auto name = nameEditor.getText().trim();
    if (name.isEmpty()) name = "Untitled";
    model.name = name;

    const auto suggested = getDefaultInstrumentFolder().getChildFile (juce::File::createLegalFileName (name) + ".dtz");
    chooser = std::make_unique<juce::FileChooser> ("Save instrument", suggested, "*.dtz");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto dest = fc.getResult();
        if (dest == juce::File()) return;
        dest = dest.withFileExtension ("dtz");

        juce::String error;
        if (! model.writePackage (dest, error))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save instrument", error);
            return;
        }
        statusLabel.setText ("Saved " + dest.getFileName(), juce::dontSendNotification);
        if (onInstrumentSaved) onInstrumentSaved (dest);
    });
}

void EditorPanel::refresh()
{
    grid.selectedLayer = std::min (grid.selectedLayer, (int) model.layers.size() - 1);
    grid.selectedColumn = std::min (grid.selectedColumn, model.numColumns() - 1);
    grid.updateSize (viewport.getMaximumVisibleWidth(), viewport.getMaximumVisibleHeight());
    grid.repaint();
    selectionChanged();

    juce::String s;
    if (! model.isEmpty())
        s << (int) model.layers.size() << " layers, " << model.totalHits() << " hits, "
          << (int) model.channels.size() << " channels, " << model.numColumns() << " mics.  ";
    if (! model.messages.isEmpty())
        s << model.messages.joinIntoString ("  ");
    statusLabel.setText (s, juce::dontSendNotification);
    saveButton.setEnabled (! model.isEmpty());
}

juce::Array<juce::File> EditorPanel::selectedCellFiles() const
{
    if (grid.selectedLayer < 0 || grid.selectedColumn < 0) return {};
    const auto [c, m] = selectedChannelMic();
    return model.cellFiles (grid.selectedLayer, c, m);
}

void EditorPanel::selectionChanged()
{
    const bool hasLayer = grid.selectedLayer >= 0 && grid.selectedLayer < (int) model.layers.size();
    const bool hasCell = hasLayer && grid.selectedColumn >= 0;

    juce::String text = "Click a cell to see its samples";
    if (hasLayer || grid.selectedColumn >= 0)
    {
        text = hasLayer ? "Layer " + juce::String (grid.selectedLayer + 1) : juce::String ("All layers");
        if (grid.selectedColumn >= 0)
        {
            const auto [c, m] = selectedChannelMic();
            text << "  -  " << model.channels[(size_t) c].name << " / " << model.channels[(size_t) c].mics[m];
        }
    }
    selectionLabel.setText (text, juce::dontSendNotification);

    velLo.setEnabled (hasLayer);
    velHi.setEnabled (hasLayer);
    if (hasLayer)
    {
        velLo.setValue (model.layers[(size_t) grid.selectedLayer].velLo, juce::dontSendNotification);
        velHi.setValue (model.layers[(size_t) grid.selectedLayer].velHi, juce::dontSendNotification);
    }

    for (auto* b : { &playButton, &upButton, &downButton, &removeButton, &clearCellButton })
        b->setEnabled (hasCell);
    removeLayerButton.setEnabled (! model.layers.empty());

    fileList.updateContent();
    fileList.repaint();
}

int EditorPanel::getNumRows() { return selectedCellFiles().size(); }

void EditorPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    const auto files = selectedCellFiles();
    if (row < 0 || row >= files.size()) return;
    if (selected) g.fillAll (colours::accent.withAlpha (0.3f));
    g.setColour (colours::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("RR" + juce::String (row + 1), 6, 0, 40, h, juce::Justification::centredLeft);
    g.setColour (colours::text);
    g.drawText (files[row].getFileName(), 46, 0, w - 100, h, juce::Justification::centredLeft, true);
    g.setColour (colours::dimText);
    g.drawText (juce::String (model.peakDb (files[row]), 1) + " dB", w - 60, 0, 54, h, juce::Justification::centredRight);
}

void EditorPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    const auto files = selectedCellFiles();
    if (row >= 0 && row < files.size())
        proc.previewFile (files[row]);
}

void EditorPanel::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
    g.setColour (colours::accent);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("INSTRUMENT EDITOR", 12, 6, 300, 18, juce::Justification::centredLeft);
}

void EditorPanel::resized()
{
    auto r = getLocalBounds().reduced (12).withTrimmedTop (22);

    // toolbar row 1: name + file operations
    {
        auto row = r.removeFromTop (26);
        nameLabel.setBounds (row.removeFromLeft (42));
        nameEditor.setBounds (row.removeFromLeft (220));
        row.removeFromLeft (10);
        for (auto* b : { &newButton, &openButton, &editLoadedButton })
        {
            b->setBounds (row.removeFromLeft (b == &editLoadedButton ? 96 : 66));
            row.removeFromLeft (6);
        }
        saveButton.setBounds (row.removeFromRight (150));
    }
    r.removeFromTop (8);

    // toolbar row 2: import + structure
    {
        auto row = r.removeFromTop (26);
        for (auto* b : { &importFilesButton, &importFolderButton })
        {
            b->setBounds (row.removeFromLeft (92));
            row.removeFromLeft (6);
        }
        row.removeFromLeft (8);
        layeringLabel.setBounds (row.removeFromLeft (98));
        layeringBox.setBounds (row.removeFromLeft (130));
        row.removeFromLeft (8);
        layersLabel.setBounds (row.removeFromLeft (48));
        layersSlider.setBounds (row.removeFromLeft (96));
        row.removeFromLeft (14);
        for (auto* b : { &addChannelButton, &addLayerButton, &removeLayerButton, &evenVelButton, &sortButton })
        {
            b->setBounds (row.removeFromLeft (b == &evenVelButton || b == &sortButton ? 112 : 76));
            row.removeFromLeft (6);
        }
    }
    r.removeFromTop (8);

    statusLabel.setBounds (r.removeFromBottom (20));
    r.removeFromBottom (4);

    // side panel
    auto side = r.removeFromRight (300);
    r.removeFromRight (10);
    selectionLabel.setBounds (side.removeFromTop (22));
    side.removeFromTop (6);
    velLabel.setBounds (side.removeFromTop (16));
    {
        auto v = side.removeFromTop (26);
        velLo.setBounds (v.removeFromLeft (140));
        v.removeFromLeft (8);
        velHi.setBounds (v.removeFromLeft (140));
    }
    side.removeFromTop (10);
    auto buttons = side.removeFromBottom (60);
    {
        auto b1 = buttons.removeFromTop (26);
        const int w = b1.getWidth() / 4;
        for (auto* b : { &playButton, &upButton, &downButton, &removeButton })
            b->setBounds (b1.removeFromLeft (w).reduced (2, 0));
        buttons.removeFromTop (6);
        clearCellButton.setBounds (buttons.removeFromTop (26).reduced (2, 0));
    }
    side.removeFromBottom (6);
    fileList.setBounds (side);

    viewport.setBounds (r);
    grid.updateSize (viewport.getMaximumVisibleWidth(), viewport.getMaximumVisibleHeight());
}
} // namespace dt::ui
