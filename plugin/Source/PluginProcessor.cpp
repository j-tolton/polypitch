#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Scales.h"

PolyPitchAudioProcessor::PolyPitchAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("PolyPitch analysis")
{
    fifoData.resize((size_t) fifoSize);
#if ! POLYPITCH_NO_PLUGIN_ENTRY
    reaper.onAvailable = [this] { setTrackHost(&reaper); };
#endif
}

PolyPitchAudioProcessor::~PolyPitchAudioProcessor()
{
    stopTimer();
    stopThread(2000);
}

bool PolyPitchAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    return in == layouts.getMainOutputChannelSet();
}

void PolyPitchAudioProcessor::configure(double sampleRate)
{
    // The analysis expects about 48 kHz: at higher rates its window would be too short for low notes.
    decimation = juce::jmax(1, juce::roundToInt(sampleRate / targetRate));
    analysisRate = sampleRate / decimation;

    polypitch::Config config;
    config.sampleRate = juce::roundToInt(analysisRate);
    config.fftSize = fftSize;
    config.hop = hopSize;
    engine = std::make_unique<polypitch::Engine>(config);
    trackEngine = std::make_unique<polypitch::DualEngine>(config.sampleRate, hopSize);
    jassert(engine->laneCount() == laneCount);

    tracker.reset();
    candidateTracker.reset();
    isFirstFrame = true;
    ring.assign((size_t) fftSize, 0.0f);
    window.assign((size_t) fftSize, 0.0f);
    ringWrite = ringFilled = sinceAnalysis = 0;
    analysedSamples = 0;

    antiAlias.clear();
    decimatorHistory.clear();
    decimatorWrite = decimatorCountdown = 0;
    if (decimation > 1)
    {
        const int length = 16 * decimation + 1;
        const double cutoff = 0.45 / decimation, middle = (length - 1) * 0.5;
        antiAlias.resize((size_t) length);
        double sum = 0.0;
        for (int i = 0; i < length; ++i)
        {
            const double x = i - middle;
            const double sinc = x == 0.0 ? 2.0 * cutoff : std::sin(juce::MathConstants<double>::twoPi * cutoff * x) / (juce::MathConstants<double>::pi * x);
            const double blackman = 0.42 - 0.5 * std::cos(juce::MathConstants<double>::twoPi * i / (length - 1)) + 0.08 * std::cos(2.0 * juce::MathConstants<double>::twoPi * i / (length - 1));
            antiAlias[(size_t) i] = (float) (sinc * blackman);
            sum += sinc * blackman;
        }
        for (auto& c : antiAlias)
            c = (float) (c / sum);
        decimatorHistory.assign((size_t) length, 0.0f);
    }

    const std::lock_guard<std::mutex> lock(frameLock);
    frames.clear();
    generation.fetch_add(1);
}

void PolyPitchAudioProcessor::prepareToPlay(double sampleRate, int)
{
    // A track is read at its own rate, whatever the project is playing at.
    if (timeline.load())
    {
        hostRate.store(sampleRate);
        return;
    }

    // Hosts prepare again for all sorts of reasons. Only a new rate invalidates the trace.
    if (! isThreadRunning() || engine == nullptr || sampleRate != hostRate.load())
    {
        stopThread(2000);
        fifo.reset();
        heardSamples.store(0);
        hostRate.store(sampleRate);
        configure(sampleRate);
        startThread(juce::Thread::Priority::low);
    }
}

void PolyPitchAudioProcessor::releaseResources()
{
}

void PolyPitchAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // The sound passes through untouched, whatever the analysis does.
    if (isNonRealtime() || timeline.load())
        return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = getTotalNumInputChannels();
    if (numSamples == 0 || numChannels == 0)
        return;

    float peak = 0.0f;
    for (int channel = 0; channel < numChannels; ++channel)
        peak = juce::jmax(peak, buffer.getMagnitude(channel, 0, numSamples));

    // A stopped transport still calls us with silence. That shouldn't scroll the trace away,
    // while sound on a stopped transport (an armed input, say) still should.
    bool playing = false;
    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
            playing = position->getIsPlaying() || position->getIsRecording();
    if (! playing && peak <= 0.0f)
        return;

    const auto scope = fifo.write(numSamples);
    const auto copyIn = [&](int fifoStart, int size, int sourceStart)
    {
        if (size <= 0)
            return;
        auto* out = fifoData.data() + fifoStart;
        juce::FloatVectorOperations::copy(out, buffer.getReadPointer(0, sourceStart), size);
        for (int channel = 1; channel < numChannels; ++channel)
            juce::FloatVectorOperations::add(out, buffer.getReadPointer(channel, sourceStart), size);
        if (numChannels > 1)
            juce::FloatVectorOperations::multiply(out, 1.0f / (float) numChannels, size);
    };
    copyIn(scope.startIndex1, scope.blockSize1, 0);
    copyIn(scope.startIndex2, scope.blockSize2, scope.blockSize1);

    const int written = scope.blockSize1 + scope.blockSize2;
    heardSamples.fetch_add(written);
    lastBlockMs.store(1000.0 * numSamples / hostRate.load());
    lastBlockAt.store(juce::Time::getMillisecondCounterHiRes());
}

