#pragma once

#include <JuceHeader.h>

struct GridLine
{
    double seconds = 0.0;
    int bar = 1;
    bool barStart = true;
};

// What the plugin needs from the DAW to work on a whole track rather than listening to it.
// All of this is called on the message thread.
class TrackHost
{
public:
    virtual ~TrackHost() = default;

    // Starts reading the track as it is now. False if there is no track to read.
    virtual bool open() = 0;
    virtual double startSeconds() = 0;
    virtual double endSeconds() = 0;
    // Interleaved stereo at the given rate. Anywhere without audio reads as silence.
    virtual void read(double startSeconds, int rate, int samplesPerChannel, double* stereo) = 0;
    // True if the track's audio has changed since open().
    virtual bool audioChanged() = 0;
    virtual juce::String trackName() = 0;

    virtual bool isPlaying() = 0;
    // What is being heard while playing, otherwise the edit cursor.
    virtual double positionSeconds() = 0;
    virtual void seek(double seconds) = 0;
    virtual void togglePlay() = 0;
    // Bar lines, and beats too if asked, between two project times. None if the host can't say.
    virtual void gridLines(double fromSeconds, double toSeconds, bool beats, std::vector<GridLine>& lines) { juce::ignoreUnused(fromSeconds, toSeconds, beats, lines); }
};
