// Runs audio through the plugin's own classes: checks the sound is untouched and the analysis
// matches the engine run directly, then saves what the editor draws from a WAV.
//   PolyPitchRenderTest                         the checks
//   PolyPitchRenderTest in.wav out.png [root scale seconds fit width height]    a picture
//   PolyPitchRenderTest in.wav --window [secs]  a real window, reporting paint times
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Scales.h"

namespace
{
    int failures = 0;

    void check(bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
        if (! ok)
            ++failures;
    }

    std::vector<float> tone(double rate, double seconds, double hz)
    {
        std::vector<float> x((size_t) (rate * seconds));
        for (size_t i = 0; i < x.size(); ++i)
        {
            float s = 0.0f;
            for (int h = 1; h <= 6; ++h)
                s += (float) std::sin(juce::MathConstants<double>::twoPi * hz * h * (double) i / rate) * 0.2f / (float) h;
            x[i] = s;
        }
        return x;
    }

    void feed(PolyPitchAudioProcessor& p, const std::vector<float>& mono, int block, int channels = 2, bool* unchanged = nullptr)
    {
        juce::AudioBuffer<float> buffer(channels, block);
        juce::MidiBuffer midi;
        for (size_t at = 0; at + (size_t) block <= mono.size(); at += (size_t) block)
        {
            for (int c = 0; c < channels; ++c)
                buffer.copyFrom(c, 0, mono.data() + at, block);
            p.processBlock(buffer, midi);
            if (unchanged != nullptr)
                for (int c = 0; c < channels; ++c)
                    if (std::memcmp(buffer.getReadPointer(c), mono.data() + at, sizeof(float) * (size_t) block) != 0)
                        *unchanged = false;
            if ((at / (size_t) block) % 32 == 0)
                p.waitForAnalysis(30000);
        }
        p.waitForAnalysis(30000);
    }