double PolyPitchAudioProcessor::heardMs() const noexcept
{
    return 1000.0 * (double) heardSamples.load() / hostRate.load();
}

void PolyPitchAudioProcessor::run()
{
    std::vector<float> chunk(4096);
    while (! threadShouldExit())
    {
        if (timeline.load())
        {
            std::vector<float> next;
            {
                const std::lock_guard<std::mutex> lock(chunkLock);
                if (! chunks.empty())
                {
                    next = std::move(chunks.front());
                    chunks.pop_front();
                }
            }
            if (next.empty())
            {
                if (! refined.load() && analysedSamples2.load() >= totalSamples.load() && totalSamples.load() > 0)
                    refineFrames();
                else
                    wait(5);
            }
            else
                analyseMono(next.data(), (int) next.size());
            continue;
        }

        const int ready = fifo.getNumReady();
        if (ready == 0)
        {
            wait(5);
            continue;
        }
        const int count = juce::jmin(ready, (int) chunk.size());
        const auto scope = fifo.read(count);
        if (scope.blockSize1 > 0)
            juce::FloatVectorOperations::copy(chunk.data(), fifoData.data() + scope.startIndex1, scope.blockSize1);
        if (scope.blockSize2 > 0)
            juce::FloatVectorOperations::copy(chunk.data() + scope.blockSize1, fifoData.data() + scope.startIndex2, scope.blockSize2);
        analyseMono(chunk.data(), scope.blockSize1 + scope.blockSize2);
    }
}

void PolyPitchAudioProcessor::analyseMono(const float* samples, int count)
{
    for (int i = 0; i < count; ++i)
    {
        float sample = samples[i];
        if (decimation > 1)
        {
            const int length = (int) decimatorHistory.size();
            decimatorHistory[(size_t) decimatorWrite] = sample;
            decimatorWrite = (decimatorWrite + 1) % length;
            if (++decimatorCountdown < decimation)
                continue;
            decimatorCountdown = 0;
            float filtered = 0.0f;
            for (int k = 0; k < length; ++k)
                filtered += antiAlias[(size_t) k] * decimatorHistory[(size_t) ((decimatorWrite + k) % length)];
            sample = filtered;
        }

        ring[(size_t) ringWrite] = sample;
        ringWrite = (ringWrite + 1) % fftSize;
        ringFilled = juce::jmin(fftSize, ringFilled + 1);
        ++analysedSamples;
        analysedSamples2.store(analysedSamples, std::memory_order_relaxed);
        if (ringFilled < fftSize)
        {
            sinceAnalysis = hopSize - 1;
            continue;
        }
        if (++sinceAnalysis >= hopSize)
        {
            sinceAnalysis = 0;
            analyseWindow();
        }
    }
}

void PolyPitchAudioProcessor::analyseWindow()
{
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = ring[(size_t) ((ringWrite + i) % fftSize)];
    const bool onTimeline = timeline.load();
    if (onTimeline)
        trackEngine->analyse(window.data());
    else
        engine->analyse(window.data(), window.size());

    TraceFrame frame;
    // On a timeline a frame belongs at the middle of its window, so the trace lines up with the sound.
    frame.timeMs = onTimeline ? 1000.0 * (trackStart + (double) (analysedSamples - fftSize) / analysisRate)
                                   : 1000.0 * (double) analysedSamples / analysisRate;
    frame.level = onTimeline ? trackEngine->inputRms() : engine->inputRms();
    frame.field.resize((size_t) laneCount);
    const auto* evidence = onTimeline ? trackEngine->laneEvidence() : engine->laneEvidence();
    const auto* measured = onTimeline ? trackEngine->lanePitch() : engine->lanePitch();
    for (int i = 0; i < laneCount; ++i)
        frame.field[(size_t) i] = (juce::uint8) juce::jlimit(0, 255, (int) (evidence[i] * 255.0f + 0.5f));

    const std::lock_guard<std::mutex> lock(frameLock);
    frame.ridges = tracker.track(evidence, measured, laneCount, ! isFirstFrame, targets);
    if (onTimeline)
    {
        frame.alternative = candidateTracker.track(evidence, measured, laneCount, ! isFirstFrame, targets);
        polypitch::markPeaks(frame.alternative, trackEngine->peakMidi(), trackEngine->peakStrength(), trackEngine->peakCount());
    }
    isFirstFrame = false;
    frames.push_back(std::move(frame));
    while (! timeline.load() && ! frames.empty() && frames.front().timeMs < frames.back().timeMs - historyMs)
        frames.pop_front();
}

