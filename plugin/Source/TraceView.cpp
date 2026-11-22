#include "TraceView.h"
#include "Scales.h"
#include "Style.h"

namespace
{
    constexpr int plotLeft = 62, plotRightGap = 4;
    constexpr double tileMs = 500.0, tileSpillPx = 24.0;
    constexpr double spanMin = 6.0, spanMax = 60.0, viewMin = 24.0, viewMax = 108.0;
    constexpr double secondsMin = 2.0, secondsMax = 60.0;
    constexpr double dotRadius = 7.0, dotSpacing = 1.2;
    constexpr int tintLevels = 12;
    constexpr double laneMin = 26.0, laneStep = 0.1;
    constexpr int followAge = 8;
    constexpr double followCore = 0.6, followTauMs = 250.0, ridgeReach = 0.45;
    // While playing, a third of the view is what has passed and the rest is what's coming.
    constexpr double aheadShare = 2.0 / 3.0;

    const juce::Colour background { 0xff070b0e };

    juce::Colour rgba(int r, int g, int b, float a) { return juce::Colour((juce::uint8) r, (juce::uint8) g, (juce::uint8) b, a); }

    double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

    bool followWant(const std::vector<polypitch::Ridge>& ridges, double lo, double hi, double span, double& goal)
    {
        // The camera answers only to a note that has lasted, and only moves when that note nears
        // the edge of the view, by just enough to give it room. Notes already on screen never move it.
        const polypitch::Ridge* top = nullptr;
        for (const auto& r : ridges)
            if (r.age >= followAge && r.core >= followCore && (top == nullptr || r.core > top->core))
                top = &r;
        if (top == nullptr)
            return false;
        const double margin = juce::jmax(2.0, span * 0.15), room = margin * 1.5, m = top->centre;
        if (m < lo + margin) { goal = m - room + span / 2; return true; }
        if (m > hi - margin) { goal = m + room - span / 2; return true; }
        return false;
    }

    const polypitch::Ridge* readoutRidge(const std::vector<polypitch::Ridge>& ridges, double held)
    {
        // Stay on the note being shown unless another is clearly stronger, so the readout doesn't
        // flicker between a note and its octave.
        const polypitch::Ridge* top = nullptr;
        const polypitch::Ridge* same = nullptr;
        for (const auto& r : ridges)
        {
            if (r.age < 2)
                continue;
            if (top == nullptr || r.core > top->core)
                top = &r;
            if (held >= 0.0 && std::abs(r.m - held) <= ridgeReach && (same == nullptr || r.core > same->core))
                same = &r;
        }
        return same != nullptr && top != nullptr && same->core >= top->core * 0.5 ? same : top;
    }
}

TraceView::TraceView(PolyPitchAudioProcessor& p) : processor(p)
{
    // A soft dot with a wide glow and a bright centre, sampled by squared distance.
    static constexpr int stops = 12;
    const auto shapeAt = [](double d)
    {
        return 0.42 * std::exp(-d * d / (2.0 * 2.4 * 2.4)) + 0.58 * std::exp(-d * d / (2.0 * 0.9 * 0.9));
    };
    for (size_t i = 0; i < dotShape.size(); ++i)
    {
        const double d = std::sqrt((double) i / (double) (dotShape.size() - 1)) * dotRadius;
        const double position = d / dotRadius * stops;
        const int stop = juce::jmin(stops - 1, (int) position);
        const double a0 = shapeAt((double) stop / stops * dotRadius);
        const double a1 = stop + 1 == stops ? 0.0 : shapeAt((double) (stop + 1) / stops * dotRadius);
        dotShape[i] = (float) (a0 + (a1 - a0) * (position - stop));
    }

    // Background evidence is the lane field itself, in neutral: room noise stays a faint
    // speckle, and a note's fringe widens where its pitch is less certain.
    for (size_t i = 0; i < fieldShape.size(); ++i)
    {
        const double e = (double) i / 255.0;
        fieldShape[i] = e < 0.03 ? 0.0f : (float) juce::jmin(1.0, 0.012 + 0.22 * std::pow(e, 2.5));
    }

    // Colour only says how close to a target: quiet blue-grey when far, vivid green when close.
    // Sharp or flat is shown by position.
    for (int level = 0; level <= tintLevels; ++level)
    {
        const float t = (float) level / tintLevels;
        tints[(size_t) level] = { (112.0f + (66.0f - 112.0f) * t) / 255.0f,
                                  (142.0f + (255.0f - 142.0f) * t) / 255.0f,
                                  (168.0f + (118.0f - 168.0f) * t) / 255.0f,
                                  0.46f + 0.54f * t };
    }

    setOpaque(true);
    setWantsKeyboardFocus(false);
    liveButton.setVisible(false);
    liveButton.onClick = [this] { goLive(); };
    addChildComponent(liveButton);
    analyseButton.onClick = [this] { processor.startTrackAnalysis(); needsDraw = true; };
    addChildComponent(analyseButton);
    keyButton.onClick = [this]
    {
        const auto guess = processor.keyGuess();
        if (guess.found && onUseKey != nullptr)
            onUseKey(guess.root, guess.minor);
        if (const double hz = suggestedReference(); hz > 0.0 && onUseTuning != nullptr)
            onUseTuning(hz);
    };
    addChildComponent(keyButton);
    applySettings();
}

void TraceView::applySettings()
{
    const auto& settings = processor.view();
    seconds = secondsExact = settings.seconds;
    span = settings.span;
    lo = settings.centre - span / 2;
    hi = settings.centre + span / 2;
    followOn = settings.follow;
    hasFollowGoal = false;
    clampView();

    rootPc = settings.rootPc;
    const auto& scale = polypitch::scales::all()[(size_t) polypitch::scales::findIndex(settings.scaleId.toRawUTF8())];
    flatNames = polypitch::scales::usesFlats(rootPc, scale);
    targetBits = polypitch::scales::targetMask(rootPc, scale);
    readoutShownAt = 0.0;
    needsDraw = true;
}

void TraceView::settingsChanged()
{
    applySettings();
    tiles.clear();
    tileKey.clear();
    repaint();
}

