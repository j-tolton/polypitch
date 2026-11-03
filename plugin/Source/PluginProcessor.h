#pragma once

#include <JuceHeader.h>
#include "polypitch.h"
#include "tracker.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <vector>

// One analysed moment. Lanes keep only a byte each, because five minutes of them add up.
struct TraceFrame
{
    double timeMs = 0.0;
    std::vector<polypitch::Ridge> ridges;
    std::vector<juce::uint8> field;
};

class PolyPitchAudioProcessor final : public juce::AudioProcessor,
                                      private juce::Thread
{
public:
    // What the editor shows, kept with the project.
    struct ViewSettings
    {
        int rootPc = 0;
        juce::String scaleId { "off" };
        double seconds = 10.0;
        double span = 30.0;
        double centre = 57.0;
        bool follow = true;
        int width = 960;
        int height = 540;
    };

    PolyPitchAudioProcessor();
    ~PolyPitchAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PolyPitch"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // Hold this lock while reading frames. Only the editor and the analysis thread ever take it.
    std::mutex& traceLock() noexcept { return frameLock; }
    const std::deque<TraceFrame>& traceFrames() const noexcept { return frames; }
    juce::uint32 traceGeneration() const noexcept { return generation.load(); }

    // Milliseconds of audio heard so far, and when the last of it arrived.
    double heardMs() const noexcept;
    double lastHeardAt() const noexcept { return lastBlockAt.load(); }
    double blockMs() const noexcept { return lastBlockMs.load(); }

    void setTargets(int mask);
    ViewSettings& view() noexcept { return viewSettings; }

    // Waits until the analysis has caught up with the audio. For tests.
    bool waitForAnalysis(int timeoutMs);

private:
    void run() override;
    void analyseMono(const float* samples, int count);
    void configure(double sampleRate);
    void analyseWindow();

    static constexpr int fftSize = 8192;
    static constexpr int hopSize = 1024;
    static constexpr int laneCount = 741;
    static constexpr double historyMs = 300000.0;
    static constexpr int fifoSize = 1 << 19;
    static constexpr double targetRate = 48000.0;

    // Written by the audio thread.
    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoData;
    std::atomic<juce::int64> heardSamples { 0 };
    std::atomic<double> lastBlockAt { 0.0 };
    std::atomic<double> lastBlockMs { 0.0 };
    std::atomic<double> hostRate { 48000.0 };

    // Used by the analysis thread.
    std::unique_ptr<polypitch::Engine> engine;
    polypitch::Tracker tracker;
    std::vector<float> ring, window, antiAlias, decimatorHistory;
    int decimation = 1, decimatorWrite = 0, decimatorCountdown = 0;
    int ringWrite = 0, ringFilled = 0, sinceAnalysis = 0;
    juce::int64 analysedSamples = 0;
    double analysisRate = 48000.0;
    bool isFirstFrame = true;

    // Shared between the analysis thread and the editor.
    std::mutex frameLock;
    std::deque<TraceFrame> frames;
    std::atomic<juce::uint32> generation { 0 };
    int targets = polypitch::ANY_NOTE;

    ViewSettings viewSettings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PolyPitchAudioProcessor)
};
