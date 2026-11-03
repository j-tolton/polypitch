// Loads the built VST3 the way a host does, plays through it and opens its window.
//   PolyPitchHostTest path\to\PolyPitch.vst3
#include <JuceHeader.h>

namespace
{
    int failures = 0;

    void check(bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
        if (! ok)
            ++failures;
    }
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 2)
        return 2;

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile(found, argv[1]);
    check(found.size() == 1, "one plugin found in the file");
    if (found.isEmpty())
        return 1;

    const auto& description = *found[0];
    check(description.name == "PolyPitch" && description.manufacturerName == "Jon Tolton", description.name + " by " + description.manufacturerName);
    check(description.category.contains("Analyzer"), "category " + description.category);

    const double rate = 48000.0;
    const int block = 512;
    juce::String error;
    auto plugin = format.createInstanceFromDescription(description, rate, block, error);
    check(plugin != nullptr, "instance created " + error);
    if (plugin == nullptr)
        return 1;

    plugin->setPlayConfigDetails(2, 2, rate, block);
    plugin->prepareToPlay(rate, block);
    check(plugin->getLatencySamples() == 0, "reports no latency");

    juce::AudioBuffer<float> buffer(2, block), expected(2, block);
    juce::MidiBuffer midi;
    juce::Random random(7);
    double phase = 0.0, worstUs = 0.0;
    bool unchanged = true;

    const auto play = [&](int blocks)
    {
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < block; ++i)
            {
                phase += juce::MathConstants<double>::twoPi * 440.0 / rate;
                expected.setSample(0, i, (float) std::sin(phase) * 0.3f);
                expected.setSample(1, i, (float) std::sin(phase * 2.0) * 0.2f + random.nextFloat() * 0.001f);
            }
            buffer.makeCopyOf(expected, true);
            const auto started = juce::Time::getMillisecondCounterHiRes();
            plugin->processBlock(buffer, midi);
            worstUs = juce::jmax(worstUs, (juce::Time::getMillisecondCounterHiRes() - started) * 1000.0);
            for (int c = 0; c < 2; ++c)
                if (std::memcmp(buffer.getReadPointer(c), expected.getReadPointer(c), sizeof(float) * (size_t) block) != 0)
                    unchanged = false;
        }
    };

    play(400);
    check(unchanged, "sound passes through bit for bit");

    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    check(state.getSize() > 0, "state saved, " + juce::String((int) state.getSize()) + " bytes");
    plugin->setStateInformation(state.getData(), (int) state.getSize());

    check(plugin->hasEditor(), "has an editor");
    std::unique_ptr<juce::AudioProcessorEditor> editor(plugin->createEditorIfNeeded());
    check(editor != nullptr, "editor created");
    if (editor != nullptr)
    {
        juce::DocumentWindow window("PolyPitch host test", juce::Colours::black, juce::DocumentWindow::closeButton);
        window.setUsingNativeTitleBar(true);
        window.setContentNonOwned(editor.get(), true);
        window.setVisible(true);
        check(editor->getWidth() >= 680 && editor->getHeight() >= 320, "window size " + juce::String(editor->getWidth()) + " x " + juce::String(editor->getHeight()));

        const auto start = juce::Time::getMillisecondCounter();
        juce::int64 played = 0;
        while (juce::Time::getMillisecondCounter() - start < 3000)
        {
            const auto due = (juce::int64) ((juce::Time::getMillisecondCounter() - start) * rate / 1000.0 / block);
            if (due > played)
            {
                play((int) (due - played));
                played = due;
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        }
        window.clearContentComponent();
    }
    editor.reset();

    check(unchanged, "still untouched with the window open");
    std::cout << "  worst audio block " << worstUs << " us" << std::endl;
    plugin->releaseResources();
    plugin.reset();
    std::cout << (failures == 0 ? "ALL PASSED" : "FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