void TraceView::saveView()
{
    auto& settings = processor.view();
    settings.seconds = seconds;
    settings.span = span;
    settings.centre = (lo + hi) / 2;
    settings.follow = followOn;
}

void TraceView::resized()
{
    liveButton.setBounds(getWidth() - 136, 10, 126, 28);
    analyseButton.setBounds(getWidth() - 136 - 8 - 186, 10, 186, 28);
    needsDraw = true;
}

double TraceView::yFor(double midi) const noexcept
{
    return getHeight() - (midi - lo) / (hi - lo) * getHeight();
}

double TraceView::tileY(double midi) const noexcept
{
    return (tileHi - midi) / (tileHi - tileLo) * tileCssHeight;
}

void TraceView::clampView()
{
    span = juce::jmin(span, viewMax - viewMin);
    const double centre = juce::jlimit(viewMin + span / 2, viewMax - span / 2, (lo + hi) / 2);
    lo = centre - span / 2;
    hi = centre + span / 2;
}

bool TraceView::audioFlowing() const
{
    if (processor.isTimeline())
        return wasPlaying;
    return nowMs() - processor.lastHeardAt() < juce::jmax(300.0, processor.blockMs() * 2.0);
}

double TraceView::liveEdge() const
{
    // The audio clock runs smoothly while blocks arrive in steps; a little lag hides the steps.
    const double elapsed = juce::jlimit(0.0, processor.blockMs(), nowMs() - processor.lastHeardAt());
    const double edge = processor.heardMs() + elapsed - 30.0;
    if (edge > lastEdge || processor.heardMs() < lastEdge - 1000.0)
        lastEdge = edge;
    return lastEdge;
}

double TraceView::displayTime() const
{
    if (inReview)
        return reviewEnd;
    return processor.isTimeline() ? viewEnd : liveEdge();
}

size_t TraceView::firstFrameAfter(double timeMs) const
{
    const auto& frames = processor.traceFrames();
    size_t a = 0, b = frames.size();
    while (a < b)
    {
        const size_t m = (a + b) / 2;
        if (frames[m].timeMs > timeMs)
            b = m;
        else
            a = m + 1;
    }
    return a;
}

void TraceView::setManualView()
{
    if (followOn)
        setFollow(false);
}

void TraceView::setFollow(bool shouldFollow)
{
    followOn = shouldFollow;
    hasFollowGoal = false;
    saveView();
    if (onFollowChanged != nullptr)
        onFollowChanged();
}

void TraceView::panPitch(double semitones)
{
    lo += semitones;
    hi += semitones;
    clampView();
    setManualView();
    saveView();
    needsDraw = true;
}

void TraceView::zoomPitchAt(double factor, float anchorY)
{
    const double fraction = 1.0 - anchorY / getHeight(), anchor = lo + fraction * span;
    span = juce::jlimit(spanMin, spanMax, span * factor);
    lo = anchor - fraction * span;
    hi = lo + span;
    clampView();
    saveView();
    needsDraw = true;
}

void TraceView::zoomPitch(double factor)
{
    zoomPitchAt(factor, getHeight() * 0.5f);
}

void TraceView::zoomTime(double factor)
{
    secondsExact = juce::jlimit(secondsMin, secondsLimit(), secondsExact * factor);
    seconds = std::round(secondsExact);
    saveView();
    needsDraw = true;
}

double TraceView::secondsLimit() const
{
    // On a timeline you can pull right back to see the whole track.
    return processor.isTimeline() ? juce::jmax(secondsMax, std::ceil(processor.trackSeconds())) : secondsMax;
}

void TraceView::fitPitch()
{
    const double now = displayTime();
    fitBetween(now - seconds * 1000.0, now);
}

void TraceView::fitBetween(double fromMs, double toMs)
{
    // Frame whatever notes are in view, ignoring stray extremes.
    std::vector<double> found;
    {
        const std::lock_guard<std::mutex> lock(processor.traceLock());
        const auto& frames = processor.traceFrames();
        const size_t end = firstFrameAfter(toMs);
        for (size_t i = firstFrameAfter(fromMs); i < end; ++i)
            for (const auto& r : frames[i].ridges)
                if (r.core >= 0.5 && r.age >= polypitch::RIDGE_AGE_FULL)
                    found.push_back(r.m);
    }
    if (found.empty())
        return;
    std::sort(found.begin(), found.end());
    const double a = found[(size_t) ((double) found.size() * 0.02)], b = found[(size_t) ((double) found.size() * 0.98)];
    span = juce::jlimit(spanMin, spanMax, b - a + 5.0);
    lo = (a + b) / 2 - span / 2;
    hi = lo + span;
    clampView();
    saveView();
    needsDraw = true;
}

void TraceView::setReview(double timeMs)
{
    {
        const std::lock_guard<std::mutex> lock(processor.traceLock());
        const auto& frames = processor.traceFrames();
        if (frames.empty())
            return;
        if (processor.isTimeline())
        {
            // Any move away from the playhead holds until you go back to it, or press play.
            inReview = true;
            reviewEnd = juce::jlimit(frames.front().timeMs, frames.back().timeMs + seconds * 1000.0, timeMs);
            hasFollowGoal = false;
        }
        else
        {
            const double edge = liveEdge();
            inReview = timeMs < edge - 1.0;
            if (inReview)
            {
                reviewEnd = juce::jmax(timeMs, frames.front().timeMs);
                hasFollowGoal = false;
            }
        }
    }
    updateLive();
    needsDraw = true;
}

void TraceView::updateLive()
{
    liveButton.setVisible(inReview);
}

void TraceView::goLive()
{
    inReview = false;
    updateLive();
    needsDraw = true;
}

void TraceView::lookBack(double milliseconds)
{
    setReview(displayTime() - milliseconds);
}

double TraceView::timeAtX(float x) const
{
    const int usable = getWidth() - plotLeft - plotRightGap;
    return displayTime() - seconds * 1000.0 + (x - plotLeft) / usable * seconds * 1000.0;
}

void TraceView::seekToX(float x)
{
    if (auto* host = processor.trackHost(); host != nullptr && processor.isTimeline() && x >= plotLeft)
        host->seek(juce::jmax(0.0, timeAtX(x) / 1000.0));
}

