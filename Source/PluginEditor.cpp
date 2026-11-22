#include "PluginEditor.h"

using namespace dt;
namespace col = dt::ui::colours;

DrumTriggerEditor::DrumTriggerEditor (DrumTriggerProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      listenToggle   (p.apvts, pid::listen, "LISTEN"),
      alignToggle    (p.apvts, pid::align, "LATENCY COMP"),
      chokeToggle    (p.apvts, pid::choke, "CHOKE"),
      polarityToggle (p.apvts, pid::polarity, "INVERT"),
      eqOnToggle     (p.apvts, pid::eqOn, "EQ ON"),
      compOnToggle   (p.apvts, pid::compOn, "COMP ON"),
      overEasyToggle (p.apvts, pid::compOverEasy, "OVEREASY")
{
    setLookAndFeel (&lnf);
    auto& s = p.apvts;

    auto make = [&s] (std::vector<std::unique_ptr<Knob>>& list, const char* id, const char* caption,
                      juce::Colour c = col::accent)
    {
        list.push_back (std::make_unique<Knob> (s, id, caption, c));
    };

    make (detectKnobs, pid::threshold, "THRESHOLD");
    make (detectKnobs, pid::ceiling, "CEILING");
    make (detectKnobs, pid::retrigger, "RETRIGGER");
    make (detectKnobs, pid::adaptive, "ADAPTIVE");
    make (detectKnobs, pid::detHpf, "DETECT HPF", col::accent2);
    make (detectKnobs, pid::detLpf, "DETECT LPF", col::accent2);

    make (sampleKnobs, pid::dynamics, "DYNAMICS");
    make (sampleKnobs, pid::pitch, "PITCH");
    make (sampleKnobs, pid::length, "LENGTH");
    make (sampleKnobs, pid::offset, "OFFSET");
    make (sampleKnobs, pid::micDirect, "DIRECT", col::accent2);
    make (sampleKnobs, pid::micOverhead, "OVERHEAD", col::accent2);
    make (sampleKnobs, pid::micRoom, "ROOM", col::accent2);

    make (eqKnobs, pid::eqHpf, "HPF", col::accent2);
    make (eqKnobs, pid::eqLowFreq, "LOW FREQ");
    make (eqKnobs, pid::eqLowGain, "LOW GAIN");
    make (eqKnobs, pid::eqHighFreq, "HIGH FREQ");
    make (eqKnobs, pid::eqHighGain, "HIGH GAIN");
    make (eqKnobs, pid::eqMidFreq, "MID FREQ");
    make (eqKnobs, pid::eqMidGain, "MID GAIN");
    make (eqKnobs, pid::eqMidQ, "MID Q");
    make (eqKnobs, pid::eqLpf, "LPF", col::accent2);

    make (compKnobs, pid::compThresh, "THRESHOLD");
    make (compKnobs, pid::compRatio, "RATIO");
    make (compKnobs, pid::compOutput, "OUTPUT");
    make (compKnobs, pid::compMix, "MIX");

    make (outKnobs, pid::trigLevel, "TRIG LEVEL");
    make (outKnobs, pid::blend, "BLEND");
    make (outKnobs, pid::outGain, "OUTPUT");

    for (auto* panel : { &detectPanel, &samplePanel, &eqPanel, &compPanel, &outPanel })
        addAndMakeVisible (panel);

    for (auto& k : detectKnobs) detectPanel.addAndMakeVisible (*k);
    for (auto& k : sampleKnobs) samplePanel.addAndMakeVisible (*k);
    for (auto& k : eqKnobs)     eqPanel.addAndMakeVisible (*k);
    for (auto& k : compKnobs)   compPanel.addAndMakeVisible (*k);
    for (auto& k : outKnobs)    outPanel.addAndMakeVisible (*k);

    detectPanel.addAndMakeVisible (listenToggle);
    detectPanel.addAndMakeVisible (alignToggle);
    detectPanel.addAndMakeVisible (velocityLabel);
    samplePanel.addAndMakeVisible (chokeToggle);
    samplePanel.addAndMakeVisible (polarityToggle);
    samplePanel.addAndMakeVisible (rrBox);
    samplePanel.addAndMakeVisible (rrLabel);
    eqPanel.addAndMakeVisible (eqOnToggle);
    compPanel.addAndMakeVisible (compOnToggle);
    compPanel.addAndMakeVisible (overEasyToggle);
    compPanel.addAndMakeVisible (grMeter);
    outPanel.addAndMakeVisible (inMeter);
    outPanel.addAndMakeVisible (trigMeter);
    outPanel.addAndMakeVisible (outMeter);

    listenToggle.setTooltip ("Hear the filtered detector signal");
    alignToggle.setTooltip ("Delay the dry signal by the 3 ms velocity window so samples land exactly on the transient");

    rrLabel.setText ("ROUND ROBIN", juce::dontSendNotification);
    rrLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    rrLabel.setColour (juce::Label::textColourId, col::dimText);
    rrLabel.setJustificationType (juce::Justification::centred);
    rrBox.addItemList ({ "Cycle", "Random" }, 1);
    rrAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, pid::rrMode, rrBox);

    velocityLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    velocityLabel.setColour (juce::Label::textColourId, col::text);
    velocityLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (display);
    addChildComponent (builder);
    builder.onInstrumentSaved = [this] (const juce::File& f)
    {
        loadFile (f);
        showBuilder (false);
    };

    // header
    addAndMakeVisible (instrumentBox);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (folderButton);
    addAndMakeVisible (builderButton);
    addAndMakeVisible (infoLabel);
    infoLabel.setFont (juce::FontOptions (12.0f));
    infoLabel.setColour (juce::Label::textColourId, col::dimText);

    instrumentBox.setTextWhenNothingSelected ("No instrument loaded");
    instrumentBox.onChange = [this]
    {
        const int id = instrumentBox.getSelectedId();
        if (id == 1)
            loadFile ({});
        else if (id >= 2 && id - 2 < instrumentFiles.size() && instrumentFiles[id - 2] != proc.getInstrumentFile())
            loadFile (instrumentFiles[id - 2]);
    };

    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Load trigger instrument", getDefaultInstrumentFolder(), "*.dtz;*.xml");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult() != juce::File())
                                      loadFile (fc.getResult());
                              });
    };

    folderButton.setTooltip ("Open the instruments folder (instruments saved here appear in the list)");
    folderButton.onClick = [] { getDefaultInstrumentFolder().startAsProcess(); };

    builderButton.setClickingTogglesState (true);
    builderButton.onClick = [this] { showBuilder (builderButton.getToggleState()); };

    proc.addChangeListener (this);
    refreshInstrumentList();
    updateInstrumentInfo();

    setSize (1120, 700);
    startTimerHz (30);
}

