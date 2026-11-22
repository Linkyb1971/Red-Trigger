#pragma once
#include "Widgets.h"
#include "PluginProcessor.h"
#include "Params.h"

namespace dt::ui
{
/** Scrolling view of the detection signal with threshold / ceiling lines and hit markers.
    Drag vertically to set the threshold. */
class DetectorDisplay : public juce::Component
{
public:
    explicit DetectorDisplay (DrumTriggerProcessor& p) : processor (p)
    {
        history.resize (historySize);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }


    void tick()
    {
        DisplayFifo::Point buf[512];
        int got;
        while ((got = processor.display.pull (buf, 512)) > 0)
            for (int i = 0; i < got; ++i)
            {
                history[(size_t) writePos] = buf[i];
                writePos = (writePos + 1) % historySize;
            }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0e1013));
        g.fillRoundedRectangle (r, 6.0f);

        // dB grid
        g.setFont (juce::FontOptions (10.0f));
        for (float db : { -6.0f, -12.0f, -24.0f, -36.0f, -48.0f })
        {
            const float y = dbToY (db);
            g.setColour (colours::panelEdge);
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            g.setColour (colours::dimText.withAlpha (0.6f));
            g.drawText (juce::String ((int) db), (int) r.getRight() - 30, (int) y - 12, 26, 12, juce::Justification::right);
        }

        const int w = getWidth();
        const float colW = (float) w / (float) historySize;
        juce::Path wave, thrPath;
        wave.startNewSubPath (0.0f, r.getBottom());
        bool thrStarted = false;

        for (int i = 0; i < historySize; ++i)
        {
            const auto& pt = history[(size_t) ((writePos + i) % historySize)];
            const float x = (float) i * colW;
            wave.lineTo (x, dbToY (gainToDb (pt.level)));

            const float ty = dbToY (gainToDb (pt.threshold));
            if (! thrStarted) { thrPath.startNewSubPath (x, ty); thrStarted = true; }
            else thrPath.lineTo (x, ty);
        }
        wave.lineTo ((float) w, r.getBottom());
        wave.closeSubPath();

        g.setGradientFill (juce::ColourGradient (colours::accent2.withAlpha (0.8f), 0, 0,
                                                 colours::accent2.withAlpha (0.15f), 0, r.getBottom(), false));
        g.fillPath (wave);

        // hit markers
        g.setColour (colours::accent);
        for (int i = 0; i < historySize; ++i)
            if (history[(size_t) ((writePos + i) % historySize)].hit)
                g.fillRect ((float) i * colW - 1.0f, 0.0f, 2.0f, r.getHeight());

        // effective (adaptive) threshold
        g.setColour (juce::Colours::yellow.withAlpha (0.45f));
        g.strokePath (thrPath, juce::PathStrokeType (1.0f));

        // threshold + ceiling lines
        const float thr = processor.apvts.getRawParameterValue (pid::threshold)->load();
        const float ceil = processor.apvts.getRawParameterValue (pid::ceiling)->load();
        g.setColour (juce::Colours::yellow);
        g.drawHorizontalLine ((int) dbToY (thr), 0.0f, (float) w);
        g.setColour (colours::meterRed.withAlpha (0.8f));
        const float dashes[] = { 4.0f, 4.0f };
        g.drawDashedLine (juce::Line<float> (0.0f, dbToY (ceil), (float) w, dbToY (ceil)), dashes, 2, 1.0f);

        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.setColour (juce::Colours::yellow);
        g.drawText ("THRESHOLD " + juce::String (thr, 1) + " dB", 8, (int) dbToY (thr) - 14, 200, 12, juce::Justification::left);
        g.setColour (colours::meterRed);
        g.drawText ("CEILING (full velocity)", 8, (int) dbToY (ceil) + 2, 200, 12, juce::Justification::left);

        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (auto* p = processor.apvts.getParameter (pid::threshold))
        {
            p->beginChangeGesture();
            setThresholdFromY (*p, (float) e.y);
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (auto* p = processor.apvts.getParameter (pid::threshold))
            setThresholdFromY (*p, (float) e.y);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (auto* p = processor.apvts.getParameter (pid::threshold))
            p->endChangeGesture();
    }

private:
    static constexpr float minDb = -60.0f, maxDb = 0.0f;
    static constexpr int historySize = 640;

    float dbToY (float db) const
    {
        const float t = (std::clamp (db, minDb, maxDb) - minDb) / (maxDb - minDb);
        return 4.0f + (1.0f - t) * ((float) getHeight() - 8.0f);
    }

    void setThresholdFromY (juce::RangedAudioParameter& p, float y)
    {
        const float t = 1.0f - (y - 4.0f) / ((float) getHeight() - 8.0f);
        const float db = minDb + std::clamp (t, 0.0f, 1.0f) * (maxDb - minDb);
        p.setValueNotifyingHost (p.convertTo0to1 (db));
    }

    DrumTriggerProcessor& processor;
    std::vector<DisplayFifo::Point> history;
    int writePos = 0;
};
} // namespace dt::ui
