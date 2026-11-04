#pragma once

#include <JuceHeader.h>
#include "TrackHost.h"
#include <functional>

// Reaper hands a VST3 its extension API, which can read a track's audio and drive the transport.
// In any other host setIHostApplication finds nothing and the plugin stays on live listening.
class ReaperBridge final : public juce::VST3ClientExtensions,
                           public TrackHost
{
public:
    ~ReaperBridge() override;

    std::function<void()> onAvailable;

    void setIHostApplication(Steinberg::FUnknown*) override;

    bool open() override;
    double startSeconds() override;
    double endSeconds() override;
    void read(double startSeconds, int rate, int samplesPerChannel, double* stereo) override;
    bool audioChanged() override;
    juce::String trackName() override;
    bool isPlaying() override;
    double positionSeconds() override;
    void seek(double seconds) override;
    void togglePlay() override;

private:
    void closeAccessor();

    Steinberg::FUnknown* hostInterface = nullptr;
    void* track = nullptr;
    void* project = nullptr;
    void* accessor = nullptr;

    void* (*createAccessor)(void*) = nullptr;
    void (*destroyAccessor)(void*) = nullptr;
    double (*accessorStart)(void*) = nullptr;
    double (*accessorEnd)(void*) = nullptr;
    int (*accessorSamples)(void*, int, int, double, int, double*) = nullptr;
    bool (*accessorChanged)(void*) = nullptr;
    bool (*getTrackNameFn)(void*, char*, int) = nullptr;
    int (*playStateEx)(void*) = nullptr;
    double (*playPositionEx)(void*) = nullptr;
    double (*cursorPositionEx)(void*) = nullptr;
    void (*setEditCursor)(void*, double, bool, bool) = nullptr;
    void (*playButtonEx)(void*) = nullptr;
    void (*stopButtonEx)(void*) = nullptr;
};
