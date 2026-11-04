#include "PluginEditor.h"
#include "Scales.h"
#include "Style.h"

namespace
{
    constexpr int headerHeight = 46;
    const juce::Colour panel { 0xff0e151a }, raised { 0xff162027 }, raisedHover { 0xff1e2b33 };
    const juce::Colour line { 0xff24323a }, text { 0xffe9f0f3 }, muted { 0xff8fa0a9 }, accent { 0xff4cd97b };
    const juce::Colour accentSoft { 0xff143524 };
}

PolyPitchAudioProcessorEditor::Look::Look()
{
    setColour(juce::ComboBox::backgroundColourId, raised);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::outlineColourId, line);
    setColour(juce::ComboBox::arrowColourId, muted);
    setColour(juce::PopupMenu::backgroundColourId, panel);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::headerTextColourId, muted);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, raisedHover);
    setColour(juce::PopupMenu::highlightedTextColourId, text);
    setColour(juce::TextButton::buttonColourId, raised);
    setColour(juce::TextButton::buttonOnColourId, accentSoft);
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::TextButton::textColourOnId, accent);
    setColour(juce::Label::textColourId, text);
}

void PolyPitchAudioProcessorEditor::Look::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& colour, bool highlighted, bool down)
{
    const auto area = button.getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(highlighted || down ? (button.getToggleState() ? colour.brighter(0.15f) : raisedHover) : colour);
    g.fillRoundedRectangle(area, 6.0f);
    g.setColour(button.getToggleState() ? accent : line);
    g.drawRoundedRectangle(area, 6.0f, 1.0f);
}

void PolyPitchAudioProcessorEditor::Look::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto area = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height).reduced(0.5f);
    g.setColour(box.isEnabled() ? raised : panel);
    g.fillRoundedRectangle(area, 6.0f);
    g.setColour(line);
    g.drawRoundedRectangle(area, 6.0f, 1.0f);

    juce::Path arrow;
    const float cx = (float) width - 13.0f, cy = (float) height * 0.5f;
    arrow.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour(muted);
    g.fillPath(arrow);
}

juce::Font PolyPitchAudioProcessorEditor::Look::getTextButtonFont(juce::TextButton&, int)
{
    return polypitch::style::font(13.0f);
}

juce::Font PolyPitchAudioProcessorEditor::Look::getComboBoxFont(juce::ComboBox&)
{
    return polypitch::style::font(13.0f);
}

juce::Font PolyPitchAudioProcessorEditor::Look::getPopupMenuFont()
{
    return polypitch::style::font(14.0f);
}