void TraceView::tickTimeline(double now)
{
    auto* host = processor.trackHost();
    const bool playing = host->isPlaying();
    const double reported = host->positionSeconds() * 1000.0;

    // The host reports in steps, so run on the clock between reports.
    if (reported != hostMs)
    {
        hostMs = reported;
        hostMsAt = now;
    }
    double estimate = reported;
    if (playing)
    {
        estimate += juce::jmin(120.0, now - hostMsAt);
        // Small steps back are only jitter. A large one is a loop or a seek.
        if (estimate < playheadMs && playheadMs - estimate < 250.0)
            estimate = playheadMs;
    }
    if (estimate != playheadMs)
        needsDraw = true;
    playheadMs = estimate;

    if (playing && ! wasPlaying && inReview)
    {
        inReview = false;
        updateLive();
    }
    wasPlaying = playing;

    if (! inReview)
    {
        const bool outside = playheadMs < viewEnd - seconds * 1000.0 || playheadMs > viewEnd;
        if (playing || outside || ! viewPlaced)
            viewEnd = playheadMs + seconds * 1000.0 * aheadShare;
        viewPlaced = true;
    }

    const bool analysing = processor.isAnalysing();
    if (wasAnalysing && ! analysing)
    {
        // The whole track has arrived. Frame it, unless you've already chosen a view.
        if (followOn)
            fitBetween(-1.0e12, 1.0e12);
        needsDraw = true;
    }
    wasAnalysing = analysing;
}

double TraceView::suggestedReference() const
{
    // Only worth saying when the notes agree with each other and it's more than a hair out.
    const auto guess = processor.keyGuess();
    if (! guess.found || guess.tuningFocus < 0.2 || std::abs(guess.tuningCents) < 4.0 || std::abs(guess.tuningCents) > 30.0)
        return 0.0;
    const double hz = (double) juce::roundToInt(440.0 * std::pow(2.0, guess.tuningCents / 1200.0));
    // Nothing to suggest once it's what you're using.
    return std::abs(hz - processor.view().referenceHz) < 0.5 ? 0.0 : hz;
}

void TraceView::updateKeyButton()
{
    // Offered until you've chosen a scale, and not after.
    const auto guess = processor.keyGuess();
    const bool offer = guess.found && processor.view().scaleId == "off";
    if (offer)
    {
        const auto keyName = [](int root, bool minor)
        {
            const auto& scale = polypitch::scales::all()[minor ? 2 : 1];
            return juce::String::fromUTF8(polypitch::scales::noteName(root, polypitch::scales::usesFlats(root, scale))) + (minor ? " minor" : " major");
        };
        auto text = "Sounds like " + keyName(guess.root, guess.minor) + " (or " + keyName(guess.nextRoot, guess.nextMinor) + "). Use it";
        if (const double hz = suggestedReference(); hz > 0.0)
            text = "Tuned to A=" + juce::String((int) hz) + ". " + text.replace("Use it", "Use both");
        if (text != keyButtonFor)
        {
            keyButtonFor = text;
            keyButton.setButtonText(text);
            keyButton.setBounds(plotLeft + 10, 10, juce::jmin(text.length() * 7 + 28, getWidth() - plotLeft - 20), 28);
        }
    }
    if (keyButton.isVisible() != offer)
        keyButton.setVisible(offer);
}

void TraceView::tick()
{
    const double now = nowMs();
    const double dt = juce::jmin(100.0, now - lastTickAt);
    lastTickAt = now;

    const bool onTimeline = processor.isTimeline();
    if (onTimeline != wasTimeline)
    {
        wasTimeline = onTimeline;
        inReview = false;
        viewPlaced = false;
        updateLive();
        needsDraw = true;
    }
    if (onTimeline)
        tickTimeline(now);
    if (onTransport != nullptr)
        onTransport(onTimeline, onTimeline && wasPlaying);

    const bool canAnalyse = processor.trackChanged() && ! processor.isAnalysing();
    if (canAnalyse != analyseButton.isVisible())
    {
        analyseButton.setButtonText(onTimeline ? "Track changed: analyse again" : "Analyse this track");
        analyseButton.setVisible(canAnalyse);
    }
    liveButton.setButtonText(onTimeline ? "Back to playhead" : "Back to live");
    updateKeyButton();

    // Other tracks changing, while they analyse, means redrawing their lines.
    double seen = 0.0;
    for (auto* other : processor.otherInstances())
        seen += other->traceGeneration() * 1000.0 + other->analysisProgress();
    if (seen != othersSeen)
    {
        othersSeen = seen;
        needsDraw = true;
    }

    {
        const std::lock_guard<std::mutex> lock(processor.traceLock());
        const auto& frames = processor.traceFrames();
        const double newest = frames.empty() ? -1.0 : frames.back().timeMs;
        if (newest != seenNewest)
        {
            seenNewest = newest;
            if (onTimeline)
                needsDraw = true;
        }

        // Live, the newest frame is the note. On a timeline, it's the one under the playhead.
        const TraceFrame* current = frames.empty() ? nullptr : &frames.back();
        if (onTimeline && current != nullptr)
        {
            const size_t after = firstFrameAfter(playheadMs);
            current = after > 0 && playheadMs - frames[after - 1].timeMs < 100.0 ? &frames[after - 1] : nullptr;
        }
        static const std::vector<polypitch::Ridge> noRidges;
        const double currentTime = current != nullptr ? current->timeMs : (onTimeline ? -2.0 : -1.0);
        if (currentTime != seenFrameTime)
        {
            seenFrameTime = currentTime;
            // Outside the track there won't be another frame to clear the readout.
            if (current == nullptr)
                readoutShownAt = 0.0;
            if (current != nullptr || onTimeline)
            {
                const auto& ridges = current != nullptr ? current->ridges : noRidges;
                if (followOn && ! inReview)
                {
                    double goal = 0.0;
                    if (followWant(ridges, lo, hi, span, goal))
                    {
                        followGoal = goal;
                        hasFollowGoal = true;
                    }
                }
                if (const auto* shown = readoutRidge(ridges, readoutMidi))
                {
                    readoutMidi = shown->centre;
                    if (now - readoutShownAt > 120.0)
                    {
                        readoutShownAt = now;
                        if (! inReview && onReadout != nullptr)
                            onReadout(readoutMidi, shown->inTune);
                    }
                }
                else if (now - readoutShownAt > 300.0)
                {
                    readoutShownAt = now;
                    readoutMidi = -1.0;
                    if (! inReview && onReadout != nullptr)
                        onReadout(-1.0, false);
                }
            }
        }
    }

    if (hasFollowGoal && followOn && ! inReview)
    {
        // Glide rather than cut, so the eye can stay with a note while the view moves.
        const double centre = (lo + hi) / 2;
        const double clamped = juce::jlimit(viewMin + span / 2, viewMax - span / 2, followGoal);
        double next = centre + (clamped - centre) * (1.0 - std::exp(-dt / followTauMs));
        if (std::abs(clamped - next) < 0.01)
        {
            next = clamped;
            hasFollowGoal = false;
            saveView();
        }
        lo = next - span / 2;
        hi = next + span / 2;
        needsDraw = true;
    }

    if (needsDraw || (audioFlowing() && ! inReview))
    {
        needsDraw = false;
        repaint();
    }
}

