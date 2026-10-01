// ===========================================================================
// TimelineBenchmarkController.h — In-app benchmark for timeline paint perf
//
// Activated by --timeline-benchmark command-line flag.
// Runs inside the real DAW application with real repaint scheduling.
// Reports p50/p95/p99/worst for Arrangement paint, paintOverChildren,
// waveform draw, frame intervals, and all other TimelinePaintMetrics.
//
// Phases:
//   0. LoadRequested  — attempt project load from --benchmark-project
//   1. LoadConfirmed  — poll until project is stable and Arrangement rebuilt
//   2. WaveformWait   — poll until visible waveforms are ready (bounded)
//   3. Armed          — print message, wait for F9 to start
//   4. Settle         — 2s settle after F9
//   5. Warm-up        — 3s with metrics active
//   6. Measure        — 10s measurement
//   7. Report         — compute percentiles, write JSON, quit
//
// File output is atomic: writes to .tmp, fsyncs, then rename to .json.
// ===========================================================================

#pragma once

#include <JuceHeader.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include "../DiagnosticsCore/TimelinePaintMetrics.h"
#include "../MainComponent.h"
#include "../ClipCore/Clip.h"

//==============================================================================
/**
 * In-app benchmark controller for timeline paint performance.
 *
 * Usage:
 *   DAW_Core.exe --timeline-benchmark ^
 *       --benchmark-project "C:\path\to\project.dawproj" ^
 *       --benchmark-label "baseline-zoom-in-run-1"
 *
 * After project load and waveform readiness, the benchmark enters Armed state.
 * The user sets the desired zoom/panel state, then presses F9 to begin measurement.
 */