void PolyPitchAudioProcessor::refineFrames()
{
    const std::lock_guard<std::mutex> lock(frameLock);
    std::vector<std::vector<polypitch::Ridge>> cleaned, candidates;
    std::vector<double> levels;
    cleaned.reserve(frames.size());
    candidates.reserve(frames.size());
    levels.reserve(frames.size());
    for (auto& frame : frames)
    {
        cleaned.push_back(frame.ridges);
        candidates.push_back(std::move(frame.alternative));
        levels.push_back(frame.level);
    }
    lastStats = polypitch::refineTrace(cleaned, analysisRate / hopSize, targets, nullptr, &candidates, &levels);
    guess = polypitch::guessKey(cleaned);
    for (size_t i = 0; i < frames.size(); ++i)
        frames[i].alternative = std::move(cleaned[i]);
    showingClean = false;
    refined.store(true);
    if (viewSettings.clean)
        for (auto& frame : frames)
            frame.ridges.swap(frame.alternative);
    showingClean = viewSettings.clean;
    generation.fetch_add(1);
}

void PolyPitchAudioProcessor::setCleanTrace(bool shouldBeClean)
{
    const std::lock_guard<std::mutex> lock(frameLock);
    viewSettings.clean = shouldBeClean;
    if (! refined.load() || showingClean == shouldBeClean)
        return;
    showingClean = shouldBeClean;
    for (size_t i = 0; i < frames.size(); ++i)
    {
        frames[i].ridges.swap(frames[i].alternative);
        // The targets may have changed while this version was out of sight.
        polypitch::tintRidges(frames[i].ridges, i > 0 ? &frames[i - 1].ridges : nullptr, targets);
    }
    generation.fetch_add(1);
}

polypitch::KeyGuess PolyPitchAudioProcessor::keyGuess() const
{
    return refined.load() ? guess : polypitch::KeyGuess {};
}

polypitch::RefineStats PolyPitchAudioProcessor::refineStats() const
{
    return lastStats;
}

void PolyPitchAudioProcessor::setTrackHost(TrackHost* host)
{
    track = host;
    pollCountdown = 0;
    startTimer(250);
}

void PolyPitchAudioProcessor::analyseTrackOnce()
{
    if (track != nullptr && ! askedToAnalyse)
    {
        askedToAnalyse = true;
        startTrackAnalysis();
    }
}

bool PolyPitchAudioProcessor::startTrackAnalysis()
{
    if (track == nullptr)
        return false;

    const bool opened = track->open();
    const double start = opened ? track->startSeconds() : 0.0, end = opened ? track->endSeconds() : 0.0;
    changed = false;

    if (! opened || end - start < 0.05)
    {
        // Nothing on the track yet, so listen to whatever arrives instead.
        if (timeline.load())
        {
            stopThread(2000);
            fifo.reset();
            heardSamples.store(0);
            configure(hostRate.load());
            readingTrack = false;
            timeline.store(false);
            startThread(juce::Thread::Priority::low);
        }
        return false;
    }

    const bool wasTimeline = timeline.exchange(true);
    juce::ignoreUnused(wasTimeline);
    stopThread(2000);
    {
        const std::lock_guard<std::mutex> lock(chunkLock);
        chunks.clear();
    }
    trackStart = start;
    trackEnd = end;
    trackLength = (juce::int64) ((end - start) * targetRate);
    trackRead = 0;
    trackLabel = track->trackName();
    configure(targetRate);
    analysedSamples2.store(0);
    refined.store(false);
    // Half a window of silence each side, so the first and last frames centre on the track's ends.
    totalSamples.store(trackLength + fftSize);
    readingTrack = true;
    pushChunk(std::vector<float>((size_t) (fftSize / 2), 0.0f));
    startThread(juce::Thread::Priority::low);
    startTimer(10);
    return true;
}

void PolyPitchAudioProcessor::pushChunk(std::vector<float>&& samples)
{
    const std::lock_guard<std::mutex> lock(chunkLock);
    chunks.push_back(std::move(samples));
}