DrumTriggerEditor::~DrumTriggerEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void DrumTriggerEditor::loadFile (const juce::File& f)
{
    juce::String msg;
    if (! proc.loadInstrument (f, msg))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't load instrument", msg);
    refreshInstrumentList();
    updateInstrumentInfo();
}

void DrumTriggerEditor::showBuilder (bool show)
{
    builderButton.setToggleState (show, juce::dontSendNotification);
    builder.setVisible (show);
    for (juce::Component* c : { (juce::Component*) &display, (juce::Component*) &detectPanel, (juce::Component*) &samplePanel,
                                (juce::Component*) &eqPanel, (juce::Component*) &compPanel, (juce::Component*) &outPanel })
        c->setVisible (! show);
}

void DrumTriggerEditor::refreshInstrumentList()
{
    instrumentFiles = findInstrumentsInDefaultFolder();
    const auto current = proc.getInstrumentFile();
    if (current != juce::File() && ! instrumentFiles.contains (current))
        instrumentFiles.insert (0, current);

    instrumentBox.clear (juce::dontSendNotification);
    instrumentBox.addItem ("(none)", 1);
    instrumentBox.addSeparator();

    const auto root = getDefaultInstrumentFolder();
    for (int i = 0; i < instrumentFiles.size(); ++i)
    {
        const auto& f = instrumentFiles[i];
        auto label = f.isAChildOf (root) ? f.getRelativePathFrom (root).upToLastOccurrenceOf (".", false, false)
                                         : f.getFileNameWithoutExtension();
        instrumentBox.addItem (label, i + 2);
    }

    const int idx = instrumentFiles.indexOf (current);
    instrumentBox.setSelectedId (idx >= 0 ? idx + 2 : 0, juce::dontSendNotification);
}

void DrumTriggerEditor::updateInstrumentInfo()
{
    auto inst = proc.getLoadedInstrument();
    juce::String text;
    if (inst != nullptr)
        text = inst->name + "  -  " + inst->getSummary();
    else if (proc.getInstrumentFile() != juce::File())
        text = "Missing: " + proc.getInstrumentFile().getFileName();
    else
        text = "Load a .dtz instrument, or build one from your samples with the Instrument Builder.";

    if (proc.getLastLoadError().isNotEmpty())
        text += "   (" + proc.getLastLoadError() + ")";
    infoLabel.setText (text, juce::dontSendNotification);

    // Dim mic knobs the instrument doesn't have.
    for (int m = 0; m < numMics; ++m)
        sampleKnobs[(size_t) (4 + m)]->setAlpha (inst == nullptr || inst->micPresent[(size_t) m] ? 1.0f : 0.35f);
}

void DrumTriggerEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshInstrumentList();
    updateInstrumentInfo();
}

void DrumTriggerEditor::timerCallback()
{
    inMeter.update (proc.inputPeak.exchange (0.0f));
    trigMeter.update (proc.triggerPeak.exchange (0.0f));
    outMeter.update (proc.outputPeak.exchange (0.0f));
    grMeter.update (proc.compGainReduction.exchange (0.0f));

    const int hits = proc.hitCounter.load();
    if (hits != lastHitCount)
    {
        lastHitCount = hits;
        ledLevel = 1.0f;
        const int layer = proc.lastLayer.load();
        velocityLabel.setText ("VEL " + juce::String ((int) proc.lastVelocity.load())
                                   + (layer >= 0 ? "  LAYER " + juce::String (layer + 1) : juce::String()),
                               juce::dontSendNotification);
    }
    ledLevel *= 0.8f;
    repaint (getLocalBounds().removeFromTop (52));

    if (display.isVisible())
        display.tick();
}

void DrumTriggerEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::background);

    auto header = getLocalBounds().removeFromTop (52).reduced (12, 0);
    g.setColour (col::text);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("DRUM", header.removeFromLeft (64), juce::Justification::centredLeft);
    g.setColour (col::accent);
    g.drawText ("TRIGGER", header.removeFromLeft (110), juce::Justification::centredLeft);

    // hit LED
    auto led = juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ (float) getWidth() - 26.0f, 26.0f });
    g.setColour (col::accent.withAlpha (0.15f + 0.85f * ledLevel));
    g.fillEllipse (led);
    g.setColour (col::panelEdge);
    g.drawEllipse (led, 1.0f);
}

void DrumTriggerEditor::resized()
{
    // header
    {
        auto h = getLocalBounds().removeFromTop (52).reduced (12, 6);
        h.removeFromLeft (180);
        h.removeFromRight (30);
        auto top = h.removeFromTop (24);
        instrumentBox.setBounds (top.removeFromLeft (360));
        top.removeFromLeft (6);
        loadButton.setBounds (top.removeFromLeft (70));
        top.removeFromLeft (6);
        folderButton.setBounds (top.removeFromLeft (60));
        top.removeFromLeft (6);
        builderButton.setBounds (top.removeFromLeft (150));
        infoLabel.setBounds (h.withTrimmedLeft (-4));
    }

    auto layoutRow = [] (juce::Rectangle<int> area, std::vector<std::unique_ptr<Knob>>& knobs, size_t from, size_t to, int w)
    {
        for (size_t i = from; i < to && i < knobs.size(); ++i)
            knobs[i]->setBounds (area.removeFromLeft (w));
        return area;
    };
    constexpr int knobH = 104;

    builder.setBounds (10, 60, getWidth() - 20, getHeight() - 70);

    // row 1
    display.setBounds (10, 60, 610, 200);
    detectPanel.setBounds (628, 60, 482, 200);
    {
        auto c = detectPanel.content();
        layoutRow (c.removeFromTop (knobH), detectKnobs, 0, 6, c.getWidth() / 6);
        c.removeFromTop (8);
        auto b = c.removeFromTop (26);
        listenToggle.setBounds (b.removeFromLeft (90));
        b.removeFromLeft (8);
        alignToggle.setBounds (b.removeFromLeft (120));
        velocityLabel.setBounds (b);
    }

    // row 2
    samplePanel.setBounds (10, 268, 1100, 142);
    {
        auto c = samplePanel.content();
        auto rest = layoutRow (c.withHeight (knobH), sampleKnobs, 0, 7, 92);
        rest.removeFromLeft (24);
        auto rr = rest.removeFromLeft (140).withSizeKeepingCentre (140, 50);
        rrLabel.setBounds (rr.removeFromTop (20));
        rrBox.setBounds (rr.removeFromTop (26));
        rest.removeFromLeft (24);
        auto t = rest.removeFromLeft (110).withSizeKeepingCentre (110, 60);
        chokeToggle.setBounds (t.removeFromTop (26));
        t.removeFromTop (8);
        polarityToggle.setBounds (t.removeFromTop (26));
    }

    // row 3
    eqPanel.setBounds (10, 418, 380, 272);
    {
        auto c = eqPanel.content();
        const int w = c.getWidth() / 5;
        layoutRow (c.removeFromTop (knobH), eqKnobs, 0, 5, w);
        c.removeFromTop (12);
        auto r2 = layoutRow (c.removeFromTop (knobH), eqKnobs, 5, 9, w);
        eqOnToggle.setBounds (r2.removeFromLeft (w).withSizeKeepingCentre (w - 8, 26));
    }

    compPanel.setBounds (398, 418, 380, 272);
    {
        auto c = compPanel.content();
        grMeter.setBounds (c.removeFromRight (40));
        const int w = c.getWidth() / 4;
        layoutRow (c.removeFromTop (knobH), compKnobs, 0, 4, w);
        c.removeFromTop (20);
        auto b = c.removeFromTop (26);
        compOnToggle.setBounds (b.removeFromLeft (100));
        b.removeFromLeft (10);
        overEasyToggle.setBounds (b.removeFromLeft (100));
    }

    outPanel.setBounds (786, 418, 324, 272);
    {
        auto c = outPanel.content();
        auto meters = c.removeFromRight (84);
        inMeter.setBounds (meters.removeFromLeft (28));
        trigMeter.setBounds (meters.removeFromLeft (28));
        outMeter.setBounds (meters);
        const int w = c.getWidth() / 2;
        layoutRow (c.removeFromTop (knobH), outKnobs, 0, 2, w);
        c.removeFromTop (12);
        layoutRow (c.removeFromTop (knobH).withTrimmedLeft (w / 2), outKnobs, 2, 3, w);
    }
}
