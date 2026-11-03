#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "TraceView.h"

class PolyPitchAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PolyPitchAudioProcessorEditor(PolyPitchAudioProcessor&);
    ~PolyPitchAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // The project's saved settings have changed.
    void settingsChanged();

    void setKey(int rootPc, const juce::String& scaleId);
    TraceView& trace() noexcept { return traceView; }

private:
    class Look final : public juce::LookAndFeel_V4
    {
    public:
        Look();
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
        void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
        juce::Font getTextButtonFont(juce::TextButton&, int) override;
        juce::Font getComboBoxFont(juce::ComboBox&) override;
        juce::Font getPopupMenuFont() override;
    };

    void showReadout(double midi, bool inTune);
    void refreshKeyControls();
    void refreshFollow();

    PolyPitchAudioProcessor& processor;
    Look look;
    TraceView traceView;

    juce::ComboBox rootBox, scaleBox;
    juce::String readoutNote { juce::String::fromUTF8("\xe2\x80\x94") }, readoutCents;
    bool readoutInTune = false;
    juce::TextButton followButton { "Follow" }, fitButton { "Fit" };
    juce::TextButton pitchIn { "+" }, pitchOut { juce::String::fromUTF8("\xe2\x88\x92") };
    juce::TextButton timeIn { "+" }, timeOut { juce::String::fromUTF8("\xe2\x88\x92") };
    juce::Rectangle<int> pitchLabelArea, timeLabelArea, readoutArea;

    juce::VBlankAttachment displayLink;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PolyPitchAudioProcessorEditor)
};