juce::String TraceView::noteLabel(int midi) const
{
    return juce::String::fromUTF8(polypitch::scales::noteName(midi, flatNames)) + juce::String((int) std::floor(midi / 12.0) - 1);
}

void TraceView::paintGrid(juce::Graphics& g) const
{
    g.fillAll(background);
    const int width = getWidth();
    const double step = getHeight() / span, zone = juce::jmax(1.0, step * 0.15);
    const bool hasTargets = targetBits != polypitch::ANY_NOTE;
    // With a reference other than A=440, the note lines move to where its notes are.
    const double offset = processor.pitchOffset();

    for (int m = (int) std::ceil(lo - offset); m <= hi - offset; ++m)
    {
        const int pc = ((m % 12) + 12) % 12;
        const double y = yFor(m + offset);
        const auto labelArea = juce::Rectangle<float>(10.0f, (float) y - 9.0f, 50.0f, 18.0f);
        if (hasTargets)
        {
            // Each target shows its in-tune zone, the same 15 cents where the trace starts turning green.
            const bool target = (targetBits >> pc) & 1, root = pc == rootPc;
            if (target)
            {
                // The zone grows as you zoom in, so it fades to stay behind the trace.
                const float fade = (float) juce::jmin(1.0, 2.5 / zone);
                g.setColour(rgba(150, 178, 195, (root ? 0.11f : 0.055f) * fade));
                g.fillRect((float) plotLeft, (float) (y - zone), (float) (width - plotLeft), (float) (zone * 2));
            }
            g.setColour(juce::Colour(root ? 0xff3d6b52 : target ? 0xff2a3a43 : 0xff121a20));
            g.fillRect(plotLeft, (int) std::round(y), width - plotLeft, 1);
            if (target)
            {
                g.setFont(polypitch::style::font(root ? 13.0f : 12.0f, root));
                g.setColour(juce::Colour(root ? 0xff4cd97b : 0xffc8d4da));
                g.drawText(noteLabel(m), labelArea, juce::Justification::centredLeft, false);
            }
        }
        else
        {
            const bool isC = pc == 0, natural = pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
            g.setColour(juce::Colour(isC ? 0xff35454e : 0xff182128));
            g.fillRect(plotLeft, (int) std::round(y), width - plotLeft, 1);
            if (span <= 24.0 || natural)
            {
                g.setFont(polypitch::style::font(12.0f));
                g.setColour(juce::Colour(isC ? 0xffc8d4da : natural ? 0xff8a9aa3 : 0xff6b7b84));
                g.drawText(noteLabel(m), labelArea, juce::Justification::centredLeft, false);
            }
        }
    }
}

void TraceView::paintDot(Tile& tile, double x, double midi, double alpha, int level, float scale)
{
    const auto& tint = tints[(size_t) level];
    const float strength = (float) juce::jmin(1.0, alpha) * tint.gain;
    const double cx = x * scale, cy = tileY(midi) * scale, radius = dotRadius * scale;
    const int height = tile.image.getHeight();
    const int x0 = juce::jmax(0, (int) std::floor(cx - radius)), x1 = juce::jmin(tile.width - 1, (int) std::ceil(cx + radius));
    const int y0 = juce::jmax(0, (int) std::floor(cy - radius)), y1 = juce::jmin(height - 1, (int) std::ceil(cy + radius));
    if (x0 > x1 || y0 > y1)
        return;
    const double toShape = (double) (dotShape.size() - 1) / (radius * radius);
    for (int py = y0; py <= y1; ++py)
    {
        const double dy = py + 0.5 - cy;
        float* out = tile.light.data() + ((size_t) py * (size_t) tile.width + (size_t) x0) * 4;
        for (int px = x0; px <= x1; ++px, out += 4)
        {
            const double dx = px + 0.5 - cx, d2 = dx * dx + dy * dy;
            const auto index = (size_t) (d2 * toShape);
            if (index >= dotShape.size())
                continue;
            const float a = dotShape[index] * strength;
            out[0] += tint.red * a;
            out[1] += tint.green * a;
            out[2] += tint.blue * a;
            out[3] += a;
        }
    }
    tile.dirtyFrom = juce::jmin(tile.dirtyFrom, x0);
    tile.dirtyTo = juce::jmax(tile.dirtyTo, x1 + 1);
}