class TimelineBenchmarkController : private juce::Timer,
                                    private juce::KeyListener
{
public:
    // ── Status constants ─────────────────────────────────────────────────
    static constexpr const char* STATUS_CONTROLLER_SMOKE_ONLY = "CONTROLLER_SMOKE_ONLY";
    static constexpr const char* STATUS_ARMED                 = "ARMED";
    static constexpr const char* STATUS_OK_AUTHORITATIVE      = "OK_AUTHORITATIVE";
    static constexpr const char* STATUS_NON_AUTHORITATIVE     = "NON_AUTHORITATIVE";
    static constexpr const char* STATUS_FAILED                = "FAILED";

    TimelineBenchmarkController(juce::Component& mainWindow,
                                const juce::String& projectPath,
                                const juce::String& label,
                                int totalRuns = 1,
                                int runIndex = 0,
                                bool autoStart = false)
        : mainWindow_(mainWindow),
          projectPath_(projectPath),
          label_(label),
          runId_(juce::Uuid().toString().substring(0, 8)),
          totalRuns_(totalRuns),
          benchmarkRunIndex_(runIndex),
          autoStart_(autoStart)
    {
        DBG("[BENCHMARK] Controller created. projectPath=" << projectPath
            << " label=" << label << " runId=" << runId_
            << " runIndex=" << runIndex << "/" << totalRuns);

        // Register global key listener for F9
        if (auto* mc = findMainComponent())
        {
            mainComponentRef_ = mc;
            mc->addKeyListener(this);
            listenerAdded_ = true;
        }

        // Start with load phase after a short settle for component init
        phase_ = Phase::LoadRequested;
        startTimer(kLoadRetryMs);
    }

    ~TimelineBenchmarkController() override
    {
        stopTimer();
        removeKeyListenerIfNeeded();
    }

private:
    // ── Timing constants (milliseconds) ──────────────────────────────────
    static constexpr int kLoadRetryMs      = 200;    // poll interval for project load
    static constexpr int kWaveformPollMs   = 100;    // poll interval for waveform readiness
    static constexpr int kSettleMs         = 2000;   // settle after F9 before warm-up
    static constexpr int kWarmUpMs         = 3000;   // warm-up with metrics active
    static constexpr int kMeasureMs        = 10000;  // measurement phase
    static constexpr int kLoadTimeoutMs    = 15000;  // max wait for project load
    static constexpr int kWaveformTimeoutMs = 15000; // max wait for waveform readiness

    // ── Phase state machine ──────────────────────────────────────────────
    enum class Phase
    {
        Idle,
        LoadRequested,
        LoadConfirmed,
        WaveformWait,
        Armed,
        Settle,
        WarmUp,
        Measure,
        Report
    };

    void timerCallback() override
    {
        switch (phase_)
        {
        case Phase::LoadRequested:  onLoadPoll();       return;
        case Phase::LoadConfirmed:  onLoadConfirmed();   return;
        case Phase::WaveformWait:   onWaveformPoll();    return;
        case Phase::Armed:
            if (autoStart_)
            {
                stopTimer(); // one-shot — auto-start triggers immediately
                startArmedRun();
                return;
            }
            /* timer not running while armed normally */ return;
        case Phase::Settle:         onSettleComplete();  return;
        case Phase::WarmUp:         onWarmUpComplete();  return;
        case Phase::Measure:        onMeasureComplete(); return;
        case Phase::Report:         stopTimer();         return;
        default:                    stopTimer();         return;
        }
    }

    // ═════════════════════════════════════════════════════════════════════
    // F9 Key Listener
    // ═════════════════════════════════════════════════════════════════════
    bool keyPressed(const juce::KeyPress& key,
                    juce::Component* /*originatingComponent*/) override
    {
        if (key == juce::KeyPress::F9Key && phase_ == Phase::Armed)
        {
            onF9Pressed();
            return true; // consumed
        }
        return false; // not consumed
    }

    void onF9Pressed()
    {
        startArmedRun();
    }

    // Shared start path: called from F9 or --benchmark-auto-start
    void startArmedRun()
    {
        // Record trigger source before any branching
        if (autoStart_)
            startTrigger_ = "benchmark_auto_start";
        else if (startTrigger_.isEmpty())
            startTrigger_ = "manual_f9";

        DBG("[BENCHMARK] Starting armed run (trigger=" << startTrigger_
            << " autoStart=" << (autoStart_ ? "yes" : "no") << ")");

        // Capture full UI state at the moment of trigger
        captureUIState();

        // Transition to settle phase
        phase_ = Phase::Settle;
        TimelinePaintMetrics::active.store(false, std::memory_order_release);
        TimelinePaintMetrics::reset();
        TimelinePaintMetrics::frameSampler.reset();
        startTimer(kSettleMs);

        // Update status
        benchmarkStatus_ = "MEASURING";
    }

    // ═════════════════════════════════════════════════════════════════════
    // Phase: LoadRequested — poll project loading
    // ═════════════════════════════════════════════════════════════════════
    void onLoadPoll()
    {
        if (loadElapsedMs_ == 0)
        {
            // First poll: attempt to load the project
            if (!projectPath_.isEmpty())
                loadProjectFromPath();
        }

        // Check load progress
        auto* mainComp = findMainComponent();
        if (mainComp == nullptr)
        {
            if (loadElapsedMs_ >= kLoadTimeoutMs)
            {
                writeFailureReport("Timed out waiting for MainComponent");
                return;
            }
            loadElapsedMs_ += kLoadRetryMs;
            startTimer(kLoadRetryMs);
            return;
        }

        auto& appCore = mainComp->getAppCore();
        auto& pm = appCore.getProjectManager();
        auto& tm = appCore.getTrackManager();

        // ── Authoritative project load verification ─────────────────────
        // 1. Actual loaded project path matches requested
        auto actualPath = pm.getProjectFile().getFullPathName();
        auto normalizedRequested = juce::File(projectPath_).getFullPathName();
        bool pathOk = !projectPath_.isEmpty()
                      ? (actualPath == normalizedRequested)
                      : true;

        // 2. Project version available (non-empty project name)
        bool nameOk = !pm.getProjectName().isEmpty()
                      && pm.getProjectName() != "Untitled";

        // 3. Engine track count stable across polls
        int currentTrackCount = tm.getNumTracks();
        bool tracksOk = (currentTrackCount > 0)
                        && (currentTrackCount == lastTrackCount_)
                        && (loadStablePolls_ >= 3);

        // 4. Clip count stable
        auto& cm = appCore.getClipManager();
        int currentClipCount = cm.getAllClips().size();
        bool clipsOk = (currentClipCount == lastClipCount_)
                       && (loadStablePolls_ >= 3);

        if (pathOk && nameOk && tracksOk && clipsOk)
        {
            // Confirm project loaded
            actualLoadedProjectPath_ = actualPath;
            normalizedProjectPath_ = normalizedRequested;
            engineTrackCount_ = currentTrackCount;
            totalClipCount_ = currentClipCount;
            projectLoadSuccess_ = true;
            projectFormatVersion_ = 6; // from XML attribute "version"

            // Check master track
            masterTrackPresent_ = tm.hasMasterTrack();

            // Capture sample rate and BPM
            sampleRate_ = appCore.getCurrentSampleRate();
            bpm_ = appCore.getTransport().getTempo();

            DBG("[BENCHMARK] Project load confirmed:"
                << " path=" << actualLoadedProjectPath_
                << " tracks=" << engineTrackCount_
                << " clips=" << totalClipCount_
                << " sr=" << sampleRate_
                << " bpm=" << bpm_);

            // Transition to waveform wait
            phase_ = Phase::WaveformWait;
            waveformWaitElapsedMs_ = 0;
            startTimer(kWaveformPollMs);
            return;
        }

        // Update stability tracking
        if (currentTrackCount == lastTrackCount_
            && currentClipCount == lastClipCount_)
        {
            ++loadStablePolls_;
        }
        else
        {
            loadStablePolls_ = 0;
        }
        lastTrackCount_ = currentTrackCount;
        lastClipCount_ = currentClipCount;

        // Check timeout
        loadElapsedMs_ += kLoadRetryMs;
        if (loadElapsedMs_ >= kLoadTimeoutMs)
        {
            juce::String reason = "Project load timeout after "
                + juce::String(kLoadTimeoutMs) + "ms."
                + " pathOk=" + (pathOk ? "yes" : "no")
                + " nameOk=" + (nameOk ? "yes" : "no")
                + " tracksOk=" + (tracksOk ? "yes" : "no")
                + " clipsOk=" + (clipsOk ? "yes" : "no")
                + " stablePolls=" + juce::String(loadStablePolls_)
                + " trackCount=" + juce::String(currentTrackCount)
                + " clipCount=" + juce::String(currentClipCount);
            writeFailureReport(reason);
            return;
        }

        startTimer(kLoadRetryMs);
    }

    void onLoadConfirmed()
    {
        // Currently unused; load verification is done in onLoadPoll
    }

    // ═════════════════════════════════════════════════════════════════════
    // Phase: WaveformWait — poll waveform readiness
    // ═════════════════════════════════════════════════════════════════════
    void onWaveformPoll()
    {
        auto* mainComp = findMainComponent();
        if (mainComp == nullptr)
        {
            waveformWaitElapsedMs_ += kWaveformPollMs;
            if (waveformWaitElapsedMs_ >= kWaveformTimeoutMs)
            {
                DBG("[BENCHMARK] Waveform wait timeout — MainComponent lost");
                waveformReadinessConfirmed_ = false;
                enterArmedState();
            }
            else
            {
                startTimer(kWaveformPollMs);
            }
            return;
        }

        // Count waveforms using authoritative diagnostics snapshot
        // from ArrangementViewCore (uses ClipRenderCore::getWaveformCache().isReady()
        // under peaksLock_ — does NOT use hasAudio()).
        countViewportWaveforms();

        // Determine all-ready: waveformReadinessConfirmed_ is set only when
        // the authoritative ClipWaveformCacheCore::isReady() check passes
        // for all visible audio renderers.
        auto* arrangementView = findArrangementView();
        bool allReady = false;
        if (arrangementView != nullptr && arrangementView->getCore() != nullptr)
        {
            auto snap = arrangementView->getCore()->getVisibleWaveformReadinessForDiagnostics();
            allReady = snap.allReady();
        }
        else
        {
            allReady = (waveformNotReadyCount_ == 0 && waveformReadyCount_ > 0);
        }

        bool timedOut = (waveformWaitElapsedMs_ >= kWaveformTimeoutMs);

        if (allReady)
        {
            waveformWaitDurationMs_ = waveformWaitElapsedMs_;
            waveformReadinessConfirmed_ = true; // authoritative — renderer cache isReady()
            DBG("[BENCHMARK] Waveform cache ready: "
                << waveformReadyCount_ << " ready, "
                << waveformNotReadyCount_ << " not ready after "
                << waveformWaitDurationMs_ << "ms (authoritative)");
            enterArmedState();
            return;
        }

        if (timedOut)
        {
            waveformWaitDurationMs_ = kWaveformTimeoutMs;
            waveformReadinessConfirmed_ = false;
            DBG("[BENCHMARK] Waveform wait timeout: ready=" << waveformReadyCount_
                << " notReady=" << waveformNotReadyCount_);
            enterArmedState();
            return;
        }

        if (waveformWaitElapsedMs_ == 0)
        {
            DBG("[BENCHMARK] Waiting for waveforms: ready=" << waveformReadyCount_
                << " notReady=" << waveformNotReadyCount_);
        }

        waveformWaitElapsedMs_ += kWaveformPollMs;
        startTimer(kWaveformPollMs);
    }

    // ═════════════════════════════════════════════════════════════════════
    // Phase: Armed — wait for F9
    // ═════════════════════════════════════════════════════════════════════
    void enterArmedState()
    {
        // Capture authoritative project counts before entering armed state
        captureAuthoritativeProjectCounts();

        phase_ = Phase::Armed;
        benchmarkStatus_ = STATUS_ARMED;

        // Write ARMED JSON so the runner knows we're ready
        writeArmedReport();

        // Print message to stdout
        std::cout << "\n========================================" << std::endl;
        std::cout << "  BENCHMARK ARMED" << std::endl;
        std::cout << "  Project: " << actualLoadedProjectPath_ << std::endl;
        std::cout << "  Tracks: " << engineTrackCount_
                  << "  Clips: " << totalClipCount_ << std::endl;
        std::cout << "  Waveforms: " << waveformReadyCount_
                  << " ready, " << waveformNotReadyCount_ << " not ready";
        if (!waveformReadinessConfirmed_)
            std::cout << " (TIMEOUT — run will be NON_AUTHORITATIVE)";
        std::cout << std::endl;
        std::cout << "  Run " << (benchmarkRunIndex_ + 1) << " of " << totalRuns_ << std::endl;
        std::cout << "  Label: " << label_ << std::endl;
        std::cout << "----------------------------------------" << std::endl;
        std::cout << "  Set the required timeline zoom, scroll position," << std::endl;
        std::cout << "  playback position, Mixer/FX/Bubblegum visibility," << std::endl;
        std::cout << "  then press F9 to start measurement." << std::endl;
        std::cout << "========================================\n" << std::endl;

        DBG("[BENCHMARK] ARMED — waiting for F9");

        // Auto-start: schedule run immediately on message thread
        if (autoStart_)
        {
            label_ = "CONTROLLER_SMOKE_ONLY";
            DBG("[BENCHMARK] Auto-start enabled — starting in 500ms");
            startTimer(500); // brief settle before auto-start
        }
    }

    // ═════════════════════════════════════════════════════════════════════
    // Phase: Settle complete → Warm-up
    // ═════════════════════════════════════════════════════════════════════
    void onSettleComplete()
    {
        DBG("[BENCHMARK] Settle complete, starting warm-up (" << kWarmUpMs << " ms)");

        // Activate metrics for warm-up
        TimelinePaintMetrics::active.store(true, std::memory_order_release);
        TimelinePaintMetrics::reset();
        TimelinePaintMetrics::frameSampler.reset();

        phase_ = Phase::WarmUp;
        startTimer(kWarmUpMs);
    }

    // ═════════════════════════════════════════════════════════════════════
    // Phase: Warm-up complete → Measure
    // ═════════════════════════════════════════════════════════════════════
    void onWarmUpComplete()
    {
        auto warmSnap = TimelinePaintMetrics::snapshot();
        DBG("[BENCHMARK] Warm-up counters: paintCount=" << warmSnap.paintCount
            << " requestedFullRepaints=" << warmSnap.requestedFullRepaints);

        // Clean transition: deactivate → reset → reactivate
        TimelinePaintMetrics::active.store(false, std::memory_order_release);
        TimelinePaintMetrics::reset();
        TimelinePaintMetrics::frameSampler.reset();
        TimelinePaintMetrics::active.store(true, std::memory_order_release);

        phase_ = Phase::Measure;
        startTimer(kMeasureMs);
        DBG("[BENCHMARK] Measure phase (" << kMeasureMs << " ms)");
    }

    // ═════════════════════════════════════════════════════════════════════
    // Phase: Measure complete → Report
    // ═════════════════════════════════════════════════════════════════════
    void onMeasureComplete()
    {
        phase_ = Phase::Report;
        reportAndQuit();
    }

    // ═════════════════════════════════════════════════════════════════════
    // Project loading from command line
    // ═════════════════════════════════════════════════════════════════════
    void loadProjectFromPath()
    {
        if (projectPath_.isEmpty())
        {
            DBG("[BENCHMARK] No project path provided; relying on default/startup project");
            return;
        }

        auto projectFile = juce::File(projectPath_);
        if (!projectFile.existsAsFile())
        {
            DBG("[BENCHMARK] Project file does not exist: " << projectPath_);
            writeFailureReport("Project file not found: " + projectPath_);
            return;
        }

        auto* mainComp = findMainComponent();
        if (mainComp == nullptr)
        {
            DBG("[BENCHMARK] Cannot load project: MainComponent not available");
            writeFailureReport("MainComponent not available for project load");
            return;
        }

        auto& pm = mainComp->getAppCore().getProjectManager();
        if (pm.loadFromFile(projectFile))
        {
            DBG("[BENCHMARK] Project load initiated: " << projectFile.getFullPathName());
        }
        else
        {
            juce::String error = "Project load failed: " + pm.getLastLoadError();
            DBG("[BENCHMARK] " << error);
            writeFailureReport(error);
        }
    }

    // ═════════════════════════════════════════════════════════════════════
    // Authoritative project counts (from production models, not component tree)
    // ═════════════════════════════════════════════════════════════════════
    void captureAuthoritativeProjectCounts()
    {
        auto* mainComp = findMainComponent();
        if (mainComp == nullptr) return;

        auto& appCore = mainComp->getAppCore();
        auto& tm = appCore.getTrackManager();
        auto& cm = appCore.getClipManager();
        auto& pm = appCore.getProjectManager();

        // Track counts
        engineTrackCount_ = tm.getNumTracks();
        masterTrackPresent_ = tm.hasMasterTrack();

        // Mute/solo counts
        mutedTrackCount_ = 0;
        soloedTrackCount_ = 0;
        pluginInstanceCount_ = 0;
        for (int i = 0; i < engineTrackCount_; ++i)
        {
            auto* track = tm.getTrack(i);
            if (track == nullptr) continue;
            if (track->isMuted())   ++mutedTrackCount_;
            if (track->isSoloed())  ++soloedTrackCount_;
            if (track->getPluginChain())
            {
                auto* chain = track->getPluginChain();
                pluginInstanceCount_ += chain->getNumActiveSlots();
            }
        }

        // Master track plugin count
        if (masterTrackPresent_)
        {
            auto* master = tm.getMasterTrack();
            if (master && master->getPluginChain())
                pluginInstanceCount_ += master->getPluginChain()->getNumActiveSlots();
        }

        // Clip counts by type
        totalClipCount_ = 0;
        audioClipCount_ = 0;
        midiClipCount_ = 0;
        patternClipCount_ = 0;

        const auto& allClips = cm.getAllClips();
        totalClipCount_ = allClips.size();
        for (auto* clip : allClips)
        {
            if (clip == nullptr) continue;
            switch (clip->getType())
            {
            case DAW::ClipType::Audio:       ++audioClipCount_;  break;
            case DAW::ClipType::MIDI:        ++midiClipCount_;   break;
            case DAW::ClipType::Pattern:     ++patternClipCount_; break;
            default: break;
            }
        }

        // Cross-check with XML expectations from initial inspection
        // Expected: 11 tracks + Master, 8 audio clips
        if (engineTrackCount_ != 11)
        {
            DBG("[BENCHMARK] WARNING: Expected 11 tracks, loaded "
                << engineTrackCount_);
        }
        if (audioClipCount_ != 8)
        {
            DBG("[BENCHMARK] WARNING: Expected 8 audio clips, loaded "
                << audioClipCount_);
        }

        DBG("[BENCHMARK] Authoritative counts:"
            << " tracks=" << engineTrackCount_
            << " master=" << (masterTrackPresent_ ? "yes" : "no")
            << " muted=" << mutedTrackCount_
            << " soloed=" << soloedTrackCount_
            << " totalClips=" << totalClipCount_
            << " audio=" << audioClipCount_
            << " midi=" << midiClipCount_
            << " pattern=" << patternClipCount_
            << " plugins=" << pluginInstanceCount_);
    }

    // ═════════════════════════════════════════════════════════════════════
    // UI State Capture
    // ═════════════════════════════════════════════════════════════════════
    void captureUIState()
    {
        auto* mainComp = findMainComponent();
        if (mainComp == nullptr) return;

        auto& appCore = mainComp->getAppCore();

        // Window bounds and monitor info
        if (auto* peer = mainWindow_.getPeer())
        {
            windowBounds_ = peer->getBounds();
        }

        // DPI, refresh rate, and monitor index via JUCE displays API
        dpiScale_ = mainWindow_.getDesktopScaleFactor();
        auto& displays = juce::Desktop::getInstance().getDisplays();
        if (auto* display = displays.getDisplayForRect(mainWindow_.getScreenBounds()))
        {
            refreshRate_ = display->verticalFrequencyHz.value_or(60.0);
            // Derive monitor index by enumerating displays
            int idx = 0;
            for (auto& d : displays.displays)
            {
                if (d.totalArea == display->totalArea
                    && d.userArea == display->userArea)
                {
                    monitorIndex_ = idx;
                    break;
                }
                ++idx;
            }
        }

        // Find ArrangementView and capture diagnostics snapshot
        // (uses production viewport rectangle — not component bounds)
        auto* arrangementView = findArrangementView();
        if (arrangementView != nullptr)
        {
            auto* core = arrangementView->getCore();
            if (core != nullptr)
            {
                timelineZoom_ = core->getZoom().getPixelsPerSecond();
                if (sampleRate_ > 0.0)
                    samplesPerPixel_ = sampleRate_ / timelineZoom_;

                // Viewport scroll
                if (auto* viewport = arrangementView->findParentComponentOfClass<juce::Viewport>())
                {
                    horizontalScroll_ = (double)viewport->getViewPositionX();
                    verticalScroll_ = viewport->getViewPositionY();
                }

                // Visible track range from ArrangementView
                auto visibleIds = core->getVisibleTrackIds();
                visibleTrackCount_ = (int)visibleIds.size();

                // ── Viewport-visible area capture ─────────────────────────
                // Use the diagnostics snapshot which computes the viewport
                // rectangle from the production Viewport, not from
                // component bounds.  This guarantees that visible time,
                // clip count, and waveform readiness all refer to the
                // same viewport-visible area.
                auto snap = core->getVisibleWaveformReadinessForDiagnostics();

                // Visible viewport dimensions (from actual Viewport, not getLocalBounds)
                viewportVisibleWidth_  = snap.viewportWidthPx;
                viewportVisibleHeight_ = snap.viewportHeightPx;

                // Visible time range using viewport width (not full component width)
                double timeAtLeft = 0.0;
                double timeAtRight = 0.0;
                if (timelineZoom_ > 0.0 && viewportVisibleWidth_ > 0)
                {
                    timeAtLeft  = horizontalScroll_ / timelineZoom_;
                    timeAtRight = (horizontalScroll_ + (double)viewportVisibleWidth_) / timelineZoom_;
                }
                visibleTimeStart_ = timeAtLeft;
                visibleTimeEnd_   = timeAtRight;

                // Visible clip count — only renderers intersecting the
                // actual viewport-visible rectangle
                visibleClipCount_ = snap.totalVisibleRenderers;

                // ── Consistency validation ────────────────────────────────
                double expectedDuration = (viewportVisibleWidth_ > 0 && timelineZoom_ > 0.0)
                    ? (double)viewportVisibleWidth_ / timelineZoom_
                    : 0.0;
                double actualDuration = visibleTimeEnd_ - visibleTimeStart_;
                if (expectedDuration > 0.0 && actualDuration > 0.0)
                {
                    double ratio = actualDuration / expectedDuration;
                    if (std::abs(ratio - 1.0) > 0.01)
                    {
                        DBG("[BENCHMARK] WARNING: visible duration mismatch"
                            << " expected=" << expectedDuration
                            << " actual=" << actualDuration
                            << " ratio=" << ratio);
                    }
                }
                DBG("[BENCHMARK] Viewport capture:"
                    << " viewW=" << viewportVisibleWidth_
                    << " viewH=" << viewportVisibleHeight_
                    << " scrollX=" << horizontalScroll_
                    << " scrollY=" << verticalScroll_
                    << " start=" << visibleTimeStart_
                    << " end=" << visibleTimeEnd_
                    << " duration=" << (visibleTimeEnd_ - visibleTimeStart_)
                    << " expected=" << expectedDuration
                    << " visibleClips=" << visibleClipCount_);
            }
        }

        // Mixer/FX/Bubblegum visibility — traverse component hierarchy
        mixerVisible_ = isChildVisible(mainComp, "MixerPanel");
        fxChainVisible_ = isChildVisible(mainComp, "MixerPluginSidePanel");
        bubblegumVisible_ = isChildVisible(mainComp, "BubblegumV2PanelUI");

        // Transport state
        auto& transport = appCore.getTransport();
        transportPlaying_ = transport.isPlaying();
        transportPosition_ = (double)transport.getPosition();

        // Master strip state (front = visible on top)
        masterStripFront_ = isChildVisible(mainComp, "MasterBubble");

        DBG("[BENCHMARK] UI state captured:"
            << " window=" << windowBounds_.toString()
            << " monitor=" << monitorIndex_
            << " dpi=" << dpiScale_
            << " refresh=" << refreshRate_
            << " zoom=" << timelineZoom_
            << " hScroll=" << horizontalScroll_
            << " vScroll=" << verticalScroll_
            << " visibleTracks=" << visibleTrackCount_
            << " visibleClips=" << visibleClipCount_
            << " mixer=" << (mixerVisible_ ? "vis" : "hid")
            << " fx=" << (fxChainVisible_ ? "vis" : "hid")
            << " bg=" << (bubblegumVisible_ ? "vis" : "hid")
            << " playing=" << (transportPlaying_ ? "yes" : "no")
            << " pos=" << transportPosition_);
    }

    // ═════════════════════════════════════════════════════════════════════
    // Viewport waveform counting — uses diagnostics snapshot from
    // ArrangementViewCore::getVisibleWaveformReadinessForDiagnostics().
    // This calls ClipRenderCore::getWaveformCache().isReady() under the
    // existing peaksLock_ — the authoritative waveform cache check.
    // ═════════════════════════════════════════════════════════════════════
    void countViewportWaveforms()
    {
        waveformReadyCount_ = 0;
        waveformNotReadyCount_ = 0;

        auto* arrangementView = findArrangementView();
        if (arrangementView == nullptr || arrangementView->getCore() == nullptr)
            return;

        auto snap = arrangementView->getCore()->getVisibleWaveformReadinessForDiagnostics();
        waveformReadyCount_ = snap.ready;
        waveformNotReadyCount_ = snap.notReady;

        DBG("[BENCHMARK] Waveform snapshot: visibleAudio="
            << snap.visibleAudioRenderers
            << " ready=" << snap.ready
            << " notReady=" << snap.notReady
            << " missing=" << snap.missingRenderer
            << " allReady=" << (snap.allReady() ? "yes" : "no"));
    }

    // ═════════════════════════════════════════════════════════════════════
    // Component tree helpers
    // ═════════════════════════════════════════════════════════════════════
    MainComponent* findMainComponent()
    {
        if (auto* dw = dynamic_cast<juce::DocumentWindow*>(&mainWindow_))
            return dynamic_cast<MainComponent*>(dw->getContentComponent());
        return nullptr;
    }

    DAW::ArrangementView* findArrangementView()
    {
        auto* mainComp = findMainComponent();
        if (mainComp == nullptr) return nullptr;

        // Search for ArrangementView in the component tree
        return findComponentOfType<DAW::ArrangementView>(mainComp);
    }

    template<typename T>
    T* findComponentOfType(juce::Component* parent) const
    {
        if (parent == nullptr) return nullptr;
        if (auto* found = dynamic_cast<T*>(parent))
            return found;
        for (int i = 0; i < parent->getNumChildComponents(); ++i)
        {
            if (auto* found = findComponentOfType<T>(parent->getChildComponent(i)))
                return found;
        }
        return nullptr;
    }

    bool isChildVisible(juce::Component* parent, const juce::String& className)
    {
        if (parent == nullptr) return false;
        // Check if any child component's class name contains the given string
        for (int i = 0; i < parent->getNumChildComponents(); ++i)
        {
            auto* child = parent->getChildComponent(i);
            if (child == nullptr) continue;
            // Check by dynamic_cast attempt or name
            if (child->isVisible())
            {
                // Use component name as fallback
                if (child->getName().contains(className))
                    return true;
                if (isChildVisible(child, className))
                    return true;
            }
        }
        return false;
    }

    // ═════════════════════════════════════════════════════════════════════
    // Key listener lifecycle
    // ═════════════════════════════════════════════════════════════════════
    // Idempotent: safe to call multiple times; does nothing after first removal.
    void removeKeyListenerIfNeeded()
    {
        if (!listenerAdded_) return;
        listenerAdded_ = false;
        if (mainComponentRef_ != nullptr)
            mainComponentRef_->removeKeyListener(this);
        mainComponentRef_ = nullptr;
    }

    // ═════════════════════════════════════════════════════════════════════
    // JSON Reporting
    // ═════════════════════════════════════════════════════════════════════
    void writeFailureReport(const juce::String& reason)
    {
        benchmarkStatus_ = STATUS_FAILED;

        std::ostringstream json;
        json << "{\n";
        json << "  \"benchmark\": \"timeline-paint\",\n";
        json << "  \"label\": \"" << label_ << "\",\n";
        json << "  \"runId\": \"" << runId_ << "\",\n";
        json << "  \"runIndex\": " << benchmarkRunIndex_ << ",\n";
        json << "  \"totalRuns\": " << totalRuns_ << ",\n";
        json << "  \"status\": \"" << STATUS_FAILED << "\",\n";
        json << "  \"startTrigger\": \"" << startTrigger_ << "\",\n";
        json << "  \"reason\": \"" << escapeJson(reason) << "\",\n";
        json << "  \"projectPath\": \"" << escapeJson(projectPath_) << "\",\n";
        json << "  \"projectPathLength\": " << projectPath_.length() << ",\n";
        json << "  \"projectPathChars\": [";
        for (int i = 0; i < projectPath_.length(); ++i)
        {
            if (i > 0) json << ",";
            json << (int)(juce::juce_wchar)projectPath_[i];
        }
        json << "]\n";
        json << "}\n";

        atomicWriteFile("FAILED", json.str());
        DBG("[BENCHMARK] FAILURE: " << reason);

        removeKeyListenerIfNeeded();
        if (auto* app = juce::JUCEApplication::getInstance())
            app->systemRequestedQuit();
    }

    void writeArmedReport()
    {
        std::ostringstream json;
        json << std::fixed << std::setprecision(1);

        json << "{\n";
        json << "  \"benchmark\": \"timeline-paint\",\n";
        json << "  \"label\": \"" << label_ << "\",\n";
        json << "  \"runId\": \"" << runId_ << "\",\n";
        json << "  \"runIndex\": " << benchmarkRunIndex_ << ",\n";
        json << "  \"totalRuns\": " << totalRuns_ << ",\n";
        json << "  \"status\": \"" << STATUS_ARMED << "\",\n";
        json << "  \"project_info\": {\n";
        json << "    \"requested_path\": \"" << escapeJson(projectPath_) << "\",\n";
        json << "    \"normalized_path\": \"" << escapeJson(normalizedProjectPath_) << "\",\n";
        json << "    \"loaded_path\": \"" << escapeJson(actualLoadedProjectPath_) << "\",\n";
        json << "    \"load_success\": " << (projectLoadSuccess_ ? "true" : "false") << ",\n";
        json << "    \"format_version\": " << projectFormatVersion_ << ",\n";
        json << "    \"sample_rate\": " << sampleRate_ << ",\n";
        json << "    \"bpm\": " << bpm_ << ",\n";
        json << "    \"engine_track_count\": " << engineTrackCount_ << ",\n";
        json << "    \"master_track_present\": " << (masterTrackPresent_ ? "true" : "false") << ",\n";
        json << "    \"muted_track_count\": " << mutedTrackCount_ << ",\n";
        json << "    \"soloed_track_count\": " << soloedTrackCount_ << ",\n";
        json << "    \"total_clip_count\": " << totalClipCount_ << ",\n";
        json << "    \"audio_clip_count\": " << audioClipCount_ << ",\n";
        json << "    \"midi_clip_count\": " << midiClipCount_ << ",\n";
        json << "    \"pattern_clip_count\": " << patternClipCount_ << ",\n";
        json << "    \"plugin_instance_count\": " << pluginInstanceCount_ << ",\n";
        json << "    \"waveform_ready_count\": " << waveformReadyCount_ << ",\n";
        json << "    \"waveform_not_ready_count\": " << waveformNotReadyCount_ << ",\n";
        json << "    \"waveform_readiness_confirmed\": "
            << (waveformReadinessConfirmed_ ? "true" : "false") << ",\n";
        json << "    \"waveform_wait_duration_ms\": " << waveformWaitDurationMs_ << "\n";
        json << "  },\n";
        json << "  \"startTrigger\": \"" << startTrigger_ << "\"\n";
        json << "}\n";

        atomicWriteFile("ARMED", json.str());
    }

    void reportAndQuit()
    {
        // Deactivate metrics immediately
        TimelinePaintMetrics::active.store(false, std::memory_order_release);

        auto snap = TimelinePaintMetrics::snapshot();

        // Determine authority status
        bool authoritative = projectLoadSuccess_
                             && waveformReadinessConfirmed_
                             && snap.paintCount > 0;
        benchmarkStatus_ = authoritative ? STATUS_OK_AUTHORITATIVE
                                         : STATUS_NON_AUTHORITATIVE;

        // Compute percentiles from sample rings
        auto paintDur    = TimelinePaintMetrics::paintDurationNs.computePercentiles();
        auto childrenDur = TimelinePaintMetrics::paintChildrenDurationNs.computePercentiles();
        auto waveDur     = TimelinePaintMetrics::waveformDurationNs.computePercentiles();
        auto timerInt    = TimelinePaintMetrics::frameIntervalNs.computePercentiles();
        auto reqAreaPct  = TimelinePaintMetrics::requestedDirtyAreaPixelsRing.computePercentiles();
        auto actualClipArea = TimelinePaintMetrics::actualPaintClipAreaPixels.computePercentiles();

        auto paintRingDropped    = TimelinePaintMetrics::paintDurationNs.dropped();
        auto childrenRingDropped = TimelinePaintMetrics::paintChildrenDurationNs.dropped();
        auto waveRingDropped     = TimelinePaintMetrics::waveformDurationNs.dropped();
        auto timerRingDropped    = TimelinePaintMetrics::frameIntervalNs.dropped();
        auto reqAreaRingDropped  = TimelinePaintMetrics::requestedDirtyAreaPixelsRing.dropped();
        auto actualClipRingDropped = TimelinePaintMetrics::actualPaintClipAreaPixels.dropped();

        auto paintRingSamples    = TimelinePaintMetrics::paintDurationNs.size();
        auto childrenRingSamples = TimelinePaintMetrics::paintChildrenDurationNs.size();
        auto waveRingSamples     = TimelinePaintMetrics::waveformDurationNs.size();
        auto timerRingSamples    = TimelinePaintMetrics::frameIntervalNs.size();
        auto reqAreaRingSamples  = TimelinePaintMetrics::requestedDirtyAreaPixelsRing.size();
        auto actualClipRingSamples = TimelinePaintMetrics::actualPaintClipAreaPixels.size();

        // Build JSON report
        std::ostringstream json;
        json << std::fixed << std::setprecision(1);

        json << "{\n";
        json << "  \"benchmark\": \"timeline-paint\",\n";
        json << "  \"label\": \"" << label_ << "\",\n";
        json << "  \"runId\": \"" << runId_ << "\",\n";
        json << "  \"runIndex\": " << benchmarkRunIndex_ << ",\n";
        json << "  \"totalRuns\": " << totalRuns_ << ",\n";
        json << "  \"status\": \"" << benchmarkStatus_ << "\",\n";
        json << "  \"startTrigger\": \"" << startTrigger_ << "\",\n";

        // ── Project info ─────────────────────────────────────────────────
        json << "  \"project_info\": {\n";
        json << "    \"requested_path\": \"" << escapeJson(projectPath_) << "\",\n";
        json << "    \"normalized_path\": \"" << escapeJson(normalizedProjectPath_) << "\",\n";
        json << "    \"loaded_path\": \"" << escapeJson(actualLoadedProjectPath_) << "\",\n";
        json << "    \"load_success\": " << (projectLoadSuccess_ ? "true" : "false") << ",\n";
        json << "    \"format_version\": " << projectFormatVersion_ << ",\n";
        json << "    \"sample_rate\": " << sampleRate_ << ",\n";
        json << "    \"bpm\": " << bpm_ << ",\n";
        json << "    \"engine_track_count\": " << engineTrackCount_ << ",\n";
        json << "    \"master_track_present\": " << (masterTrackPresent_ ? "true" : "false") << ",\n";
        json << "    \"muted_track_count\": " << mutedTrackCount_ << ",\n";
        json << "    \"soloed_track_count\": " << soloedTrackCount_ << ",\n";
        json << "    \"total_clip_count\": " << totalClipCount_ << ",\n";
        json << "    \"audio_clip_count\": " << audioClipCount_ << ",\n";
        json << "    \"midi_clip_count\": " << midiClipCount_ << ",\n";
        json << "    \"pattern_clip_count\": " << patternClipCount_ << ",\n";
        json << "    \"plugin_instance_count\": " << pluginInstanceCount_ << ",\n";
        json << "    \"waveform_ready_count\": " << waveformReadyCount_ << ",\n";
        json << "    \"waveform_not_ready_count\": " << waveformNotReadyCount_ << ",\n";
        json << "    \"waveform_readiness_confirmed\": "
            << (waveformReadinessConfirmed_ ? "true" : "false") << ",\n";
        json << "    \"waveform_wait_duration_ms\": " << waveformWaitDurationMs_ << "\n";
        json << "  },\n";

        // ── UI state at trigger ──────────────────────────────────────────
        json << "  \"ui_state\": {\n";
        json << "    \"window_bounds\": \"" << windowBounds_.toString() << "\",\n";
        json << "    \"monitor_index\": " << monitorIndex_ << ",\n";
        json << "    \"refresh_rate_hz\": " << refreshRate_ << ",\n";
        json << "    \"dpi_scale\": " << dpiScale_ << ",\n";
        json << "    \"timeline_zoom_px_per_sec\": " << timelineZoom_ << ",\n";
        json << "    \"samples_per_pixel\": " << samplesPerPixel_ << ",\n";
        json << "    \"horizontal_scroll_px\": " << horizontalScroll_ << ",\n";
        json << "    \"vertical_scroll_px\": " << verticalScroll_ << ",\n";
        json << "    \"visible_time_start_sec\": " << visibleTimeStart_ << ",\n";
        json << "    \"visible_time_end_sec\": " << visibleTimeEnd_ << ",\n";
        json << "    \"visible_time_duration_sec\": " << (visibleTimeEnd_ - visibleTimeStart_) << ",\n";
        json << "    \"visible_track_count\": " << visibleTrackCount_ << ",\n";
        json << "    \"visible_clip_count\": " << visibleClipCount_ << ",\n";
        json << "    \"viewport_visible_width\": " << viewportVisibleWidth_ << ",\n";
        json << "    \"viewport_visible_height\": " << viewportVisibleHeight_ << ",\n";
        json << "    \"mixer_visible\": " << (mixerVisible_ ? "true" : "false") << ",\n";
        json << "    \"fx_chain_visible\": " << (fxChainVisible_ ? "true" : "false") << ",\n";
        json << "    \"bubblegum_visible\": " << (bubblegumVisible_ ? "true" : "false") << ",\n";
        json << "    \"master_strip_front\": " << (masterStripFront_ ? "true" : "false") << ",\n";
        json << "    \"transport_playing\": " << (transportPlaying_ ? "true" : "false") << ",\n";
        json << "    \"transport_position_samples\": " << transportPosition_ << "\n";
        json << "  },\n";

        // ── Counters ─────────────────────────────────────────────────────
        json << "  \"counters\": {\n";
        json << "    \"paintCount\": "              << snap.paintCount << ",\n";
        json << "    \"paintOverChildrenCount\": "  << snap.paintOverChildrenCount << ",\n";
        json << "    \"rulerPaintCount\": "         << snap.rulerPaintCount << ",\n";
        json << "    \"clipRenderPaints\": "        << snap.clipRenderPaints << ",\n";
        json << "    \"tracksIterated\": "          << snap.tracksIterated << ",\n";
        json << "    \"tracksCulled\": "            << snap.tracksCulled << ",\n";
        json << "    \"clipsPainted\": "            << snap.clipsPainted << ",\n";
        json << "    \"clipsCulled\": "             << snap.clipsCulled << ",\n";
        json << "    \"trackListScrollEvents\": "  << snap.trackListScrollEvents << ",\n";
        json << "    \"trackListScrollResizedCalls\": " << snap.trackListScrollResizedCalls << ",\n";
        json << "    \"trackListScrollRowBoundsAssignments\": " << snap.trackListScrollRowBoundsAssignments << ",\n";
        json << "    \"trackListScrollRowPositionUpdates\": " << snap.trackListScrollRowPositionUpdates << ",\n";
        json << "    \"arrangementScrollEvents\": " << snap.arrangementScrollEvents << ",\n";
        json << "    \"arrangementScrollClipIterations\": " << snap.arrangementScrollClipIterations << ",\n";
        json << "    \"arrangementScrollVisibilityChanges\": " << snap.arrangementScrollVisibilityChanges << ",\n";
        json << "    \"arrangementScrollWaveformRefreshes\": " << snap.arrangementScrollWaveformRefreshes << ",\n";
        json << "    \"arrangementScrollFullRepaints\": " << snap.arrangementScrollFullRepaints << ",\n";
        json << "    \"arrangementScrollPartialRepaints\": " << snap.arrangementScrollPartialRepaints << ",\n";
        json << "    \"arrangementScrollDirtyAreaPixels\": " << snap.arrangementScrollDirtyAreaPixels << ",\n";
        json << "    \"waveformDrawCalls\": "       << snap.waveformDrawCalls << ",\n";
        json << "    \"waveformPeaksWalked\": "     << snap.waveformPeaksWalked << ",\n";
        json << "    \"waveformCachedDraws\": "     << snap.waveformCachedDraws << ",\n";
        json << "    \"automationTimerTicks\": "    << snap.automationTimerTicks << ",\n";
        json << "    \"automationTimerSkipped\": "  << snap.automationTimerSkipped << ",\n";
        json << "    \"automationTimerInvalidated\": " << snap.automationTimerInvalidated << ",\n";
        json << "    \"automationContainersProcessed\": " << snap.automationContainersProcessed << ",\n";
        json << "    \"automationCacheInvalidations\": " << snap.automationCacheInvalidations << ",\n";
        json << "    \"automationPhase1LanesDrawn\": " << snap.automationPhase1LanesDrawn << ",\n";
        json << "    \"gridBarsGenerated\": "       << snap.gridBarsGenerated << ",\n";
        json << "    \"gridBeatsGenerated\": "      << snap.gridBeatsGenerated << "\n";
        json << "  },\n";

        // ── Requested repaint metrics ────────────────────────────────────
        json << "  \"requestedRepaintMetrics\": {\n";
        json << "    \"requestedFullRepaints\": "     << snap.requestedFullRepaints << ",\n";
        json << "    \"requestedPartialRepaints\": "   << snap.requestedPartialRepaints << ",\n";
        json << "    \"requestedDirtyAreaPixelsTotal\": " << snap.requestedDirtyAreaPixels << ",\n";
        json << "    \"requestedDirtyAreaPixelsSamples\": " << reqAreaRingSamples << ",\n";
        json << "    \"requestedDirtyAreaPercentiles\": {\n";
        if (reqAreaRingSamples > 0) {
            json << "      \"samples\": " << reqAreaRingSamples << ",\n";
            json << "      \"dropped\": " << reqAreaRingDropped << ",\n";
            json << "      \"p50\": " << reqAreaPct.p50_ns << ",\n";
            json << "      \"p95\": " << reqAreaPct.p95_ns << ",\n";
            json << "      \"p99\": " << reqAreaPct.p99_ns << ",\n";
            json << "      \"worst\": " << reqAreaPct.worst_ns << ",\n";
            json << "      \"mean\": " << reqAreaPct.mean_ns << "\n";
        } else {
            json << "      \"status\": \"no-samples\"\n";
        }
        json << "    }\n";
        json << "  },\n";

        // ── Actual paint metrics ─────────────────────────────────────────
        json << "  \"actualPaintMetrics\": {\n";
        json << "    \"actualFullPaintInvocations\": " << snap.actualFullPaintInvocations << ",\n";
        json << "    \"actualPartialPaintInvocations\": " << snap.actualPartialPaintInvocations << ",\n";
        json << "    \"actualPaintClipAreaPixelsSamples\": " << actualClipRingSamples << ",\n";
        json << "    \"actualPaintClipAreaPercentiles\": {\n";
        if (actualClipRingSamples > 0) {
            json << "      \"samples\": " << actualClipRingSamples << ",\n";
            json << "      \"dropped\": " << actualClipRingDropped << ",\n";
            json << "      \"p50\": " << actualClipArea.p50_ns << ",\n";
            json << "      \"p95\": " << actualClipArea.p95_ns << ",\n";
            json << "      \"p99\": " << actualClipArea.p99_ns << ",\n";
            json << "      \"worst\": " << actualClipArea.worst_ns << ",\n";
            json << "      \"mean\": " << actualClipArea.mean_ns << "\n";
        } else {
            json << "      \"status\": \"no-samples\"\n";
        }
        json << "    }\n";
        json << "  },\n";

        // ── Paint duration percentiles ───────────────────────────────────
        json << "  \"paintDurationNs\": {\n";
        json << "    \"samples\": " << paintRingSamples << ",\n";
        json << "    \"dropped\": " << paintRingDropped << ",\n";
        json << "    \"p50\": " << paintDur.p50_ns << ",\n";
        json << "    \"p95\": " << paintDur.p95_ns << ",\n";
        json << "    \"p99\": " << paintDur.p99_ns << ",\n";
        json << "    \"worst\": " << paintDur.worst_ns << ",\n";
        json << "    \"mean\": " << paintDur.mean_ns << "\n";
        json << "  },\n";

        json << "  \"paintChildrenDurationNs\": {\n";
        json << "    \"samples\": " << childrenRingSamples << ",\n";
        json << "    \"dropped\": " << childrenRingDropped << ",\n";
        json << "    \"p50\": " << childrenDur.p50_ns << ",\n";
        json << "    \"p95\": " << childrenDur.p95_ns << ",\n";
        json << "    \"p99\": " << childrenDur.p99_ns << ",\n";
        json << "    \"worst\": " << childrenDur.worst_ns << ",\n";
        json << "    \"mean\": " << childrenDur.mean_ns << "\n";
        json << "  },\n";

        json << "  \"waveformDurationNs\": {\n";
        json << "    \"samples\": " << waveRingSamples << ",\n";
        json << "    \"dropped\": " << waveRingDropped << ",\n";
        json << "    \"p50\": " << waveDur.p50_ns << ",\n";
        json << "    \"p95\": " << waveDur.p95_ns << ",\n";
        json << "    \"p99\": " << waveDur.p99_ns << ",\n";
        json << "    \"worst\": " << waveDur.worst_ns << ",\n";
        json << "    \"mean\": " << waveDur.mean_ns << "\n";
        json << "  },\n";

        json << "  \"arrangementViewMessageThreadTimerIntervalNs\": {\n";
        json << "    \"description\": \"ArrangementView message-thread timer interval; not OS-presented FPS, VSync timing, or ApexPresentationClock presentation evidence.\",\n";
        json << "    \"samples\": " << timerRingSamples << ",\n";
        json << "    \"dropped\": " << timerRingDropped << ",\n";
        json << "    \"p50\": " << timerInt.p50_ns << ",\n";
        json << "    \"p95\": " << timerInt.p95_ns << ",\n";
        json << "    \"p99\": " << timerInt.p99_ns << ",\n";
        json << "    \"worst\": " << timerInt.worst_ns << ",\n";
        json << "    \"mean\": " << timerInt.mean_ns << "\n";
        json << "  }\n";

        json << "}\n";
        json << std::endl;

        // Print to stdout
        std::cout << "\n===== BENCHMARK RESULT =====" << std::endl;
        std::cout << json.str();
        std::cout << "===== END BENCHMARK RESULT =====" << std::endl;

        // Atomic file write
        atomicWriteFile(benchmarkStatus_, json.str());

        DBG("[BENCHMARK] Report complete. Exiting.");

        removeKeyListenerIfNeeded();
        if (auto* app = juce::JUCEApplication::getInstance())
            app->systemRequestedQuit();
    }

    // ── Atomic file write ───────────────────────────────────────────────
    void atomicWriteFile(const juce::String& suffix, const juce::String& content)
    {
        auto reportDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                             .getChildFile("apex-benchmark-reports");
        reportDir.createDirectory();

        auto timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
        auto finalFile = reportDir.getChildFile(
            "timeline-benchmark-" + suffix + "-" + timestamp + "-" + runId_ + ".json");
        auto tmpFile = reportDir.getChildFile(
            "timeline-benchmark-" + suffix + "-" + timestamp + "-" + runId_ + ".json.tmp");

        {
            juce::FileOutputStream fos(tmpFile);
            if (fos.openedOk())
            {
                auto utf8 = content.toUTF8();
                fos.write(utf8, utf8.sizeInBytes() - 1);
                fos.flush();
            }
            else
            {
                DBG("[BENCHMARK] ERROR: could not write temp file: " << tmpFile.getFullPathName());
                return;
            }
        }

        if (!tmpFile.moveFileTo(finalFile))
        {
            DBG("[BENCHMARK] ERROR: could not rename " << tmpFile.getFullPathName()
                << " -> " << finalFile.getFullPathName());
        }
        else
        {
            DBG("[BENCHMARK] Report written to " << finalFile.getFullPathName());
        }
    }

    // ── JSON string escaping ────────────────────────────────────────────
    static juce::String escapeJson(const juce::String& input)
    {
        juce::String result;
        for (auto c : input)
        {
            switch (c)
            {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:   result += c;      break;
            }
        }
        return result;
    }

    // ═════════════════════════════════════════════════════════════════════
    // Member variables
    // ═════════════════════════════════════════════════════════════════════

    // ── Construction parameters ─────────────────────────────────────────
    juce::Component& mainWindow_;
    juce::String projectPath_;
    juce::String label_;
    juce::String runId_;
    int totalRuns_ = 1;
    int benchmarkRunIndex_ = 0;
    bool autoStart_ = false;

    // ── Key listener state (safe removal across all exit paths) ─────────
    juce::Component::SafePointer<juce::Component> mainComponentRef_;
    bool listenerAdded_ = false;

    // ── Phase state ─────────────────────────────────────────────────────
    Phase phase_ = Phase::Idle;
    juce::String benchmarkStatus_ = STATUS_CONTROLLER_SMOKE_ONLY;

    // ── Project load state ──────────────────────────────────────────────
    bool    projectLoadSuccess_ = false;
    int     loadElapsedMs_ = 0;
    int     loadStablePolls_ = 0;
    int     lastTrackCount_ = -1;
    int     lastClipCount_ = -1;

    juce::String normalizedProjectPath_;
    juce::String actualLoadedProjectPath_;

    // ── Authoritative project counts ────────────────────────────────────
    int     projectFormatVersion_ = 0;
    double  sampleRate_ = 0.0;
    double  bpm_ = 0.0;
    int     engineTrackCount_ = 0;
    bool    masterTrackPresent_ = false;
    int     mutedTrackCount_ = 0;
    int     soloedTrackCount_ = 0;
    int     totalClipCount_ = 0;
    int     audioClipCount_ = 0;
    int     midiClipCount_ = 0;
    int     patternClipCount_ = 0;
    int     pluginInstanceCount_ = 0;

    // ── Waveform readiness ──────────────────────────────────────────────
    int     waveformReadyCount_ = 0;
    int     waveformNotReadyCount_ = 0;
    int     waveformWaitElapsedMs_ = 0;
    int     waveformWaitDurationMs_ = 0;
    bool    waveformReadinessConfirmed_ = false;

    // ── UI state at F9 trigger ──────────────────────────────────────────
    juce::Rectangle<int> windowBounds_;
    int     monitorIndex_ = -1;
    double  refreshRate_ = 0.0;
    double  dpiScale_ = 0.0;
    double  timelineZoom_ = 0.0;
    double  samplesPerPixel_ = 0.0;
    double  horizontalScroll_ = 0.0;
    int     verticalScroll_ = 0;
    double  visibleTimeStart_ = 0.0;
    double  visibleTimeEnd_ = 0.0;
    int     visibleTrackCount_ = 0;
    int     visibleClipCount_ = 0;
    int     viewportVisibleWidth_ = 0;
    int     viewportVisibleHeight_ = 0;
    bool    mixerVisible_ = false;
    bool    fxChainVisible_ = false;
    bool    bubblegumVisible_ = false;
    bool    masterStripFront_ = false;
    bool    transportPlaying_ = false;
    double  transportPosition_ = 0.0;

    // ── Start trigger ───────────────────────────────────────────────────
    // Set when startArmedRun() is called.  Possible values:
    //   "manual_f9"            — user pressed F9
    //   "benchmark_auto_start" — --benchmark-auto-start flag
    //   ""                     — not yet set (external_or_unknown)
    juce::String startTrigger_;
};

