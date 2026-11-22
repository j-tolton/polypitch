#include "ReaperBridge.h"

JUCE_BEGIN_IGNORE_WARNINGS_MSVC(4100)
#include <pluginterfaces/base/funknown.h>
JUCE_END_IGNORE_WARNINGS_MSVC

namespace
{
    // From Reaper's SDK. Must match its layout exactly.
    class IReaperHostApplication : public Steinberg::FUnknown
    {
    public:
        virtual void* PLUGIN_API getReaperApi(Steinberg::CStringA name) = 0;
        virtual void* PLUGIN_API getReaperParent(Steinberg::uint32 which) = 0; // track=1, take=2, project=3
        virtual void* PLUGIN_API reaperExtended(Steinberg::uint32 call, void* a, void* b, void* c) = 0;
    };

    constexpr Steinberg::uint32 parentTrack = 1, parentProject = 3;

    IReaperHostApplication* reaperOf(Steinberg::FUnknown* unknown)
    {
        return static_cast<IReaperHostApplication*>(unknown);
    }
}

ReaperBridge::~ReaperBridge()
{
    closeAccessor();
    if (hostInterface != nullptr)
        hostInterface->release();
}

void ReaperBridge::setIHostApplication(Steinberg::FUnknown* host)
{
    if (hostInterface != nullptr)
    {
        closeAccessor();
        hostInterface->release();
        hostInterface = nullptr;
    }
    if (host == nullptr)
        return;

    const Steinberg::TUID reaperId = INLINE_UID(0x79655E36, 0x77EE4267, 0xA573FEF7, 0x4912C27C);
    void* found = nullptr;
    if (host->queryInterface(reaperId, &found) != Steinberg::kResultOk || found == nullptr)
        return;
    auto* reaper = static_cast<IReaperHostApplication*>(found);
    hostInterface = reaper;

    const auto lookUp = [reaper](auto& function, const char* name)
    {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(reaper->getReaperApi(name));
        return function != nullptr;
    };

    bool complete = lookUp(createAccessor, "CreateTrackAudioAccessor");
    complete = lookUp(destroyAccessor, "DestroyAudioAccessor") && complete;
    complete = lookUp(accessorStart, "GetAudioAccessorStartTime") && complete;
    complete = lookUp(accessorEnd, "GetAudioAccessorEndTime") && complete;
    complete = lookUp(accessorSamples, "GetAudioAccessorSamples") && complete;
    complete = lookUp(playStateEx, "GetPlayStateEx") && complete;
    complete = lookUp(playPositionEx, "GetPlayPositionEx") && complete;
    complete = lookUp(cursorPositionEx, "GetCursorPositionEx") && complete;
    complete = lookUp(setEditCursor, "SetEditCurPos2") && complete;
    complete = lookUp(playButtonEx, "OnPlayButtonEx") && complete;
    complete = lookUp(stopButtonEx, "OnStopButtonEx") && complete;
    if (! lookUp(accessorChanged, "AudioAccessorStateChanged"))
        lookUp(accessorChanged, "AudioAccessorValidateState");
    lookUp(getTrackNameFn, "GetTrackName");
    lookUp(timeToBeats, "TimeMap2_timeToBeats");
    lookUp(beatsToTime, "TimeMap2_beatsToTime");

    if (complete && onAvailable != nullptr)
        onAvailable();
}

void ReaperBridge::closeAccessor()
{
    if (accessor != nullptr && destroyAccessor != nullptr)
        destroyAccessor(accessor);
    accessor = nullptr;
}

bool ReaperBridge::open()
{
    closeAccessor();
    if (hostInterface == nullptr)
        return false;
    track = reaperOf(hostInterface)->getReaperParent(parentTrack);
    project = reaperOf(hostInterface)->getReaperParent(parentProject);
    if (track == nullptr)
        return false;
    accessor = createAccessor(track);
    return accessor != nullptr;
}

double ReaperBridge::startSeconds()
{
    return accessor != nullptr ? accessorStart(accessor) : 0.0;
}

double ReaperBridge::endSeconds()
{
    return accessor != nullptr ? accessorEnd(accessor) : 0.0;
}

void ReaperBridge::read(double start, int rate, int samplesPerChannel, double* stereo)
{
    std::fill(stereo, stereo + samplesPerChannel * 2, 0.0);
    if (accessor != nullptr)
        accessorSamples(accessor, rate, 2, start, samplesPerChannel, stereo);
}

bool ReaperBridge::audioChanged()
{
    return accessor != nullptr && accessorChanged != nullptr && accessorChanged(accessor);
}

juce::String ReaperBridge::trackName()
{
    char name[256] = {};
    if (track != nullptr && getTrackNameFn != nullptr && getTrackNameFn(track, name, (int) sizeof(name)))
        return juce::String::fromUTF8(name);
    return "track";
}

bool ReaperBridge::isPlaying()
{
    // Bit 0 is playing and bit 2 recording. Bit 1 is paused, which isn't moving.
    if (hostInterface == nullptr)
        return false;
    const int state = playStateEx(project);
    return (state & 2) == 0 && (state & 5) != 0;
}

double ReaperBridge::positionSeconds()
{
    if (hostInterface == nullptr)
        return 0.0;
    return playStateEx(project) != 0 ? playPositionEx(project) : cursorPositionEx(project);
}

void ReaperBridge::seek(double seconds)
{
    if (hostInterface != nullptr)
        setEditCursor(project, seconds, false, true);
}

void ReaperBridge::togglePlay()
{
    if (hostInterface == nullptr)
        return;
    if (playStateEx(project) != 0)
        stopButtonEx(project);
    else
        playButtonEx(project);
}

void ReaperBridge::gridLines(double fromSeconds, double toSeconds, bool beats, std::vector<GridLine>& lines)
{
    if (hostInterface == nullptr || timeToBeats == nullptr || beatsToTime == nullptr)
        return;
    // Measures count from zero here and from one on Reaper's ruler. Beat positions are relative
    // to the start of their measure, so tempo and time signature changes come out right.
    int measure = 0, beatsInBar = 4, denominator = 4;
    double fullBeats = 0.0;
    timeToBeats(project, juce::jmax(0.0, fromSeconds), &measure, &beatsInBar, &fullBeats, &denominator);
    for (int guard = 0; guard < 4000; ++guard, ++measure)
    {
        const double barStart = beatsToTime(project, 0.0, &measure);
        if (barStart > toSeconds)
            break;
        if (barStart >= fromSeconds)
            lines.push_back({ barStart, measure + 1, true });
        if (! beats)
            continue;
        int sameMeasure = 0, length = 4, denom = 4;
        double unused = 0.0;
        timeToBeats(project, barStart + 1.0e-6, &sameMeasure, &length, &unused, &denom);
        for (int beat = 1; beat < length; ++beat)
        {
            const double at = beatsToTime(project, (double) beat, &measure);
            if (at > toSeconds)
                break;
            if (at >= fromSeconds)
                lines.push_back({ at, measure + 1, false });
        }
    }
}