void TraceView::paintField(Tile& tile, const TraceFrame& frame, double x1, double frameDx, float scale)
{
    const int laneCount = (int) frame.field.size();
    const int first = juce::jmax(0, (int) std::ceil((tileLo - 0.5 - laneMin) / laneStep));
    const int last = juce::jmin(laneCount - 1, (int) std::floor((tileHi + 0.5 - laneMin) / laneStep));
    if (last < first)
        return;

    const double left = (x1 - frameDx / 2) * scale, right = left + juce::jmax(1.0, frameDx) * scale;
    const double top = tileY(laneMin + last * laneStep) * scale, bottom = tileY(laneMin + first * laneStep) * scale;
    const int height = tile.image.getHeight(), rows = last - first + 1;
    const double rowHeight = (bottom - top) / rows;
    const int col0 = juce::jmax(0, (int) std::floor(left)), col1 = juce::jmin(tile.width, (int) std::ceil(right));
    if (col0 >= col1 || rowHeight <= 0.0)
        return;

    // Once cleaned up, the background only shows as the fringe around the notes that are left.
    std::array<bool, 741> nearNote;
    const bool masked = processor.isShowingClean() && laneCount <= (int) nearNote.size();
    if (masked)
    {
        nearNote.fill(false);
        for (const auto& r : frame.ridges)
        {
            const int centreLane = (int) std::round((r.m - laneMin) / laneStep);
            for (int lane = juce::jmax(0, centreLane - 6); lane <= juce::jmin(laneCount - 1, centreLane + 6); ++lane)
                nearNote[(size_t) lane] = true;
        }
    }

    const float red = 150.0f / 255.0f, green = 175.0f / 255.0f, blue = 190.0f / 255.0f;
    const int y0 = juce::jmax(0, (int) std::floor(top)), y1 = juce::jmin(height, (int) std::ceil(bottom));
    for (int py = y0; py < y1; ++py)
    {
        // Scaling one column of lanes up smooths it, so lanes don't show as blocks.
        const double position = juce::jlimit(0.0, (double) (rows - 1), (py + 0.5 - top) / rowHeight - 0.5);
        const int row = (int) position;
        const float blend = (float) (position - row);
        const int lane0 = last - row, lane1 = last - juce::jmin(rows - 1, row + 1);
        const float a0 = masked && ! nearNote[(size_t) lane0] ? 0.0f : fieldShape[frame.field[(size_t) lane0]];
        const float a1 = masked && ! nearNote[(size_t) lane1] ? 0.0f : fieldShape[frame.field[(size_t) lane1]];
        const float alpha = a0 + (a1 - a0) * blend;
        if (alpha <= 0.0f)
            continue;
        for (int px = col0; px < col1; ++px)
        {
            const float coverage = (float) (juce::jmin(right, px + 1.0) - juce::jmax(left, (double) px));
            if (coverage <= 0.0f)
                continue;
            const float a = alpha * coverage;
            float* out = tile.light.data() + ((size_t) py * (size_t) tile.width + (size_t) px) * 4;
            out[0] += red * a;
            out[1] += green * a;
            out[2] += blue * a;
            out[3] += a;
        }
    }
    tile.dirtyFrom = juce::jmin(tile.dirtyFrom, col0);
    tile.dirtyTo = juce::jmax(tile.dirtyTo, col1);
}

void TraceView::paintFrame(Tile& tile, const TraceFrame& frame, const TraceFrame* previous, double pixelsPerMs, float scale)
{
    // Zoom changes temporal coverage, not pitch width.
    const auto cssX = [&](double t) { return (t * pixelsPerMs - tile.column) / scale; };
    const double x1 = cssX(frame.timeMs), x0 = previous != nullptr ? cssX(previous->timeMs) : x1;
    const double frameDx = std::abs(x1 - x0);
    paintField(tile, frame, x1, frameDx, scale);

    // Each ridge deposits soft dots at its measured pitch. There are no lines: overlapping
    // dots form the trace, spaced evenly so brightness doesn't depend on speed.
    for (const auto& r : frame.ridges)
    {
        if (r.m < tileLo - 0.5 || r.m > tileHi + 0.5)
            continue;
        // Position shows the real pitch, wobble and all. Colour follows the note's centre, so
        // vibrato around an in-tune pitch stays green.
        const double alpha = polypitch::ridgeAlpha(r);
        const int level = juce::jlimit(0, tintLevels, (int) std::round(r.tint * tintLevels));
        if (! r.hasPrev)
        {
            // A ridge with no past: noise stays one faint dot, while a note's onset fills its frame.
            if (r.core < polypitch::RIDGE_STRONG)
            {
                paintDot(tile, x1, r.m, alpha, level, scale);
                continue;
            }
            const double width = juce::jmin(frameDx, 6.0);
            const int n = juce::jmax(1, (int) std::ceil(width / dotSpacing));
            for (int k = 0; k < n; ++k)
                paintDot(tile, x1 - width * (k + 0.5) / n, r.m, alpha * juce::jmin(1.0, width / n / dotSpacing), level, scale);
            continue;
        }

        // A curve through the midpoints between frames has no corners, and needs no future frames.
        const double sm = r.hasPrev2 ? (r.prev2 + r.prev) / 2 : r.prev, sx = r.hasPrev2 ? x0 - frameDx / 2 : x0;
        const double em = (r.prev + r.m) / 2, ex = x0 + frameDx / 2;
        const double len = std::hypot(x0 - sx, tileY(r.prev) - tileY(sm)) + std::hypot(ex - x0, tileY(em) - tileY(r.prev));
        const int n = juce::jmax(1, (int) std::ceil(len / dotSpacing));
        const double each = alpha * juce::jmin(1.0, len / n / dotSpacing);
        for (int k = 0; k < n; ++k)
        {
            const double u = (k + 0.5) / n, v = 1.0 - u;
            paintDot(tile, v * v * sx + 2 * v * u * x0 + u * u * ex, v * v * sm + 2 * v * u * r.prev + u * u * em, each, level, scale);
        }
    }
}

void TraceView::appendFrames(Tile& tile, double pixelsPerMs, float scale)
{
    // Progress is tracked by time: frame positions shift once old history is trimmed.
    const auto& frames = processor.traceFrames();
    for (size_t i = firstFrameAfter(tile.lastTime); i < frames.size(); ++i)
    {
        const auto& frame = frames[i];
        if (frame.timeMs >= tile.end + tile.margin)
            break;
        paintFrame(tile, frame, i > 0 ? &frames[i - 1] : nullptr, pixelsPerMs, scale);
        tile.lastTime = frame.timeMs;
    }
}

