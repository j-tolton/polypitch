#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <array>
#include <functional>
#include <map>

// The scrolling pitch trace, drawn the way the web app draws it: soft dots add up into lines
// on 500 ms tiles, so only the newest tile changes as sound arrives.
class TraceView final : public juce::Component
{
public:
    explicit TraceView(PolyPitchAudioProcessor&);

    // Call once per display frame.
    void tick();

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void zoomPitch(double factor);
    void zoomTime(double factor);
    void fitPitch();
    void setFollow(bool shouldFollow);
    bool isFollowing() const noexcept { return followOn; }
    void goLive();
    void lookBack(double milliseconds);
    void settingsChanged();

    // The note to show, or a negative number when there is none, and whether it is in tune.
    std::function<void(double, bool)> onReadout;
    std::function<void()> onFollowChanged;

    double lastPaintMs() const noexcept { return paintMs; }

private:
    struct Tile
    {
        juce::Image image;
        std::vector<float> light;
        int column = 0, width = 0, dirtyFrom = 0, dirtyTo = 0;
        double start = 0.0, end = 0.0, margin = 0.0, lastTime = 0.0;
        int count = 0;
        bool complete = false;
    };

    struct Tint
    {
        float red, green, blue, gain;
    };

    double yFor(double midi) const noexcept;
    double tileY(double midi) const noexcept;
    double displayTime() const;
    double liveEdge() const;
    bool audioFlowing() const;
    void clampView();
    void setManualView();
    void panPitch(double semitones);
    void zoomPitchAt(double factor, float anchorY);
    void setReview(double timeMs);
    size_t firstFrameAfter(double timeMs) const;
    void saveView();
    void applySettings();
    juce::String noteLabel(int midi) const;
    void updateLive();

    void paintGrid(juce::Graphics&) const;
    void paintTiles(juce::Graphics&, double now, float scale);
    void paintTimeAxis(juce::Graphics&, double now) const;
    void paintCursor(juce::Graphics&, double now);
    Tile& ensureTile(int index, double pixelsPerMs, float scale);
    void appendFrames(Tile&, double pixelsPerMs, float scale);
    void paintFrame(Tile&, const TraceFrame&, const TraceFrame*, double pixelsPerMs, float scale);
    void paintField(Tile&, const TraceFrame&, double x1, double frameDx, float scale);
    void paintDot(Tile&, double x, double midi, double alpha, int level, float scale);
    void publish(Tile&);

    PolyPitchAudioProcessor& processor;

    double lo = 42.0, hi = 72.0, span = 30.0;
    double seconds = 10.0, secondsExact = 10.0;
    bool followOn = true, hasFollowGoal = false;
    double followGoal = 0.0;
    bool inReview = false;
    double reviewEnd = 0.0;
    double readoutMidi = -1.0, readoutShownAt = 0.0, seenFrameTime = -1.0;
    mutable double lastEdge = 0.0;
    double lastTickAt = 0.0;
    bool needsDraw = true;
    bool flatNames = false;
    int rootPc = 0, targetBits = polypitch::ANY_NOTE;
    double paintMs = 0.0;

    std::map<int, Tile> tiles;
    double tileLo = 0.0, tileHi = 0.0;
    int tileCssHeight = 0;
    juce::String tileKey;
    juce::uint32 tileGeneration = 0;
    std::array<float, 1024> dotShape {};
    std::array<float, 256> fieldShape {};
    std::array<Tint, 13> tints {};

    struct Pan
    {
        juce::Point<float> start;
        double lo = 0.0, hi = 0.0, end = 0.0;
        int axis = 0; // 0 undecided, 1 time, 2 pitch
    } pan;
    bool hasCursor = false;
    juce::Point<float> cursor;

    juce::TextButton liveButton { "Back to live" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TraceView)
};