void PolyPitchAudioProcessor::readTrackChunk()
{
    {
        const std::lock_guard<std::mutex> lock(chunkLock);
        if ((int) chunks.size() >= maxQueuedChunks)
            return;
    }

    const int count = (int) juce::jmin((juce::int64) trackChunk, trackLength - trackRead);
    if (count > 0)
    {
        std::vector<double> stereo((size_t) count * 2);
        track->read(trackStart + (double) trackRead / targetRate, (int) targetRate, count, stereo.data());
        std::vector<float> mono((size_t) count);
        for (int i = 0; i < count; ++i)
            mono[(size_t) i] = (float) ((stereo[(size_t) i * 2] + stereo[(size_t) i * 2 + 1]) * 0.5);
        pushChunk(std::move(mono));
        trackRead += count;
    }

    if (trackRead >= trackLength)
    {
        pushChunk(std::vector<float>((size_t) (fftSize / 2), 0.0f));
        readingTrack = false;
        startTimer(250);
    }
}

void PolyPitchAudioProcessor::timerCallback()
{
    if (track == nullptr)
        return;
    if (readingTrack)
    {
        readTrackChunk();
        return;
    }

    // About once a second, see whether the track has moved on from the trace.
    if (--pollCountdown > 0)
        return;
    pollCountdown = 4;
    if (timeline.load())
    {
        if (track->audioChanged())
            changed = true;
    }
    else if (askedToAnalyse && ! changed && track->open() && track->endSeconds() - track->startSeconds() >= 0.05)
    {
        changed = true;
    }
}

double PolyPitchAudioProcessor::analysisProgress() const noexcept
{
    const auto total = totalSamples.load();
    return total > 0 ? juce::jlimit(0.0, 1.0, (double) analysedSamples2.load() / (double) total) : 0.0;
}

void PolyPitchAudioProcessor::setTargets(int mask)
{
    const std::lock_guard<std::mutex> lock(frameLock);
    if (mask == targets)
        return;
    targets = mask;
    // Old notes are judged against the new targets too.
    for (size_t i = 0; i < frames.size(); ++i)
        polypitch::tintRidges(frames[i].ridges, i > 0 ? &frames[i - 1].ridges : nullptr, targets);
    if (! frames.empty())
        tracker.adoptTints(frames.back().ridges);
    generation.fetch_add(1);
}

bool PolyPitchAudioProcessor::waitForAnalysis(int timeoutMs)
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (fifo.getNumReady() > 0)
    {
        if (juce::Time::getMillisecondCounter() > until)
            return false;
        juce::Thread::sleep(2);
    }
    juce::Thread::sleep(50);
    return true;
}

void PolyPitchAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement xml("POLYPITCH");
    xml.setAttribute("rootPc", viewSettings.rootPc);
    xml.setAttribute("scale", viewSettings.scaleId);
    xml.setAttribute("seconds", viewSettings.seconds);
    xml.setAttribute("span", viewSettings.span);
    xml.setAttribute("centre", viewSettings.centre);
    xml.setAttribute("follow", viewSettings.follow);
    xml.setAttribute("clean", viewSettings.clean);
    xml.setAttribute("width", viewSettings.width);
    xml.setAttribute("height", viewSettings.height);
    copyXmlToBinary(xml, destData);
}

void PolyPitchAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName("POLYPITCH"))
        return;

    viewSettings.rootPc = juce::jlimit(0, 11, xml->getIntAttribute("rootPc", 0));
    viewSettings.scaleId = xml->getStringAttribute("scale", "off");
    viewSettings.seconds = juce::jlimit(2.0, 7200.0, xml->getDoubleAttribute("seconds", 10.0));
    viewSettings.span = juce::jlimit(6.0, 60.0, xml->getDoubleAttribute("span", 30.0));
    viewSettings.centre = juce::jlimit(24.0, 108.0, xml->getDoubleAttribute("centre", 57.0));
    viewSettings.follow = xml->getBoolAttribute("follow", true);
    setCleanTrace(xml->getBoolAttribute("clean", true));
    viewSettings.width = juce::jlimit(480, 3000, xml->getIntAttribute("width", 960));
    viewSettings.height = juce::jlimit(280, 2000, xml->getIntAttribute("height", 540));

    const auto index = polypitch::scales::findIndex(viewSettings.scaleId.toRawUTF8());
    setTargets(polypitch::scales::targetMask(viewSettings.rootPc, polypitch::scales::all()[(size_t) index]));

    if (auto* editor = dynamic_cast<PolyPitchAudioProcessorEditor*>(getActiveEditor()))
        editor->settingsChanged();
}

juce::AudioProcessorEditor* PolyPitchAudioProcessor::createEditor()
{
    return new PolyPitchAudioProcessorEditor(*this);
}

#if ! POLYPITCH_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PolyPitchAudioProcessor();
}
#endif