void TraceView::publish(Tile& tile)
{
    if (tile.dirtyFrom >= tile.dirtyTo)
        return;
    const juce::Image::BitmapData pixels(tile.image, juce::Image::BitmapData::readWrite);
    const int height = tile.image.getHeight();
    for (int y = 0; y < height; ++y)
    {
        auto* out = pixels.getLinePointer(y) + tile.dirtyFrom * pixels.pixelStride;
        const float* in = tile.light.data() + ((size_t) y * (size_t) tile.width + (size_t) tile.dirtyFrom) * 4;
        for (int x = tile.dirtyFrom; x < tile.dirtyTo; ++x, in += 4, out += pixels.pixelStride)
        {
            // Light only ever adds, and the pixels are premultiplied, so colour can't pass alpha.
            const float alpha = juce::jmin(1.0f, in[3]);
            const auto toByte = [&](float v) { return (juce::uint8) (juce::jmin(v, alpha) * 255.0f + 0.5f); };
            reinterpret_cast<juce::PixelARGB*>(out)->setARGB(toByte(alpha), toByte(in[0]), toByte(in[1]), toByte(in[2]));
        }
    }
    tile.dirtyFrom = tile.width;
    tile.dirtyTo = 0;
}

TraceView::Tile& TraceView::ensureTile(int index, double pixelsPerMs, float scale)
{
    auto found = tiles.find(index);
    if (found == tiles.end())
    {
        Tile tile;
        tile.start = index * tileMs;
        tile.end = tile.start + tileMs;
        tile.column = (int) std::floor(tile.start * pixelsPerMs);
        tile.width = juce::jmax(1, (int) std::floor(tile.end * pixelsPerMs) - tile.column);
        // Soft deposits reach across tile edges, so each tile also paints neighbouring
        // frames close enough to spill into it.
        tile.margin = tileSpillPx * scale / pixelsPerMs;
        tile.lastTime = tile.start - tile.margin;
        const int height = juce::jmax(1, (int) std::ceil(tileCssHeight * scale));
        tile.image = juce::Image(juce::Image::ARGB, tile.width, height, true);
        tile.light.assign((size_t) tile.width * (size_t) height * 4, 0.0f);
        tile.dirtyFrom = tile.width;
        tile.dirtyTo = 0;
        found = tiles.emplace(index, std::move(tile)).first;
    }

    auto& tile = found->second;
    // A tile stays open until every frame that can touch it has been painted.
    if (! tile.complete)
    {
        appendFrames(tile, pixelsPerMs, scale);
        publish(tile);
        const auto& frames = processor.traceFrames();
        if (! frames.empty() && frames.back().timeMs >= tile.end + tile.margin)
        {
            tile.complete = true;
            tile.light = {};
        }
    }
    return tile;
}

void TraceView::paintTiles(juce::Graphics& g, double now, float scale)
{
    const auto& frames = processor.traceFrames();
    if (frames.empty())
    {
        tiles.clear();
        return;
    }

    const double t0 = now - seconds * 1000.0;
    const int usable = getWidth() - plotLeft - plotRightGap;
    const double pixelsPerMs = usable * (double) scale / (seconds * 1000.0);

    // Tiles reach half a view beyond each side, so scrolling pitch only moves them.
    if (lo < tileLo || hi > tileHi || std::abs(tileHi - tileLo - span * 2) > 1e-3 || tileCssHeight != getHeight() * 2)
    {
        const double centre = (lo + hi) / 2;
        tileLo = centre - span;
        tileHi = centre + span;
        tileCssHeight = getHeight() * 2;
    }

    const auto key = juce::String(getWidth()) + "|" + juce::String(scale) + "|" + juce::String(seconds) + "|" + juce::String(span, 4) + "|" + juce::String(tileLo, 4) + "|" + juce::String(tileCssHeight);
    const auto generation = processor.traceGeneration();
    if (key != tileKey || generation != tileGeneration)
    {
        tiles.clear();
        tileKey = key;
        tileGeneration = generation;
    }

    const int firstTile = (int) std::floor(t0 / tileMs), lastTile = (int) std::floor(now / tileMs);
    for (auto it = tiles.begin(); it != tiles.end();)
    {
        if (it->first < firstTile - 2 || it->first > lastTile + 1)
            it = tiles.erase(it);
        else
            ++it;
    }

    // Everything lands on whole pixels, so tiles meet without seams.
    const double offset = std::floor(t0 * pixelsPerMs);
    const int top = (int) std::round(yFor(tileHi) * scale);
    const int left = (int) std::round(plotLeft * scale);

    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(plotLeft, 0, usable, getHeight());
    g.addTransform(juce::AffineTransform::scale(1.0f / scale));
    g.setOpacity(1.0f);

    const double earliest = frames.front().timeMs;
    for (int index = firstTile; index <= lastTile; ++index)
    {
        if ((index + 1) * tileMs + 200.0 < earliest || index * tileMs - 200.0 > frames.back().timeMs)
            continue;
        const auto& tile = ensureTile(index, pixelsPerMs, scale);
        g.drawImageAt(tile.image, left + (int) (tile.column - offset), top);
    }
}