PolyPitchAudioProcessorEditor::PolyPitchAudioProcessorEditor(PolyPitchAudioProcessor& p)
    : AudioProcessorEditor(p),
      processor(p),
      traceView(p),
      displayLink(this, [this] { traceView.tick(); })
{
    setLookAndFeel(&look);
    setOpaque(true);

    addAndMakeVisible(traceView);
    traceView.onReadout = [this](double midi, bool inTune) { showReadout(midi, inTune); };
    traceView.onFollowChanged = [this] { refreshFollow(); };
    traceView.onTransport = [this](bool available, bool playing)
    {
        if (playButton.isVisible() != available)
        {
            playButton.setVisible(available);
            resized();
        }
        const auto label = playing ? "Stop" : "Play";
        if (playButton.getButtonText() != label)
            playButton.setButtonText(label);
        const bool canClean = processor.hasCleanTrace();
        if (cleanButton.isVisible() != canClean)
        {
            cleanButton.setVisible(canClean);
            resized();
        }
        cleanButton.setToggleState(processor.view().clean, juce::dontSendNotification);
    };
    cleanButton.setTooltip("Show the cleaned-up trace, or everything as first heard");
    cleanButton.onClick = [this] { processor.setCleanTrace(! processor.view().clean); };
    addChildComponent(cleanButton);
    traceView.onUseKey = [this](int root, bool minor) { setKey(root, minor ? "minor" : "major"); };
    playButton.onClick = [this]
    {
        if (auto* host = processor.trackHost())
            host->togglePlay();
    };
    addChildComponent(playButton);
    processor.analyseTrackOnce();

    const auto& scales = polypitch::scales::all();
    scaleBox.addSectionHeading("Scales");
    for (size_t i = 0; i < scales.size(); ++i)
    {
        if (i == 10)
            scaleBox.addSectionHeading("Arpeggios");
        scaleBox.addItem(juce::String(scales[i].label) + (scales[i].isArpeggio ? " arpeggio" : ""), (int) i + 1);
    }
    scaleBox.setTooltip("The notes that count as targets");
    scaleBox.onChange = [this]
    {
        const auto& chosen = polypitch::scales::all()[(size_t) juce::jmax(0, scaleBox.getSelectedId() - 1)];
        setKey(processor.view().rootPc, chosen.id);
    };
    rootBox.onChange = [this]
    {
        // Choosing a key with no scale means its major scale.
        const auto current = processor.view().scaleId;
        setKey(rootBox.getSelectedId() - 1, current == "off" ? "major" : current);
    };
    addAndMakeVisible(rootBox);
    addAndMakeVisible(scaleBox);

    followButton.setClickingTogglesState(false);
    followButton.onClick = [this] { traceView.setFollow(! traceView.isFollowing()); };
    fitButton.onClick = [this] { traceView.fitPitch(); };
    pitchIn.onClick = [this] { traceView.zoomPitch(0.8); };
    pitchOut.onClick = [this] { traceView.zoomPitch(1.25); };
    timeIn.onClick = [this] { traceView.zoomTime(0.8); };
    timeOut.onClick = [this] { traceView.zoomTime(1.25); };
    for (auto* button : { &followButton, &fitButton, &pitchIn, &pitchOut, &timeIn, &timeOut })
        addAndMakeVisible(*button);

    refreshKeyControls();
    refreshFollow();

    // Setting the limits resizes us, which would overwrite the saved size.
    const int savedWidth = processor.view().width, savedHeight = processor.view().height;
    setResizable(true, true);
    setResizeLimits(960, 320, 3000, 2000);
    setSize(savedWidth, savedHeight);
}

PolyPitchAudioProcessorEditor::~PolyPitchAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void PolyPitchAudioProcessorEditor::setKey(int rootPc, const juce::String& scaleId)
{
    auto& settings = processor.view();
    settings.rootPc = rootPc;
    settings.scaleId = scaleId;
    const auto& scale = polypitch::scales::all()[(size_t) polypitch::scales::findIndex(scaleId.toRawUTF8())];
    processor.setTargets(polypitch::scales::targetMask(rootPc, scale));
    refreshKeyControls();
    traceView.settingsChanged();
}

void PolyPitchAudioProcessorEditor::settingsChanged()
{
    refreshKeyControls();
    traceView.settingsChanged();
    refreshFollow();
    setSize(processor.view().width, processor.view().height);
}

void PolyPitchAudioProcessorEditor::refreshKeyControls()
{
    const auto& settings = processor.view();
    const int index = polypitch::scales::findIndex(settings.scaleId.toRawUTF8());
    const auto& scale = polypitch::scales::all()[(size_t) index];
    const bool flats = polypitch::scales::usesFlats(settings.rootPc, scale);
    const bool hasScale = scale.steps.size() != 0;

    // With no scale chosen there is no key to spell for, so the roots use the common names.
    static constexpr bool commonFlats[12] { false, true, false, true, false, false, false, false, true, false, true, false };
    rootBox.clear(juce::dontSendNotification);
    for (int pc = 0; pc < 12; ++pc)
        rootBox.addItem(juce::String::fromUTF8(polypitch::scales::noteName(pc, hasScale ? flats : commonFlats[pc])), pc + 1);
    rootBox.setSelectedId(settings.rootPc + 1, juce::dontSendNotification);
    scaleBox.setSelectedId(index + 1, juce::dontSendNotification);

    // Still usable, since choosing a key starts its major scale, but no key applies yet.
    rootBox.setAlpha(hasScale ? 1.0f : 0.45f);
}

void PolyPitchAudioProcessorEditor::refreshFollow()
{
    followButton.setToggleState(traceView.isFollowing(), juce::dontSendNotification);
}

