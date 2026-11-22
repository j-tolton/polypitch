#pragma once

#include <JuceHeader.h>
#include "polypitch.h"
#include "dual_engine.h"
#include "tracker.h"
#include "refine.h"
#include "TrackHost.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <vector>

#if ! POLYPITCH_NO_PLUGIN_ENTRY
 #include "ReaperBridge.h"
#endif

// One analysed moment. Lanes keep only a byte each, because five minutes of them add up.
struct TraceFrame
{
    double timeMs = 0.0;
    float level = 0.0f;
    std::vector<polypitch::Ridge> ridges;
    // On a timeline, the other version of this frame: cleaned up or as first heard.
    std::vector<polypitch::Ridge> alternative;
    std::vector<juce::uint8> field;
};

class PolyPitchAudioProcessor final : public juce::AudioProcessor,
                                      private juce::Thread,
                                      private juce::Timer
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
        bool clean = true;
        bool showOthers = true;
        double referenceHz = 440.0;
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
    // The pitch that counts as A, so a track that isn't at 440 is judged on its own terms.
    void setReference(double hz);
    double pitchOffset() const noexcept { return referenceOffset.load(); }

    // Working on a whole track. Frames are placed on the project timeline, and the live
    // audio is ignored. Without a track to read the plugin listens instead.
    void setTrackHost(TrackHost*);
    TrackHost* trackHost() noexcept { return track; }
    bool isTimeline() const noexcept { return timeline.load(); }
    // Uses a saved analysis of the same audio when there is one, unless told not to.
    bool startTrackAnalysis(bool useSaved = true);
    void analyseTrackOnce();
    bool isAnalysing() const noexcept { return timeline.load() && ! refined.load(); }
    // Show the cleaned-up trace, or everything as first heard.
    void setCleanTrace(bool shouldBeClean);
    bool hasCleanTrace() const noexcept { return timeline.load() && refined.load(); }
    bool isShowingClean() const noexcept { return hasCleanTrace() && viewSettings.clean; }
    polypitch::KeyGuess keyGuess() const;
    polypitch::RefineStats refineStats() const;
    double analysisProgress() const noexcept;
    bool trackChanged() const noexcept { return changed; }
    double trackSeconds() const noexcept { return trackEnd - trackStart; }
    const juce::String& trackTitle() const noexcept { return trackLabel; }

#if ! POLYPITCH_NO_PLUGIN_ENTRY
    juce::VST3ClientExtensions* getVST3ClientExtensions() override { return &reaper; }
#endif
    ViewSettings& view() noexcept { return viewSettings; }

    // Every PolyPitch loaded in this host, so each can show the others' notes alongside its own.
    std::vector<PolyPitchAudioProcessor*> otherInstances() const;
    int colourIndex() const noexcept { return colourSlot; }
    bool loadedSavedAnalysis() const noexcept { return fromSaved; }

    // Where finished analyses are kept between sessions.
    static juce::File analysisFolder();
    static void setAnalysisFolder(const juce::File&);

    // Waits until the analysis has caught up with the audio. For tests.
    bool waitForAnalysis(int timeoutMs);

private:
    void run() override;
    void timerCallback() override;
    void readTrackChunk();
    void pushChunk(std::vector<float>&&);
    void refineFrames();
    void analyseMono(const float* samples, int count);
    void configure(double sampleRate);
    void analyseWindow();
    juce::String fingerprint(double start, double end);
    void saveAnalysis();
    bool loadAnalysis(const juce::String& key);
    void retintShown();

    static constexpr int fftSize = 8192;
    static constexpr int hopSize = 1024;
    static constexpr int laneCount = 741;
    static constexpr double historyMs = 300000.0;
    static constexpr int fifoSize = 1 << 19;
    static constexpr double targetRate = 48000.0;
    static constexpr int trackChunk = 24000, maxQueuedChunks = 8;

    // Written by the audio thread.
    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoData;
    std::atomic<juce::int64> heardSamples { 0 };
    std::atomic<double> lastBlockAt { 0.0 };
    std::atomic<double> lastBlockMs { 0.0 };
    std::atomic<double> hostRate { 48000.0 };

    // Used by the analysis thread.
    std::unique_ptr<polypitch::Engine> engine;
    std::unique_ptr<polypitch::DualEngine> trackEngine;
    polypitch::Tracker tracker;
    polypitch::Tracker candidateTracker = polypitch::refineTracker();
    std::vector<float> ring, window, antiAlias, decimatorHistory;
    int decimation = 1, decimatorWrite = 0, decimatorCountdown = 0;
    int ringWrite = 0, ringFilled = 0, sinceAnalysis = 0;
    juce::int64 analysedSamples = 0;
    double analysisRate = 48000.0;
    bool isFirstFrame = true;

    // Reading a track. The message thread reads it and the analysis thread takes the chunks.
    std::mutex chunkLock;
    std::deque<std::vector<float>> chunks;
    TrackHost* track = nullptr;
    std::atomic<bool> timeline { false };
    std::atomic<juce::int64> analysedSamples2 { 0 }, totalSamples { 0 };
    std::atomic<bool> refined { false };
    bool showingClean = true;
    polypitch::KeyGuess guess;
    polypitch::RefineStats lastStats;
    double trackStart = 0.0, trackEnd = 0.0;
    juce::int64 trackRead = 0, trackLength = 0;
    bool readingTrack = false, changed = false, askedToAnalyse = false;
    int pollCountdown = 0;
    juce::String trackLabel, analysisKey;
    bool fromSaved = false;
    int colourSlot = 0;
    std::atomic<double> referenceOffset { 0.0 };

    // Shared between the analysis thread and the editor.
    std::mutex frameLock;
    std::deque<TraceFrame> frames;
    std::atomic<juce::uint32> generation { 0 };
    int targets = polypitch::ANY_NOTE;

    ViewSettings viewSettings;

#if ! POLYPITCH_NO_PLUGIN_ENTRY
    ReaperBridge reaper;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PolyPitchAudioProcessor)
};