void TraceView::paintTimeAxis(juce::Graphics& g, double now) const
{
    // Seconds behind the newest sound, so a place in history can be named and found again.
    const auto& frames = processor.traceFrames();
    if (frames.empty())
        return;
    const double newest = frames.back().timeMs, sec = seconds;
    const int usable = getWidth() - plotLeft - plotRightGap;
    double step = 600.0;
    for (double s : { 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0 })
        if (sec / s <= 10.0)
        {
            step = s;
            break;
        }

    g.setFont(polypitch::style::font(11.0f));
    g.setColour(juce::Colour(0xff778991));

    if (processor.isTimeline() && ! barLines.empty())
    {
        // Bar numbers, as on Reaper's ruler, spaced out when they'd crowd.
        double barGap = 1.0e9, lastBar = -1.0;
        for (const auto& line : barLines)
            if (line.barStart)
            {
                const double x = plotLeft + (line.seconds * 1000.0 - (now - sec * 1000.0)) / (sec * 1000.0) * usable;
                if (lastBar >= 0.0)
                    barGap = juce::jmin(barGap, x - lastBar);
                lastBar = x;
            }
        int every = 1;
        while (barGap * every < 36.0 && every < 4096)
            every *= 2;
        for (const auto& line : barLines)
        {
            if (! line.barStart || (line.bar - 1) % every != 0)
                continue;
            const double x = plotLeft + (line.seconds * 1000.0 - (now - sec * 1000.0)) / (sec * 1000.0) * usable;
            if (x < 70.0 || x > getWidth() - 20.0)
                continue;
            g.fillRect((float) x, (float) getHeight() - 4.0f, 1.0f, 4.0f);
            g.drawText(juce::String(line.bar), juce::Rectangle<float>((float) x - 30.0f, (float) getHeight() - 20.0f, 60.0f, 15.0f), juce::Justification::centred, false);
        }
        return;
    }

    if (processor.isTimeline())
    {
        // Project time, matching Reaper's ruler.
        const double from = now - sec * 1000.0;
        for (int k = (int) std::ceil(juce::jmax(0.0, from) / 1000.0 / step); k * step * 1000.0 <= now; ++k)
        {
            const double x = plotLeft + (k * step * 1000.0 - from) / (sec * 1000.0) * usable;
            if (x < 90.0 || x > getWidth() - 20.0)
                continue;
            const int total = (int) (k * step);
            g.fillRect((float) x, (float) getHeight() - 4.0f, 1.0f, 4.0f);
            g.drawText(juce::String(total / 60) + ":" + juce::String(total % 60).paddedLeft('0', 2),
                       juce::Rectangle<float>((float) x - 30.0f, (float) getHeight() - 20.0f, 60.0f, 15.0f), juce::Justification::centred, false);
        }
        return;
    }

    for (int k = (int) std::ceil((newest - now) / 1000.0 / step); k * step <= (newest - now + sec * 1000.0) / 1000.0; ++k)
    {
        const double x = plotLeft + (newest - k * step * 1000.0 - (now - sec * 1000.0)) / (sec * 1000.0) * usable;
        if (x < 90.0 || x > getWidth() - 20.0)
            continue;
        g.fillRect((float) x, (float) getHeight() - 4.0f, 1.0f, 4.0f);
        const auto text = k != 0 ? juce::String::fromUTF8("\xe2\x88\x92") + juce::String((int) (k * step)) + " s" : juce::String("now");
        g.drawText(text, juce::Rectangle<float>((float) x - 30.0f, (float) getHeight() - 20.0f, 60.0f, 15.0f), juce::Justification::centred, false);
    }
}

void TraceView::paintCursor(juce::Graphics& g, double now)
{
    // Away from live, pointing at a moment reads out the note there.
    if (! hasCursor || cursor.x < plotLeft || (! inReview && audioFlowing()))
        return;
    const auto& frames = processor.traceFrames();
    if (frames.empty())
        return;
    const int usable = getWidth() - plotLeft - plotRightGap;
    const double t = now - seconds * 1000.0 + (cursor.x - plotLeft) / usable * seconds * 1000.0;
    if (processor.isTimeline() && (t < frames.front().timeMs - 100.0 || t > frames.back().timeMs + 100.0))
        return;
    const auto& frame = frames[juce::jmin(frames.size() - 1, firstFrameAfter(t))];
    g.setColour(rgba(200, 212, 218, 0.35f));
    g.fillRect(cursor.x, 0.0f, 1.0f, (float) getHeight());

    if (onReadout == nullptr)
        return;
    if (std::abs(frame.timeMs - t) > 100.0)
    {
        onReadout(-1.0, false);
        return;
    }
    const double at = lo + (1.0 - cursor.y / getHeight()) * span;
    const polypitch::Ridge* best = nullptr;
    for (const auto& r : frame.ridges)
        if (r.core >= 0.1 && (best == nullptr || std::abs(r.m - at) < std::abs(best->m - at)))
            best = &r;
    onReadout(best != nullptr ? best->m : -1.0, best != nullptr && best->inTune);
}

void TraceView::paintPlayhead(juce::Graphics& g, double now) const
{
    const int usable = getWidth() - plotLeft - plotRightGap;
    const double x = plotLeft + (playheadMs - (now - seconds * 1000.0)) / (seconds * 1000.0) * usable;
    if (x < plotLeft || x > getWidth())
        return;
    g.setColour(juce::Colour(0xffe9f0f3).withAlpha(0.75f));
    g.fillRect((float) x - 0.5f, 0.0f, 1.5f, (float) getHeight());
}

void TraceView::paintStatus(juce::Graphics& g) const
{
    if (! processor.isAnalysing())
        return;
    const auto text = "Analysing " + processor.trackTitle() + "   " + juce::String(juce::roundToInt(processor.analysisProgress() * 100.0)) + "%";
    const auto area = juce::Rectangle<float>((float) plotLeft + 10.0f, 10.0f, 300.0f, 28.0f);
    g.setColour(juce::Colour(0xff162027));
    g.fillRoundedRectangle(area, 6.0f);
    g.setColour(juce::Colour(0xffe9f0f3));
    g.setFont(polypitch::style::font(13.0f));
    g.drawText(text, area.reduced(10.0f, 0.0f), juce::Justification::centredLeft, true);
}

void TraceView::paintBars(juce::Graphics& g, double now)
{
    // The project's bars, and its beats when they're far enough apart to read.
    barLines.clear();
    auto* host = processor.trackHost();
    if (host == nullptr || ! processor.isTimeline())
        return;
    const int usable = getWidth() - plotLeft - plotRightGap;
    host->gridLines((now - seconds * 1000.0) / 1000.0, now / 1000.0, seconds <= 30.0, barLines);
    int beats = 0;
    for (const auto& line : barLines)
        if (! line.barStart)
            ++beats;
    const bool showBeats = beats > 0 && usable / (double) beats >= 8.0;
    for (const auto& line : barLines)
    {
        if (! line.barStart && ! showBeats)
            continue;
        const double x = plotLeft + (line.seconds * 1000.0 - (now - seconds * 1000.0)) / (seconds * 1000.0) * usable;
        g.setColour(rgba(200, 212, 218, line.barStart ? 0.13f : 0.045f));
        g.fillRect((float) x, 0.0f, 1.0f, (float) getHeight());
    }
}

