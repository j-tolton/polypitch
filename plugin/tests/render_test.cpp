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

    // The live engine, as the web app uses it. A note, plus a little sound an octave below it that
    // isn't a note of its own, then the same with a real note there.
    void falseOctave()
    {
        const auto strengthAt = [](const std::vector<float>& audio, double midi)
        {
            polypitch::Config config;
            polypitch::Engine engine(config);
            polypitch::Tracker tracker;
            std::vector<double> cores;
            for (size_t s = 0, k = 0; s + 8192 <= audio.size(); s += 1024, ++k)
            {
                engine.analyse(audio.data() + s, 8192);
                const auto& ridges = tracker.track(engine.laneEvidence(), engine.lanePitch(), engine.laneCount(), k > 0, polypitch::ANY_NOTE);
                if (k < 40)
                    continue;
                double core = 0.0;
                for (const auto& r : ridges)
                    if (std::abs(r.m - midi) < 0.4)
                        core = juce::jmax(core, r.core);
                cores.push_back(core);
            }
            std::sort(cores.begin(), cores.end());
            return cores[cores.size() / 2];
        };
        const auto a4 = tone(48000.0, 3.0, 440.0), d4 = tone(48000.0, 3.0, 293.66), a3 = tone(48000.0, 3.0, 220.0);
        std::vector<float> doubleStop(a4.size()), withFaint(a4.size()), withReal(a4.size());
        for (size_t i = 0; i < a4.size(); ++i)
        {
            doubleStop[i] = a4[i] + d4[i];
            const auto low = (float) std::sin(juce::MathConstants<double>::twoPi * 220.0 * (double) i / 48000.0);
            withFaint[i] = doubleStop[i] + low * 0.04f;
            withReal[i] = doubleStop[i] + a3[i];
        }
        const double faint = strengthAt(withFaint, 57.0), real = strengthAt(withReal, 57.0), upper = strengthAt(withFaint, 69.0), lower = strengthAt(withFaint, 62.0);
        check(faint < 0.1, "a D4 + A4 double stop shows no false A3: " + juce::String(faint, 2));
        check(upper > 0.5 && lower > 0.5, "while D4 and A4 stay strong: " + juce::String(lower, 2) + ", " + juce::String(upper, 2));
        check(real > 0.5, "and a really played A3 still shows: " + juce::String(real, 2));
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

    // Stands in for Reaper: a track of audio at some project position, and a transport.
    class FakeHost final : public TrackHost
    {
    public:
        std::vector<float> audio;
        double start = 0.0, position = 0.0, soughtTo = -1.0;
        bool playing = false, changedFlag = false;
        int toggles = 0;

        bool open() override { changedFlag = false; return true; }
        double startSeconds() override { return start; }
        double endSeconds() override { return start + (double) audio.size() / 48000.0; }
        void read(double from, int rate, int count, double* stereo) override
        {
            jassert(rate == 48000);
            const auto first = (juce::int64) std::llround((from - start) * rate);
            for (int i = 0; i < count; ++i)
            {
                const auto at = first + i;
                const double v = at >= 0 && at < (juce::int64) audio.size() ? audio[(size_t) at] : 0.0;
                stereo[i * 2] = stereo[i * 2 + 1] = v;
            }
        }
        bool audioChanged() override { return changedFlag; }
        juce::String trackName() override { return "Test track"; }
        bool isPlaying() override { return playing; }
        double positionSeconds() override { return position; }
        void seek(double seconds) override { soughtTo = seconds; position = seconds; }
        void togglePlay() override { playing = ! playing; ++toggles; }
        double bpm = 120.0;
        int beatsPerBar = 4;
        void gridLines(double from, double to, bool beats, std::vector<GridLine>& lines) override
        {
            const double beat = 60.0 / bpm;
            for (int b = juce::jmax(0, (int) std::floor(from / beat)); b * beat <= to; ++b)
                if (b * beat >= from && (beats || b % beatsPerBar == 0))
                    lines.push_back({ b * beat, b / beatsPerBar + 1, b % beatsPerBar == 0 });
        }
    };

    bool pumpUntilAnalysed(PolyPitchAudioProcessor& p, double& tookMs)
    {
        const auto started = juce::Time::getMillisecondCounterHiRes();
        while (p.isAnalysing())
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
            if (juce::Time::getMillisecondCounterHiRes() - started > 120000.0)
                return false;
        }
        tookMs = juce::Time::getMillisecondCounterHiRes() - started;
        return true;
    }

    void timelineChecks(const juce::File& wav)
    {
        // Five seconds of silence, two of A4, one of silence, at ten seconds into the project.
        FakeHost host;
        host.start = 10.0;
        host.audio.assign(48000 * 8, 0.0f);
        const auto a4 = tone(48000.0, 2.0, 440.0);
        std::copy(a4.begin(), a4.end(), host.audio.begin() + 48000 * 5);

        PolyPitchAudioProcessor p;
        p.setPlayConfigDetails(2, 2, 44100.0, 512);
        p.prepareToPlay(44100.0, 512);
        p.setTrackHost(&host);
        p.analyseTrackOnce();
        check(p.isTimeline(), "a track with audio goes on the timeline");
        double took = 0.0;
        check(pumpUntilAnalysed(p, took), "analysis finishes");
        p.setCleanTrace(false);

        {
            const std::lock_guard<std::mutex> lock(p.traceLock());
            const auto& frames = p.traceFrames();
            check(! frames.empty() && std::abs(frames.front().timeMs - 10000.0) < 0.01, "first frame at the track's start: " + juce::String(frames.empty() ? 0.0 : frames.front().timeMs));
            check(! frames.empty() && std::abs(frames.back().timeMs - 18000.0) < 25.0, "last frame at the track's end: " + juce::String(frames.empty() ? 0.0 : frames.back().timeMs));

            double onset = -1.0, ending = -1.0, sum = 0.0;
            int count = 0;
            for (const auto& frame : frames)
                for (const auto& r : frame.ridges)
                    if (std::abs(r.m - 69.0) < 0.3 && r.core >= polypitch::RIDGE_STRONG)
                    {
                        if (onset < 0.0)
                            onset = frame.timeMs;
                        ending = frame.timeMs;
                        sum += r.m;
                        ++count;
                    }
            check(std::abs(onset - 15000.0) < 110.0, "the note starts where it does in the track: " + juce::String(onset, 1) + " ms");
            check(std::abs(ending - 17000.0) < 100.0, "the note ends where it does in the track: " + juce::String(ending, 1) + " ms");
            check(count > 0 && std::abs(sum / count - 69.0) < 0.03, "A4 measured: " + juce::String(count > 0 ? sum / count : 0.0, 3));

            // The same frames as the engine run directly over the padded track.
            std::vector<float> padded(4096, 0.0f);
            padded.insert(padded.end(), host.audio.begin(), host.audio.end());
            padded.resize(padded.size() + 4096, 0.0f);
            polypitch::DualEngine engine(48000);
            polypitch::Tracker tracker;
            size_t index = 0, different = 0;
            for (size_t s = 0; s + 8192 <= padded.size() && index < frames.size(); s += 1024, ++index)
            {
                engine.analyse(padded.data() + s);
                const auto& direct = tracker.track(engine.laneEvidence(), engine.lanePitch(), engine.laneCount(), index > 0, polypitch::ANY_NOTE);
                if (direct.size() != frames[index].ridges.size())
                    ++different;
                else
                    for (size_t r = 0; r < direct.size(); ++r)
                        if (direct[r].m != frames[index].ridges[r].m || direct[r].tint != frames[index].ridges[r].tint)
                        {
                            ++different;
                            break;
                        }
            }
            check(different == 0 && index == frames.size(), "matches the engine run directly: " + juce::String((int) index) + " frames, " + juce::String((int) different) + " differ");
        }

        // Playing no longer feeds the trace, and the sound is still untouched.
        {
            size_t before = 0;
            {
                const std::lock_guard<std::mutex> lock(p.traceLock());
                before = p.traceFrames().size();
            }
            bool unchanged = true;
            feed(p, tone(44100.0, 1.0, 330.0), 512, 2, &unchanged);
            juce::Thread::sleep(100);
            const std::lock_guard<std::mutex> lock(p.traceLock());
            check(unchanged && p.traceFrames().size() == before, "playback leaves the trace alone and the sound untouched");
        }

        // The view and the transport.
        {
            PolyPitchAudioProcessorEditor editor(p);
            editor.setSize(1100, 560);
            host.position = 12.0;
            editor.trace().tick();
            check(std::abs(editor.trace().playheadSeconds() - 12.0) < 0.001, "playhead read from the host");

            // The view runs from a third of its length before the playhead, in 10 s by default.
            const int usable = 1100 - 62 - 4;
            editor.trace().seekToX(62.0f + (float) usable / 2.0f);
            const double expected = 12.0 + 10.0 * (2.0 / 3.0) - 10.0 * 0.5;
            check(std::abs(host.soughtTo - expected) < 0.01, "a click seeks to the time under it: " + juce::String(host.soughtTo, 3) + " s, expected " + juce::String(expected, 3));

            editor.trace().tick();
            const auto stillAt = editor.trace().playheadSeconds();
            check(std::abs(stillAt - expected) < 0.001, "the playhead moved there");

            editor.trace().lookBack(3000.0);
            host.playing = true;
            host.position = 11.0;
            editor.trace().tick();
            check(std::abs(editor.trace().playheadSeconds() - 11.0) < 0.15, "pressing play returns to the playhead");
            host.playing = false;
        }

        host.changedFlag = true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(1500);
        check(p.trackChanged(), "a changed track is noticed");
        check(p.startTrackAnalysis() && pumpUntilAnalysed(p, took) && ! p.trackChanged(), "and can be analysed again");

        // An empty track listens instead.
        {
            FakeHost empty;
            PolyPitchAudioProcessor q;
            q.setPlayConfigDetails(2, 2, 48000.0, 512);
            q.prepareToPlay(48000.0, 512);
            q.setTrackHost(&empty);
            q.analyseTrackOnce();
            check(! q.isTimeline(), "an empty track stays on live listening");
            feed(q, tone(48000.0, 1.0, 440.0), 512);
            {
                const std::lock_guard<std::mutex> lock(q.traceLock());
                check(! q.traceFrames().empty(), "and still hears audio");
            }
            empty.audio.assign(48000, 0.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1500);
            check(q.trackChanged(), "and offers analysis once the track has audio");
        }

        // The cleanup, on something with known contents: a loud C4 and a quieter E4 together for
        // three seconds, then a burst of noise on its own.
        {
            FakeHost mix;
            mix.audio.assign(48000 * 6, 0.0f);
            const auto c4 = tone(48000.0, 3.0, 261.63), e4 = tone(48000.0, 3.0, 329.63);
            for (size_t i = 0; i < c4.size(); ++i)
                mix.audio[i] = c4[i] + e4[i] * 0.3f;
            juce::Random noise(11);
            for (int i = 48000 * 4; i < 48000 * 4 + 2400; ++i)
                mix.audio[(size_t) i] = (noise.nextFloat() - 0.5f) * 0.4f;

            PolyPitchAudioProcessor m;
            m.setTrackHost(&mix);
            m.analyseTrackOnce();
            pumpUntilAnalysed(m, took);

            const auto measure = [&m](double midi, double fromMs, double toMs, double& medianCore)
            {
                const std::lock_guard<std::mutex> lock(m.traceLock());
                std::vector<double> cores;
                for (const auto& frame : m.traceFrames())
                    if (frame.timeMs >= fromMs && frame.timeMs <= toMs)
                        for (const auto& r : frame.ridges)
                            if (std::abs(r.m - midi) < 0.3)
                                cores.push_back(r.core);
                std::sort(cores.begin(), cores.end());
                medianCore = cores.empty() ? 0.0 : cores[cores.size() / 2];
                return (int) cores.size();
            };
            const auto ridgesIn = [&m](double fromMs, double toMs)
            {
                const std::lock_guard<std::mutex> lock(m.traceLock());
                int count = 0;
                for (const auto& frame : m.traceFrames())
                    if (frame.timeMs >= fromMs && frame.timeMs <= toMs)
                        count += (int) frame.ridges.size();
                return count;
            };

            double cleanE = 0.0, rawE = 0.0, cleanC = 0.0, rawC = 0.0;
            const int cleanEFrames = measure(64.0, 300.0, 2700.0, cleanE), cleanCFrames = measure(60.0, 300.0, 2700.0, cleanC);
            const int cleanNoise = ridgesIn(3900.0, 4300.0);
            m.setCleanTrace(false);
            const int rawEFrames = measure(64.0, 300.0, 2700.0, rawE), rawCFrames = measure(60.0, 300.0, 2700.0, rawC);
            const int rawNoise = ridgesIn(3900.0, 4300.0);
            m.setCleanTrace(true);

            check(cleanCFrames >= rawCFrames * 95 / 100 && cleanEFrames >= rawEFrames * 95 / 100, "cleaning keeps both notes: C4 " + juce::String(cleanCFrames) + " of " + juce::String(rawCFrames) + ", E4 " + juce::String(cleanEFrames) + " of " + juce::String(rawEFrames));
            check(cleanE >= rawE * 0.99, "the quieter voice isn't dimmed: " + juce::String(rawE, 3) + " to " + juce::String(cleanE, 3));
            check(cleanC >= rawC * 0.99, "the louder voice isn't dimmed: " + juce::String(rawC, 3) + " to " + juce::String(cleanC, 3));
            check(cleanNoise * 5 <= rawNoise, "a burst of noise goes: " + juce::String(rawNoise) + " ridges to " + juce::String(cleanNoise));

            const std::lock_guard<std::mutex> lock(m.traceLock());
            bool linksValid = true;
            const auto& frames = m.traceFrames();
            for (size_t i = 0; i < frames.size(); ++i)
                for (const auto& r : frames[i].ridges)
                    if (r.from >= (i > 0 ? (int) frames[i - 1].ridges.size() : 0))
                        linksValid = false;
            check(linksValid, "links still point at real ridges");
        }

        // The lift itself, on frames built by hand: a steady loud note and a faint one above it.
        {
            std::vector<std::vector<polypitch::Ridge>> frames(47);
            for (size_t k = 0; k < frames.size(); ++k)
                for (int voice = 0; voice < 2; ++voice)
                {
                    polypitch::Ridge r;
                    r.m = r.centre = voice == 0 ? 60.0 : 64.0;
                    r.w = voice == 0 ? 1.0 : 0.46;
                    r.core = voice == 0 ? 1.0 : 0.1;
                    r.age = (int) k;
                    r.from = k > 0 ? voice : -1;
                    frames[k].push_back(r);
                }
            polypitch::refineTrace(frames, 46.875, polypitch::ANY_NOTE);
            const auto& middle = frames[frames.size() / 2];
            check(middle.size() == 2 && middle[0].core == 1.0 && middle[1].core > 0.3 && middle[1].core < 1.0, "a faint lasting note is lifted, and the loud one left alone: " + juce::String(middle.size() == 2 ? middle[1].core : 0.0, 3));
        }

        // A faint voice in pieces: returning to one pitch it's recovered, scattered it's not.
        for (const bool returning : { true, false })
        {
            static constexpr double scattered[6] { 63.0, 66.5, 70.0, 64.5, 68.5, 62.5 };
            std::vector<std::vector<polypitch::Ridge>> frames(60);
            for (int k = 0; k < (int) frames.size(); ++k)
            {
                polypitch::Ridge loud;
                loud.m = loud.centre = 60.0;
                loud.w = loud.core = 1.0;
                loud.age = k;
                loud.from = k > 0 ? 0 : -1;
                frames[(size_t) k].push_back(loud);
                if (k % 10 < 5)
                {
                    polypitch::Ridge faint;
                    faint.m = faint.centre = returning ? 64.0 : scattered[(k / 10) % 6];
                    faint.w = 0.3;
                    faint.core = 0.03;
                    faint.peak = 0.5;
                    faint.age = k % 10;
                    faint.from = k % 10 > 0 ? 1 : -1;
                    frames[(size_t) k].push_back(faint);
                }
            }
            polypitch::refineTrace(frames, 46.875, polypitch::ANY_NOTE);
            int faintLeft = 0;
            double brightest = 0.0;
            for (const auto& frame : frames)
                for (const auto& r : frame)
                    if (r.m > 61.0)
                    {
                        ++faintLeft;
                        brightest = juce::jmax(brightest, r.core);
                    }
            if (returning)
                check(faintLeft == 30 && brightest >= 0.25, "a faint voice returning to one pitch is kept and shown: " + juce::String(faintLeft) + " frames at " + juce::String(brightest, 2));
            else
                check(faintLeft == 0, "scattered pieces are not: " + juce::String(faintLeft) + " frames left");
        }

        // Near-silence goes, however confident it looks, while a merely quiet passage stays.
        {
            std::vector<std::vector<polypitch::Ridge>> frames(150);
            std::vector<double> levels(150);
            for (int k = 0; k < 150; ++k)
            {
                polypitch::Ridge r;
                r.m = r.centre = 60.0;
                r.w = r.core = 1.0;
                r.age = k % 50;
                r.from = k % 50 > 0 ? 0 : -1;
                frames[(size_t) k].push_back(r);
                levels[(size_t) k] = k < 50 ? 0.0001 : k < 100 ? 0.02 : 0.1; // about -80, -34 and -20 dB
            }
            polypitch::refineTrace(frames, 46.875, polypitch::ANY_NOTE, nullptr, nullptr, &levels);
            const auto countIn = [&frames](int from, int to)
            {
                int n = 0;
                for (int k = from; k < to; ++k)
                    n += (int) frames[(size_t) k].size();
                return n;
            };
            check(countIn(0, 50) == 0 && countIn(50, 100) == 50 && countIn(100, 150) == 50, "the loudness gate: silence " + juce::String(countIn(0, 50)) + ", quiet " + juce::String(countIn(50, 100)) + ", loud " + juce::String(countIn(100, 150)));
        }

        // A faint voice only the generous tracker saw. It needs its own peak, off the louder note's series.
        for (const auto& [midi, peak, shouldShow, label] : std::vector<std::tuple<double, double, bool, const char*>> {
                 { 64.0, 0.5, true, "a faint voice with its own peak is added" },
                 { 64.0, 0.0, false, "without a peak it isn't" },
                 { 72.0, 0.5, false, "nor at a harmonic of the louder note" } })
        {
            std::vector<std::vector<polypitch::Ridge>> frames(60), candidates(60);
            for (int k = 0; k < 60; ++k)
            {
                polypitch::Ridge loud;
                loud.m = loud.centre = 60.0;
                loud.w = loud.core = 1.0;
                loud.age = k;
                loud.from = k > 0 ? 0 : -1;
                frames[(size_t) k].push_back(loud);
                candidates[(size_t) k].push_back(loud);
                polypitch::Ridge faint;
                faint.m = faint.centre = midi;
                faint.w = 0.3;
                faint.core = 0.03;
                faint.peak = peak;
                faint.age = k;
                faint.from = k > 0 ? 1 : -1;
                candidates[(size_t) k].push_back(faint);
            }
            polypitch::refineTrace(frames, 46.875, polypitch::ANY_NOTE, nullptr, &candidates);
            int shown = 0;
            double loudCore = 0.0, faintCore = 0.0;
            for (const auto& frame : frames)
                for (const auto& r : frame)
                {
                    if (r.m > 61.0) { ++shown; faintCore = juce::jmax(faintCore, r.core); }
                    else loudCore = juce::jmax(loudCore, r.core);
                }
            check((shown == 60) == shouldShow && (! shouldShow || (faintCore < 0.5 && loudCore == 1.0)), juce::String(label) + ": " + juce::String(shown) + " frames at " + juce::String(faintCore, 2));
        }

        // How fast a real recording goes through.
        double rate = 0.0;
        const auto mono = readWav(wav, rate);
        if (! mono.empty() && rate == 48000.0)
        {
            FakeHost real;
            real.audio = mono;
            PolyPitchAudioProcessor r;
            r.setTrackHost(&real);
            r.analyseTrackOnce();
            double ms = 0.0;
            pumpUntilAnalysed(r, ms);
            std::cout << "  " << (double) mono.size() / rate << " s analysed in " << ms / 1000.0 << " s" << std::endl;
            if (wav.getFileName().contains("6.1"))
                check(r.keyGuess().found && r.keyGuess().root == 1 && ! r.keyGuess().minor, "the D-flat major take is recognised");
        }
    }

    std::vector<float> at48k(std::vector<float> mono, double rate)
    {
        if (rate == 48000.0 || mono.empty())
            return mono;
        const double ratio = rate / 48000.0;
        std::vector<float> out((size_t) ((double) mono.size() / ratio));
        juce::LagrangeInterpolator interpolator;
        interpolator.process(ratio, mono.data(), out.data(), (int) out.size());
        return out;
    }

    // Runs the cleanup directly on a recording, and reports what it took out.
    int removedReport(const juce::File& wav, double fromSeconds, double toSeconds, int minFrames)
    {
        double rate = 0.0;
        auto mono = readWav(wav, rate);
        mono = at48k(std::move(mono), rate);
        polypitch::DualEngine engine(48000);
        polypitch::Tracker tracker;
        auto candidateTracker = polypitch::refineTracker();
        std::vector<std::vector<polypitch::Ridge>> frames, candidates;
        std::vector<float> padded(4096, 0.0f);
        padded.insert(padded.end(), mono.begin(), mono.end());
        padded.resize(padded.size() + 4096, 0.0f);
        for (size_t s = 0; s + 8192 <= padded.size(); s += 1024)
        {
            engine.analyse(padded.data() + s);
            candidates.push_back(candidateTracker.track(engine.laneEvidence(), engine.lanePitch(), engine.laneCount(), ! frames.empty(), polypitch::ANY_NOTE));
            polypitch::markPeaks(candidates.back(), engine.peakMidi(), engine.peakStrength(), engine.peakCount());
            frames.push_back(tracker.track(engine.laneEvidence(), engine.lanePitch(), engine.laneCount(), ! frames.empty(), polypitch::ANY_NOTE));
        }
        std::vector<polypitch::RemovedGroup> removed;
        const double fps = 48000.0 / 1024.0;
        auto cleaned = frames;
        polypitch::refineTrace(cleaned, fps, polypitch::ANY_NOTE, &removed, &candidates);
        // How much of a passage shows a second voice, not at a harmonic of the first.
        const auto twoVoices = [&](const std::vector<std::vector<polypitch::Ridge>>& f, double from, double to)
        {
            int sung = 0, both = 0;
            for (int k = (int) (from * fps); k < (int) (to * fps) && k < (int) f.size(); ++k)
            {
                const polypitch::Ridge* top = nullptr;
                for (const auto& r : f[(size_t) k])
                    if (top == nullptr || r.core > top->core)
                        top = &r;
                if (top == nullptr || top->core < 0.5 || top->age < 5)
                    continue;
                ++sung;
                for (const auto& r : f[(size_t) k])
                {
                    const double gap = std::abs(r.m - top->m);
                    if (r.core < 0.08 || gap < 1.5 || gap > 14.0 || std::abs(gap - 12.0) < 0.4)
                        continue;
                    ++both;
                    break;
                }
            }
            return juce::String(sung > 0 ? 100 * both / sung : 0) + "%";
        };
        std::cout << "second voice shown  verse 12-30: raw " << twoVoices(frames, 12, 30) << " clean " << twoVoices(cleaned, 12, 30)
                  << "   harmony 32-52: raw " << twoVoices(frames, 32, 52) << " clean " << twoVoices(cleaned, 32, 52) << std::endl;
        const auto noteOf = [](double m)
        {
            const int n = (int) std::floor(m + 0.5);
            return juce::String(polypitch::scales::noteName(n, false)).retainCharacters("ABCDEFG") + juce::String((int) std::floor(n / 12.0) - 1);
        };
        for (const auto& g : removed)
        {
            const double at = g.firstFrame / fps;
            if (at >= fromSeconds && at <= toSeconds && g.frames >= minFrames)
                std::cout << juce::String(at, 2) << "  " << juce::String(g.frames / fps, 2) << "s  " << noteOf(g.midi) << "  " << juce::String(g.midi, 2) << "  level " << juce::String(g.level, 3) << "  " << g.why << std::endl;
        }
        return 0;
    }

    // Loudness against clutter, every few seconds.
    int levelReport(const juce::File& wav, double stepSeconds)
    {
        double rate = 0.0;
        auto mono = readWav(wav, rate);
        mono = at48k(std::move(mono), rate);
        polypitch::DualEngine engine(48000);
        polypitch::Tracker tracker;
        auto candidateTracker = polypitch::refineTracker();
        std::vector<std::vector<polypitch::Ridge>> frames, candidates;
        std::vector<double> rms;
        std::vector<float> padded(4096, 0.0f);
        padded.insert(padded.end(), mono.begin(), mono.end());
        padded.resize(padded.size() + 4096, 0.0f);
        for (size_t s = 0; s + 8192 <= padded.size(); s += 1024)
        {
            engine.analyse(padded.data() + s);
            rms.push_back(engine.inputRms());
            candidates.push_back(candidateTracker.track(engine.laneEvidence(), engine.lanePitch(), engine.laneCount(), ! frames.empty(), polypitch::ANY_NOTE));
            polypitch::markPeaks(candidates.back(), engine.peakMidi(), engine.peakStrength(), engine.peakCount());
            frames.push_back(tracker.track(engine.laneEvidence(), engine.lanePitch(), engine.laneCount(), ! frames.empty(), polypitch::ANY_NOTE));
        }
        auto cleaned = frames;
        const double fps = 48000.0 / 1024.0;
        const auto stats = polypitch::refineTrace(cleaned, fps, polypitch::ANY_NOTE, nullptr, &candidates, &rms);
        std::cout << "groups kept " << stats.kept << " faint voices " << stats.faintVoices << std::endl;
        std::cout << "time  rms-dB  ridges/frame raw  clean  clean-strong" << std::endl;
        const int step = (int) (stepSeconds * fps);
        for (int k = 0; k + step <= (int) frames.size(); k += step)
        {
            double sum = 0.0;
            int raw = 0, clean = 0, strong = 0;
            for (int i = k; i < k + step; ++i)
            {
                sum += rms[(size_t) i] * rms[(size_t) i];
                raw += (int) frames[(size_t) i].size();
                clean += (int) cleaned[(size_t) i].size();
                for (const auto& r : cleaned[(size_t) i]) if (r.core >= polypitch::RIDGE_STRONG) ++strong;
            }
            std::cout << juce::String(k / fps, 0).paddedLeft(' ', 4) << "  " << juce::String(20.0 * std::log10(std::sqrt(sum / step) + 1e-9), 1).paddedLeft(' ', 6) << "  " << juce::String((double) raw / step, 1).paddedLeft(' ', 5) << "  " << juce::String((double) clean / step, 1).paddedLeft(' ', 5) << "  " << juce::String((double) strong / step, 1).paddedLeft(' ', 5) << std::endl;
        }
        return 0;
    }

    // Follows one pitch through the stages: the engine's lanes, its spectral peaks, then ridges.
    int probeReport(const juce::File& wav, double fromSeconds, double toSeconds, double midi)
    {
        double rate = 0.0;
        auto mono = readWav(wav, rate);
        mono = at48k(std::move(mono), rate);
        polypitch::DualEngine engine(48000);
        polypitch::Tracker tracker;
        std::vector<std::vector<polypitch::Ridge>> frames;
        std::vector<float> padded(4096, 0.0f);
        padded.insert(padded.end(), mono.begin(), mono.end());
        padded.resize(padded.size() + 4096, 0.0f);
        const double fps = 48000.0 / 1024.0;
        auto candidateTracker = polypitch::refineTracker();
        std::vector<std::vector<polypitch::Ridge>> candidates;
        struct Row { double laneMax, laneTop, laneTopMidi, peak, peakMidi; };
        std::vector<Row> rows;
        const int lanes = engine.laneCount();
        for (size_t s = 0; s + 8192 <= padded.size(); s += 1024)
        {
            engine.analyse(padded.data() + s);
            Row row {};
            for (int i = 0; i < lanes; ++i)
            {
                const double e = engine.laneEvidence()[i], m = engine.laneMidi()[i];
                if (e > row.laneTop) { row.laneTop = e; row.laneTopMidi = m; }
                if (std::abs(m - midi) <= 0.4 && e > row.laneMax) row.laneMax = e;
            }
            for (int i = 0; i < engine.peakCount(); ++i)
                if (std::abs(engine.peakMidi()[i] - midi) <= 0.4 && engine.peakStrength()[i] > row.peak)
                {
                    row.peak = engine.peakStrength()[i];
                    row.peakMidi = engine.peakMidi()[i];
                }
            rows.push_back(row);
            candidates.push_back(candidateTracker.track(engine.laneEvidence(), engine.lanePitch(), lanes, ! frames.empty(), polypitch::ANY_NOTE));
            polypitch::markPeaks(candidates.back(), engine.peakMidi(), engine.peakStrength(), engine.peakCount());
            frames.push_back(tracker.track(engine.laneEvidence(), engine.lanePitch(), lanes, ! frames.empty(), polypitch::ANY_NOTE));
        }
        auto cleaned = frames;
        polypitch::refineTrace(cleaned, fps, polypitch::ANY_NOTE, nullptr, &candidates);
        std::cout << "time   lane@note top-lane(midi)  peak(midi)      ridge-w ratio core  clean-core" << std::endl;
        for (int k = (int) (fromSeconds * fps); k < (int) (toSeconds * fps) && k < (int) frames.size(); k += 6)
        {
            const auto& row = rows[(size_t) k];
            double loud = 0.0, w = 0.0, core = 0.0, cleanCore = 0.0;
            for (const auto& r : frames[(size_t) k]) { loud = juce::jmax(loud, r.w); if (std::abs(r.m - midi) <= 0.4 && r.w > w) { w = r.w; core = r.core; } }
            for (const auto& r : cleaned[(size_t) k]) if (std::abs(r.m - midi) <= 0.4) cleanCore = juce::jmax(cleanCore, r.core);
            std::cout << juce::String(k / fps, 2) << "  " << juce::String(row.laneMax, 3) << "   " << juce::String(row.laneTop, 3) << " (" << juce::String(row.laneTopMidi, 1) << ")   "
                      << juce::String(row.peak, 3) << " (" << juce::String(row.peakMidi, 2) << ")   " << juce::String(w, 3) << " " << juce::String(loud > 0.0 ? w / loud : 0.0, 2) << " " << juce::String(core, 3) << "   " << juce::String(cleanCore, 3) << std::endl;
        }
        return 0;
    }

    // Saved analyses, the tuning reference, other tracks and the bar grid.
    void pluginFeatures()
    {
        const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("polypitch-test-analyses");
        folder.deleteRecursively();
        PolyPitchAudioProcessor::setAnalysisFolder(folder);

        // Three seconds of A at 443 Hz, a little sharp of A=440.
        FakeHost host;
        host.audio = tone(48000.0, 3.0, 443.0);
        double took = 0.0;

        const auto summary = [](PolyPitchAudioProcessor& p)
        {
            const std::lock_guard<std::mutex> lock(p.traceLock());
            int ridges = 0;
            double total = 0.0;
            for (const auto& frame : p.traceFrames())
                for (const auto& r : frame.ridges)
                {
                    ++ridges;
                    total += r.m + r.core;
                }
            return std::pair<int, double>(ridges, total);
        };

        PolyPitchAudioProcessor first;
        first.setTrackHost(&host);
        first.analyseTrackOnce();
        pumpUntilAnalysed(first, took);
        const auto saved = folder.findChildFiles(juce::File::findFiles, false, "*.ppa");
        check(saved.size() == 1 && ! first.loadedSavedAnalysis(), "a finished analysis is saved: " + juce::String(saved.size()) + " file, " + juce::String(saved.isEmpty() ? 0 : (int) saved[0].getSize()) + " bytes");

        {
            PolyPitchAudioProcessor again;
            again.setTrackHost(&host);
            again.analyseTrackOnce();
            const auto a = summary(first), b = summary(again);
            check(again.loadedSavedAnalysis() && ! again.isAnalysing(), "the same audio opens at once from it");
            check(a.first == b.first && std::abs(a.second - b.second) < 1.0e-3 * juce::jmax(1.0, std::abs(a.second)), "with the same trace: " + juce::String(a.first) + " and " + juce::String(b.first) + " ridges");
            check(again.keyGuess().found, "and a key suggestion");
            again.setCleanTrace(false);
            check(summary(again).first > 0, "and the trace as first heard too");
        }

        {
            FakeHost edited = host;
            for (auto& v : edited.audio)
                v *= 0.8f;
            PolyPitchAudioProcessor changed;
            changed.setTrackHost(&edited);
            changed.analyseTrackOnce();
            check(! changed.loadedSavedAnalysis() && changed.isAnalysing(), "changed audio is analysed afresh");
            pumpUntilAnalysed(changed, took);
        }

        // The tuning reference: at A=440 this note is 12 cents sharp, at A=443 it's in tune.
        const auto aIsGreen = [](PolyPitchAudioProcessor& p)
        {
            const std::lock_guard<std::mutex> lock(p.traceLock());
            const auto& frames = p.traceFrames();
            for (const auto& r : frames[frames.size() / 2].ridges)
                if (std::abs(r.m - 69.1) < 0.3)
                    return r.inTune;
            return false;
        };
        check(! aIsGreen(first), "A at 443 Hz is not in tune against A=440");
        first.setReference(443.0);
        check(aIsGreen(first) && std::abs(first.pitchOffset() - 0.118) < 0.002, "but is against A=443: offset " + juce::String(first.pitchOffset(), 3));
        check(first.keyGuess().tuningCents > 9.0 && first.keyGuess().tuningCents < 14.0, "and its tuning is measured: " + juce::String(first.keyGuess().tuningCents, 1) + " cents");

        // Other tracks see each other, and forget each other.
        {
            FakeHost harmony;
            harmony.audio = tone(48000.0, 3.0, 554.37);
            PolyPitchAudioProcessor second;
            second.setTrackHost(&harmony);
            second.analyseTrackOnce();
            pumpUntilAnalysed(second, took);
            const auto others = first.otherInstances();
            check(std::find(others.begin(), others.end(), &second) != others.end(), "another PolyPitch track is visible to the first");

            PolyPitchAudioProcessorEditor editor(first);
            editor.setSize(1100, 560);
            host.position = 1.5;
            editor.trace().tick();
            editor.trace().tick();
            const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
            check(editor.trace().otherTracksShown() == 1, "and drawn alongside it: " + juce::String(editor.trace().otherTracksShown()));
            // At 120 bpm, a beat every half second. The view runs from 1.83 s before the project starts to
            // 8.17 s, and there are no beats before the start, so 0 to 8 s: 17 lines, give or take an edge.
            check(editor.trace().gridLineCount() >= 16 && editor.trace().gridLineCount() <= 18, "the bar grid: " + juce::String(editor.trace().gridLineCount()) + " lines");
            const auto out = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("polypitch-features.png");
            out.deleteFile();
            juce::FileOutputStream stream(out);
            juce::PNGImageFormat().writeImageToStream(image, stream);
        }
        const auto after = first.otherInstances();
        check(after.empty(), "a removed track is forgotten: " + juce::String((int) after.size()) + " left");
        folder.deleteRecursively();
        PolyPitchAudioProcessor::setAnalysisFolder({});
    }

    int timelinePicture(const juce::File& wav, const juce::File& out, double playhead, double seconds, const juce::String& mode, double spanWanted, double centreWanted)
    {
        double rate = 0.0;
        FakeHost host;
        auto mono = readWav(wav, rate);
        host.audio = at48k(std::move(mono), rate);
        if (host.audio.empty())
            return 1;
        host.position = playhead;
        PolyPitchAudioProcessor p;
        p.view().seconds = seconds;
        p.view().clean = mode != "raw";
        if (spanWanted > 0.0)
        {
            p.view().span = spanWanted;
            p.view().centre = centreWanted;
            p.view().follow = false;
        }
        p.setTrackHost(&host);
        PolyPitchAudioProcessorEditor editor(p);
        editor.setSize(1100, 560);
        double took = 0.0;
        for (int i = 0; i < 2; ++i)
        {
            editor.trace().tick();
            pumpUntilAnalysed(p, took);
        }
        editor.trace().tick();
        editor.trace().tick();
        const auto stats = p.refineStats();
        const auto key = p.keyGuess();
        std::cout << "tracks " << stats.tracks << " groups kept " << stats.kept << " faint " << stats.tooFaint << " short " << stats.tooShort << " harmonics " << stats.harmonics << " joined " << stats.joined << " recovered " << stats.recovered << " faint voices " << stats.faintVoices << std::endl;
        std::cout << "key " << juce::String(polypitch::scales::noteName(key.root, false)).retainCharacters("ABCDEFG") << (key.root == 1 || key.root == 3 || key.root == 6 || key.root == 8 || key.root == 10 ? "#" : "") << (key.minor ? " minor" : " major") << " score " << key.score << ", then " << juce::String(polypitch::scales::noteName(key.nextRoot, false)).retainCharacters("ABCDEFG") << (key.nextRoot == 1 || key.nextRoot == 3 || key.nextRoot == 6 || key.nextRoot == 8 || key.nextRoot == 10 ? "#" : "") << (key.nextMinor ? " minor" : " major") << " " << key.nextScore << " focus " << key.tuningFocus << " tuning " << key.tuningCents << " cents, A=" << 440.0 * std::pow(2.0, key.tuningCents / 1200.0) << std::endl;
        for (int pc = 0; pc < 12; ++pc)
            std::cout << juce::String(polypitch::scales::noteName(pc, false)).retainCharacters("ABCDEFG") << (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10 ? "#" : "") << " " << juce::roundToInt(key.weight[pc] * 100.0) << "  ";
        std::cout << std::endl;
        std::cout << "rests ";
        for (int pc = 0; pc < 12; ++pc)
            std::cout << juce::roundToInt(key.finals[pc] * 100.0) << " ";
        std::cout << std::endl;
        const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 1.0f);
        out.deleteFile();
        juce::FileOutputStream stream(out);
        juce::PNGImageFormat png;
        return png.writeImageToStream(image, stream) ? 0 : 1;
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

    if (argc >= 4 && juce::String(argv[2]) == "--levels")
        return levelReport(juce::File(argv[1]), juce::String(argv[3]).getDoubleValue());

    if (argc >= 6 && juce::String(argv[2]) == "--probe")
        return probeReport(juce::File(argv[1]), juce::String(argv[3]).getDoubleValue(), juce::String(argv[4]).getDoubleValue(), juce::String(argv[5]).getDoubleValue());

    if (argc >= 6 && juce::String(argv[2]) == "--removed")
        return removedReport(juce::File(argv[1]), juce::String(argv[3]).getDoubleValue(), juce::String(argv[4]).getDoubleValue(), juce::String(argv[5]).getIntValue());

    if (argc >= 4 && juce::String(argv[2]) == "--timeline")
        return timelinePicture(juce::File(argv[1]), juce::File(argv[3]),
                               argc > 4 ? juce::String(argv[4]).getDoubleValue() : 0.0,
                               argc > 5 ? juce::String(argv[5]).getDoubleValue() : 10.0,
                               argc > 6 ? juce::String(argv[6]) : juce::String("clean"),
                               argc > 7 ? juce::String(argv[7]).getDoubleValue() : 0.0,
                               argc > 8 ? juce::String(argv[8]).getDoubleValue() : 0.0);

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
    falseOctave();
    pluginFeatures();
    if (argc == 2)
    {
        matchesEngine(juce::File(argv[1]));
        timelineChecks(juce::File(argv[1]));
    }

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