//==============================================================================
// Inline helper to extract benchmark-label and benchmark-project arguments.
// Handles both:
//   --benchmark-project "C:\path\with spaces.dawproj"
//   --benchmark-project="C:\path\with spaces.dawproj"
// ===============================================================================
inline juce::String extractBenchmarkProjectArg(const juce::String& cmdLine)
{
    auto idx = cmdLine.indexOf("--benchmark-project");
    if (idx < 0) return {};

    auto rest = cmdLine.substring(idx + 21).trimStart(); // 21 = len("--benchmark-project")

    // Handle --benchmark-project=<value> syntax
    if (rest.startsWithChar('='))
        rest = rest.substring(1).trimStart();

    // Handle quoted value
    if (rest.startsWithChar('"'))
    {
        auto end = rest.indexOf(1, "\"");
        return end > 0 ? rest.substring(1, end) : rest.substring(1);
    }

    // Unquoted value: go until next --flag or end of string
    auto nextArg = rest.indexOf(" --");
    return nextArg > 0 ? rest.substring(0, nextArg).trimEnd() : rest.trimEnd();
}

inline juce::String extractBenchmarkLabelArg(const juce::String& cmdLine)
{
    auto idx = cmdLine.indexOf("--benchmark-label");
    if (idx < 0) return {};

    auto rest = cmdLine.substring(idx + 17).trimStart(); // 17 = len("--benchmark-label")

    // Handle --benchmark-label=<value> syntax
    if (rest.startsWithChar('='))
        rest = rest.substring(1).trimStart();

    // Handle quoted value
    if (rest.startsWithChar('"'))
    {
        auto end = rest.indexOf(1, "\"");
        return end > 0 ? rest.substring(1, end) : rest.substring(1);
    }

    // Unquoted value: go until next --flag or end of string
    auto nextArg = rest.indexOf(" --");
    return nextArg > 0 ? rest.substring(0, nextArg).trimEnd() : rest.trimEnd();
}
