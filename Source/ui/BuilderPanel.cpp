#include "BuilderPanel.h"

namespace dt::ui
{
BuilderPanel::BuilderPanel()
{
    for (auto* b : { &addFilesButton, &addFolderButton, &clearButton, &saveButton })
        addAndMakeVisible (b);
    saveButton.setColour (juce::TextButton::buttonColourId, colours::accent.darker (0.2f));

    auto setupLabel = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, colours::dimText);
        l.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (l);
    };
    setupLabel (modeLabel, "LAYERING");
    setupLabel (layersLabel, "VELOCITY LAYERS");
    setupLabel (nameLabel, "INSTRUMENT NAME");

    modeBox.addItem ("Sort by loudness", 1);
    modeBox.addItem ("From filenames (v# / rr#)", 2);
    modeBox.setSelectedId (1, juce::dontSendNotification);
    modeBox.onChange = [this] { layersSlider.setEnabled (modeBox.getSelectedId() == 1); refresh(); };
    addAndMakeVisible (modeBox);

    layersSlider.setRange (1, 32, 1);
    layersSlider.setValue (5, juce::dontSendNotification);
    layersSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 40, 20);
    layersSlider.onValueChange = [this] { refresh(); };
    addAndMakeVisible (layersSlider);

    nameEditor.setTextToShowWhenEmpty ("My Snare", colours::dimText);
    addAndMakeVisible (nameEditor);

    summary.setMultiLine (true);
    summary.setReadOnly (true);
    summary.setScrollbarsShown (true);
    summary.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    addAndMakeVisible (summary);

    helpLabel.setText ("Drop WAV / AIFF / FLAC files or whole folders anywhere on this page.\n\n"
                       "Mic perspectives are matched by filename token:\n"
                       "   _OH / _Overhead  ->  Overhead\n"
                       "   _Room / _Amb     ->  Room\n"
                       "   _DI / _Close / none  ->  Direct\n"
                       "or by sub-folders named OH / Room.\n\n"
                       "Loudness mode sorts every hit by peak level and splits them into the chosen "
                       "number of velocity layers; hits within a layer become round robins.\n\n"
                       "Filename mode uses v1, v2... for layers and rr1, rr2... for round robins.\n\n"
                       "Instruments are saved as a single .dtz file (zip: instrument.xml + samples).",
                       juce::dontSendNotification);
    helpLabel.setColour (juce::Label::textColourId, colours::dimText);
    helpLabel.setFont (juce::FontOptions (12.0f));
    helpLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (helpLabel);

    addFilesButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Add samples", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc) { addFiles (fc.getResults()); });
    };

    addFolderButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Add a folder of samples");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc) { addFiles (fc.getResults()); });
    };

    clearButton.onClick = [this] { builder.clear(); refresh(); };
    saveButton.onClick = [this] { save(); };

    refresh();
}

void BuilderPanel::filesDropped (const juce::StringArray& paths, int, int)
{
    dragOver = false;
    juce::Array<juce::File> files;
    for (auto& p : paths)
        files.add (juce::File (p));
    addFiles (files);
}

void BuilderPanel::addFiles (const juce::Array<juce::File>& files)
{
    if (files.isEmpty())
        return;

    const bool wasEmpty = builder.getSamples().empty();
    builder.addFiles (files);

    if (wasEmpty && nameEditor.isEmpty() && ! builder.getSamples().empty())
    {
        const auto& first = files.getFirst();
        nameEditor.setText (first.isDirectory() ? first.getFileName() : first.getParentDirectory().getFileName());
    }
    refresh();
}

void BuilderPanel::refresh()
{
    const auto mode = modeBox.getSelectedId() == 2 ? InstrumentBuilder::Mode::byFilename
                                                   : InstrumentBuilder::Mode::byLoudness;
    plan = builder.makePlan (mode, (int) layersSlider.getValue());

    juce::String text;
    if (! builder.getSamples().empty())
        text << (int) builder.getSamples().size() << " files loaded\n";
    text << InstrumentBuilder::describePlan (plan);
    summary.setText (text, false);
    saveButton.setEnabled (! plan.layers.empty());
    repaint();
}

void BuilderPanel::save()
{
    auto name = nameEditor.getText().trim();
    if (name.isEmpty())
        name = "Untitled";

    const auto suggested = getDefaultInstrumentFolder().getChildFile (juce::File::createLegalFileName (name) + ".dtz");
    chooser = std::make_unique<juce::FileChooser> ("Save instrument", suggested, "*.dtz");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, name] (const juce::FileChooser& fc)
    {
        auto dest = fc.getResult();
        if (dest == juce::File())
            return;
        dest = dest.withFileExtension ("dtz");

        juce::String error;
        if (! InstrumentBuilder::writePackage (plan, name, dest, error))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save instrument", error);
            return;
        }
        if (onInstrumentSaved)
            onInstrumentSaved (dest);
    });
}

void BuilderPanel::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
    g.setColour (colours::accent);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("INSTRUMENT BUILDER", 12, 6, 300, 18, juce::Justification::centredLeft);
}

void BuilderPanel::paintOverChildren (juce::Graphics& g)
{
    if (dragOver)
    {
        g.setColour (colours::accent.withAlpha (0.15f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 6.0f);
        g.setColour (colours::accent);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 6.0f, 2.0f);
    }
}

void BuilderPanel::resized()
{
    auto r = getLocalBounds().reduced (12).withTrimmedTop (22);
    auto left = r.removeFromLeft (320);
    r.removeFromLeft (12);
    summary.setBounds (r);

    auto row = [&left] (int h) { auto a = left.removeFromTop (h); left.removeFromTop (6); return a; };

    auto buttons = row (28);
    addFilesButton.setBounds (buttons.removeFromLeft (110));
    buttons.removeFromLeft (6);
    addFolderButton.setBounds (buttons.removeFromLeft (110));
    buttons.removeFromLeft (6);
    clearButton.setBounds (buttons);

    modeLabel.setBounds (row (16));
    modeBox.setBounds (row (26));
    layersLabel.setBounds (row (16));
    layersSlider.setBounds (row (26));
    nameLabel.setBounds (row (16));
    nameEditor.setBounds (row (26));
    saveButton.setBounds (row (34));
    left.removeFromTop (6);
    helpLabel.setBounds (left);
}
} // namespace dt::ui