void PolyPitchAudioProcessorEditor::showReadout(double midi, bool inTune)
{
    juce::String note = juce::String::fromUTF8("\xe2\x80\x94"), centsText;
    if (midi >= 0.0)
    {
        const auto& settings = processor.view();
        const auto& scale = polypitch::scales::all()[(size_t) polypitch::scales::findIndex(settings.scaleId.toRawUTF8())];
        const int nearest = (int) std::floor(midi + 0.5), cents = juce::roundToInt((midi - nearest) * 100.0);
        note = juce::String::fromUTF8(polypitch::scales::noteName(nearest, polypitch::scales::usesFlats(settings.rootPc, scale)))
               + juce::String((int) std::floor(nearest / 12.0) - 1);
        centsText = (cents >= 0 ? juce::String("+") : juce::String::fromUTF8("\xe2\x88\x92")) + juce::String(std::abs(cents)) + juce::String::fromUTF8("\xc2\xa2");
    }
    if (note == readoutNote && centsText == readoutCents && inTune == readoutInTune)
        return;
    readoutNote = note;
    readoutCents = centsText;
    readoutInTune = inTune;
    repaint(readoutArea);
}

void PolyPitchAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(panel);
    g.setColour(line);
    g.fillRect(0, headerHeight - 1, getWidth(), 1);

    juce::AttributedString brand;
    brand.append("Poly", polypitch::style::font(16.0f, true), text);
    brand.append("Pitch", polypitch::style::font(16.0f, true), accent);
    brand.setJustification(juce::Justification::centredLeft);
    brand.draw(g, juce::Rectangle<float>(12.0f, 0.0f, 84.0f, (float) headerHeight - 1.0f));

    // The note is what you read at a glance. The cents sit beside it, quieter.
    juce::AttributedString reading;
    reading.append(readoutNote, polypitch::style::font(22.0f, true), readoutInTune ? accent : text);
    if (readoutCents.isNotEmpty())
        reading.append("  " + readoutCents, polypitch::style::font(13.0f), muted);
    reading.setJustification(juce::Justification::centred);
    reading.setWordWrap(juce::AttributedString::none);
    reading.draw(g, readoutArea.toFloat());

    g.setColour(muted);
    g.setFont(polypitch::style::font(12.0f));
    g.drawText("Pitch", pitchLabelArea, juce::Justification::centredRight, false);
    g.drawText("Time", timeLabelArea, juce::Justification::centredRight, false);
}

void PolyPitchAudioProcessorEditor::resized()
{
    auto& settings = processor.view();
    settings.width = getWidth();
    settings.height = getHeight();

    auto area = getLocalBounds();
    traceView.setBounds(area.removeFromBottom(getHeight() - headerHeight));

    auto bar = area.withTrimmedBottom(1).reduced(8, 7);
    bar.removeFromLeft(90);
    rootBox.setBounds(bar.removeFromLeft(66));
    bar.removeFromLeft(6);
    scaleBox.setBounds(bar.removeFromLeft(182));
    if (playButton.isVisible())
    {
        bar.removeFromLeft(6);
        playButton.setBounds(bar.removeFromLeft(56));
    }
    if (cleanButton.isVisible())
    {
        bar.removeFromLeft(6);
        cleanButton.setBounds(bar.removeFromLeft(56));
    }

    timeIn.setBounds(bar.removeFromRight(28));
    bar.removeFromRight(4);
    timeOut.setBounds(bar.removeFromRight(28));
    bar.removeFromRight(8);
    timeLabelArea = bar.removeFromRight(30);
    bar.removeFromRight(14);
    pitchIn.setBounds(bar.removeFromRight(28));
    bar.removeFromRight(4);
    pitchOut.setBounds(bar.removeFromRight(28));
    bar.removeFromRight(8);
    pitchLabelArea = bar.removeFromRight(32);
    bar.removeFromRight(14);
    fitButton.setBounds(bar.removeFromRight(40));
    bar.removeFromRight(6);
    followButton.setBounds(bar.removeFromRight(62));
    bar.removeFromRight(6);
    readoutArea = bar;
}