    std::vector<float> readWav(const juce::File& file, double& rate)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr)
            return {};
        rate = reader->sampleRate;
        juce::AudioBuffer<float> b((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read(&b, 0, b.getNumSamples(), 0, true, true);
        std::vector<float> mono((size_t) b.getNumSamples());
        for (int c = 0; c < b.getNumChannels(); ++c)
            juce::FloatVectorOperations::add(mono.data(), b.getReadPointer(c), b.getNumSamples());
        if (b.getNumChannels() > 1)
            juce::FloatVectorOperations::multiply(mono.data(), 1.0f / (float) b.getNumChannels(), b.getNumSamples());
        return mono;
    }

    void toneAtRate(double rate, int block)
    {
        PolyPitchAudioProcessor p;
        p.setPlayConfigDetails(2, 2, rate, block);
        p.prepareToPlay(rate, block);
        bool unchanged = true;
        feed(p, tone(rate, 3.0, 440.0), block, 2, &unchanged);
        check(unchanged, "sound untouched at " + juce::String(rate) + " Hz");

        const std::lock_guard<std::mutex> lock(p.traceLock());
        const auto& frames = p.traceFrames();
        std::vector<double> tops;
        for (size_t i = frames.size() / 2; i < frames.size(); ++i)
        {
            const polypitch::Ridge* top = nullptr;
            for (const auto& r : frames[i].ridges)
                if (top == nullptr || r.core > top->core)
                    top = &r;
            if (top != nullptr)
                tops.push_back(top->m);
        }
        std::sort(tops.begin(), tops.end());
        const double m = tops.empty() ? 0.0 : tops[tops.size() / 2];
        const double expectedSeconds = 3.0;
        const double last = frames.empty() ? 0.0 : frames.back().timeMs;
        check(std::abs(m - 69.0) < 0.03, "A4 found at " + juce::String(rate) + " Hz: " + juce::String(m, 3));
        check(std::abs(last / 1000.0 - expectedSeconds) < 0.1, "time runs true at " + juce::String(rate) + " Hz: " + juce::String(last / 1000.0, 3) + " s");
    }

    void matchesEngine(const juce::File& wav)
    {
        double rate = 0.0;
        const auto mono = readWav(wav, rate);
        check(! mono.empty(), "read " + wav.getFileName());
        if (mono.empty())
            return;

        // The engine and tracker run directly, one window per hop.
        polypitch::Config config;
        config.sampleRate = juce::roundToInt(rate);
        polypitch::Engine engine(config);
        polypitch::Tracker tracker;
        std::vector<std::vector<polypitch::Ridge>> direct;
        for (size_t s = 0; s + 8192 <= mono.size(); s += 1024)
        {
            engine.analyse(mono.data() + s, 8192);
            direct.push_back(tracker.track(engine.laneEvidence(), engine.lanePitch(), engine.laneCount(), ! direct.empty(), polypitch::ANY_NOTE));
        }

        // Through the plugin, in awkward block sizes.
        for (int block : { 64, 512, 1023, 4096 })
        {
            PolyPitchAudioProcessor p;
            p.setPlayConfigDetails(1, 1, rate, block);
            p.prepareToPlay(rate, block);
            feed(p, mono, block, 1);
            const std::lock_guard<std::mutex> lock(p.traceLock());
            const auto& frames = p.traceFrames();
            size_t compared = 0, different = 0;
            for (size_t i = 0; i < frames.size() && i < direct.size(); ++i)
            {
                ++compared;
                if (frames[i].ridges.size() != direct[i].size())
                {
                    ++different;
                    continue;
                }
                for (size_t r = 0; r < direct[i].size(); ++r)
                    if (frames[i].ridges[r].m != direct[i][r].m || frames[i].ridges[r].tint != direct[i][r].tint || frames[i].ridges[r].core != direct[i][r].core)
                    {
                        ++different;
                        break;
                    }
            }
            const size_t missing = direct.size() - juce::jmin(direct.size(), frames.size());
            // Up to one block's worth of the file's tail is never fed.
            check(different == 0 && missing <= (size_t) (block / 1024 + 1), "block " + juce::String(block) + ": " + juce::String((int) compared) + " frames, " + juce::String((int) different) + " differ, " + juce::String((int) missing) + " missing");
        }
    }

    void retint()
    {
        PolyPitchAudioProcessor p;
        p.setPlayConfigDetails(2, 2, 48000.0, 512);
        p.prepareToPlay(48000.0, 512);
        feed(p, tone(48000.0, 2.0, 440.0), 512);

        const auto greenAtEnd = [&]
        {
            const std::lock_guard<std::mutex> lock(p.traceLock());
            for (const auto& r : p.traceFrames().back().ridges)
                if (std::abs(r.m - 69.0) < 0.2)
                    return r.inTune;
            return false;
        };
        const auto before = p.traceGeneration();
        check(greenAtEnd(), "A4 is green with every note a target");
        p.setTargets(polypitch::scales::targetMask(1, polypitch::scales::all()[1]));
        check(! greenAtEnd(), "A4 is not green in D-flat major");
        check(p.traceGeneration() != before, "changing targets redraws");
        p.setTargets(polypitch::scales::targetMask(9, polypitch::scales::all()[1]));
        check(greenAtEnd(), "A4 is green again in A major");

        juce::MemoryBlock state;
        p.view().seconds = 23.0;
        p.view().scaleId = "dorian";
        p.view().rootPc = 2;
        p.getStateInformation(state);
        PolyPitchAudioProcessor q;
        q.setStateInformation(state.getData(), (int) state.getSize());
        check(q.view().seconds == 23.0 && q.view().scaleId == "dorian" && q.view().rootPc == 2, "settings survive saving");
    }

    int picture(const juce::File& wav, const juce::File& out, int root, const juce::String& scale, double seconds, bool fit, int width, int height)
    {
        double rate = 0.0;
        const auto mono = readWav(wav, rate);
        if (mono.empty())
            return 1;
        PolyPitchAudioProcessor p;
        p.setPlayConfigDetails(1, 1, rate, 1024);
        p.prepareToPlay(rate, 1024);
        p.view().seconds = seconds;
        feed(p, mono, 1024, 1);

        PolyPitchAudioProcessorEditor editor(p);
        editor.setKey(root, scale);
        editor.setSize(width, height);
        if (fit)
            editor.trace().fitPitch();
        for (int i = 0; i < 3; ++i)
            editor.trace().tick();

        const auto started = juce::Time::getMillisecondCounterHiRes();
        const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
        const auto first = juce::Time::getMillisecondCounterHiRes() - started;
        const auto again = juce::Time::getMillisecondCounterHiRes();
        editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
        std::cout << "first draw " << first << " ms, redraw " << juce::Time::getMillisecondCounterHiRes() - again << " ms" << std::endl;

        out.deleteFile();
        juce::FileOutputStream stream(out);
        juce::PNGImageFormat png;
        return png.writeImageToStream(image, stream) ? 0 : 1;
    }

    int window(const juce::File& wav, double seconds)
    {
        double rate = 0.0;
        auto mono = readWav(wav, rate);
        if (mono.empty())
            return 1;
        PolyPitchAudioProcessor p;
        const int block = 512;
        p.setPlayConfigDetails(1, 1, rate, block);
        p.prepareToPlay(rate, block);

        juce::DocumentWindow win("PolyPitch test", juce::Colours::black, juce::DocumentWindow::closeButton);
        auto* editor = new PolyPitchAudioProcessorEditor(p);
        win.setContentOwned(editor, true);
        win.setUsingNativeTitleBar(true);
        win.setVisible(true);

        juce::AudioBuffer<float> buffer(1, block);
        juce::MidiBuffer midi;
        size_t at = 0;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        double total = 0.0, worst = 0.0;
        int paints = 0;
        double lastSeen = -1.0;
        while (juce::Time::getMillisecondCounterHiRes() - start < seconds * 1000.0)
        {
            const auto due = (size_t) ((juce::Time::getMillisecondCounterHiRes() - start) / 1000.0 * rate);
            while (at + (size_t) block <= due && at + (size_t) block <= mono.size())
            {
                buffer.copyFrom(0, 0, mono.data() + at, block);
                p.processBlock(buffer, midi);
                at += (size_t) block;
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil(4);
            const double paintMs = editor->trace().lastPaintMs();
            if (paintMs != lastSeen)
            {
                lastSeen = paintMs;
                total += paintMs;
                worst = juce::jmax(worst, paintMs);
                ++paints;
            }
        }
        std::cout << "paints " << paints << " in " << seconds << " s, average " << (paints > 0 ? total / paints : 0.0) << " ms, worst " << worst << " ms" << std::endl;
        return paints > 0 ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc >= 3 && juce::String(argv[2]) == "--window")
        return window(juce::File(argv[1]), argc > 3 ? juce::String(argv[3]).getDoubleValue() : 5.0);

    if (argc >= 3)
        return picture(juce::File(argv[1]), juce::File(argv[2]),
                       argc > 3 ? juce::String(argv[3]).getIntValue() : 0,
                       argc > 4 ? juce::String(argv[4]) : juce::String("off"),
                       argc > 5 ? juce::String(argv[5]).getDoubleValue() : 20.0,
                       argc > 6 && juce::String(argv[6]) == "fit",
                       argc > 7 ? juce::String(argv[7]).getIntValue() : 1100,
                       argc > 8 ? juce::String(argv[8]).getIntValue() : 560);

    for (const auto& [rate, block] : std::vector<std::pair<double, int>> { { 44100.0, 512 }, { 48000.0, 256 }, { 88200.0, 1024 }, { 96000.0, 480 }, { 192000.0, 2048 } })
        toneAtRate(rate, block);
    retint();
    if (argc == 2)
        matchesEngine(juce::File(argv[1]));

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