void TraceView::paintOthers(juce::Graphics& g, double now)
{
    // Other PolyPitch tracks, each in its own colour, so parts can be read against each other.
    othersShown = 0;
    if (! processor.view().showOthers || ! processor.isTimeline())
        return;
    static const juce::Colour palette[] { juce::Colour(0xffe8b04a), juce::Colour(0xffb48cf2), juce::Colour(0xfff07a7a), juce::Colour(0xff5ab8f0) };
    const int usable = getWidth() - plotLeft - plotRightGap;
    const double from = now - seconds * 1000.0;
    const auto xFor = [&](double ms) { return (float) (plotLeft + (ms - from) / (seconds * 1000.0) * usable); };
    std::vector<std::pair<juce::String, juce::Colour>> legend;

    juce::Graphics::ScopedSaveState save(g);
    g.reduceClipRegion(plotLeft, 0, usable, getHeight());
    for (auto* other : processor.otherInstances())
    {
        if (! other->isTimeline() || ! other->hasCleanTrace())
            continue;
        const auto colour = palette[(size_t) other->colourIndex() % 4];
        const std::lock_guard<std::mutex> lock(other->traceLock());
        const auto& f = other->traceFrames();
        size_t i = 0, high = f.size();
        while (i < high)
        {
            const size_t mid = (i + high) / 2;
            if (f[mid].timeMs < from - 200.0) i = mid + 1; else high = mid;
        }
        for (; i < f.size() && f[i].timeMs <= now + 200.0; ++i)
        {
            const float x = xFor(f[i].timeMs), xp = i > 0 ? xFor(f[i - 1].timeMs) : x;
            for (const auto& r : f[i].ridges)
            {
                if (r.core < 0.2)
                    continue;
                g.setColour(colour.withAlpha((float) juce::jmin(0.9, 0.25 + 0.7 * r.core)));
                if (r.hasPrev)
                    g.drawLine(xp, (float) yFor(r.prev), x, (float) yFor(r.m), 2.0f);
                else
                    g.fillEllipse(x - 1.5f, (float) yFor(r.m) - 1.5f, 3.0f, 3.0f);
            }
        }
        legend.push_back({ other->trackTitle(), colour });
        ++othersShown;
    }

    float y = (float) getHeight() - 40.0f;
    g.setFont(polypitch::style::font(12.0f));
    for (const auto& [name, colour] : legend)
    {
        g.setColour(colour);
        g.fillRoundedRectangle((float) plotLeft + 10.0f, y + 4.0f, 14.0f, 4.0f, 2.0f);
        g.setColour(juce::Colour(0xffc8d4da));
        g.drawText(name, juce::Rectangle<float>((float) plotLeft + 30.0f, y - 2.0f, 220.0f, 16.0f), juce::Justification::centredLeft, true);
        y -= 18.0f;
    }
}

void TraceView::paint(juce::Graphics& g)
{
    const double started = nowMs();
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    paintGrid(g);
    const double now = displayTime();
    paintBars(g, now);
    if (std::abs(processor.pitchOffset()) > 1.0e-6)
    {
        g.setColour(juce::Colour(0xff778991));
        g.setFont(polypitch::style::font(11.0f));
        g.drawText("A=" + juce::String(processor.view().referenceHz, 0), juce::Rectangle<float>(10.0f, (float) getHeight() - 20.0f, 50.0f, 14.0f), juce::Justification::centredLeft, false);
    }

    {
        const std::lock_guard<std::mutex> lock(processor.traceLock());
        paintTiles(g, now, scale);
        paintTimeAxis(g, now);
        if (processor.isTimeline())
            paintPlayhead(g, now);
        paintCursor(g, now);
        paintStatus(g);

        if (processor.traceFrames().empty() && ! processor.isAnalysing())
        {
            g.setColour(juce::Colour(0xff8fa0a9));
            g.setFont(polypitch::style::font(14.0f));
            g.drawText("Play the track and its notes will appear here", getLocalBounds().withTrimmedLeft(plotLeft), juce::Justification::centred, false);
        }
    }
    paintOthers(g, now);
    paintMs = nowMs() - started;
}

void TraceView::mouseDown(const juce::MouseEvent& event)
{
    pan.start = event.position;
    pan.lo = lo;
    pan.hi = hi;
    pan.end = displayTime();
    pan.axis = 0;
}

void TraceView::mouseDrag(const juce::MouseEvent& event)
{
    // One drag moves along whichever axis it starts out moving.
    const auto delta = event.position - pan.start;
    if (pan.axis == 0 && delta.getDistanceFromOrigin() > 6.0f)
        pan.axis = std::abs(delta.x) > std::abs(delta.y) ? 1 : 2;

    if (pan.axis == 2)
    {
        const double semitones = delta.y / getHeight() * span;
        lo = pan.lo + semitones;
        hi = pan.hi + semitones;
        clampView();
        setManualView();
        saveView();
        needsDraw = true;
    }
    else if (pan.axis == 1)
    {
        const int usable = getWidth() - plotLeft - plotRightGap;
        setReview(pan.end - delta.x / usable * seconds * 1000.0);
    }
}

void TraceView::mouseUp(const juce::MouseEvent& event)
{
    // A click away from live places the reading cursor.
    if (pan.axis == 0)
    {
        hasCursor = true;
        cursor = event.position;
        needsDraw = true;
        // On a timeline a click also moves the playhead there.
        seekToX(event.position.x);
    }
}

void TraceView::mouseMove(const juce::MouseEvent& event)
{
    hasCursor = true;
    cursor = event.position;
    if (inReview || ! audioFlowing())
        needsDraw = true;
}

void TraceView::mouseExit(const juce::MouseEvent&)
{
    hasCursor = false;
    needsDraw = true;
}

void TraceView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    // About the same distance per notch as the web app's wheel.
    const double y = -wheel.deltaY * 400.0, x = -wheel.deltaX * 400.0;
    if (event.mods.isCtrlDown() || event.mods.isCommandDown())
        zoomPitchAt(std::exp(y * 0.002), event.position.y);
    else if (event.mods.isAltDown())
        zoomTime(std::exp((x != 0.0 ? x : y) * 0.002));
    else if (event.mods.isShiftDown() || std::abs(x) > std::abs(y))
    {
        const int usable = getWidth() - plotLeft - plotRightGap;
        setReview(displayTime() + (x != 0.0 ? x : y) / usable * seconds * 1000.0);
    }
    else
        panPitch(-y / getHeight() * span);
}
