// ============================================================================
// FILE: AudioEngine_processTrackNode_patched.cpp
//
// PURPOSE
//   Drop-in replacement for the existing AudioEngine::processTrackNode()
//   method. Adds the InputTrim + InputMeter wiring that fixes the bug where
//   the input gain knob does not affect audio and the input VU meter does
//   not receive signal.
//
// HOW TO APPLY
//   1. Drop Source/InputMonitorCore/TrackInputProcessorCore.h into the
//      project (Batch 5 companion file).
//   2. In AudioEngine.cpp:
//        a. Add the include below near the existing InputMonitorCore includes:
//
//              #include "../InputMonitorCore/TrackInputProcessorCore.h"
//
//        b. Replace the entire processTrackNode() method body with the one
//           below.
//
//   3. In AudioEngine::prepareToPlay() (or wherever the engine prepares
//      per-track audio resources), add:
//
//        for (int i = 0; i < tracks_->getNumTracks(); ++i)
//            if (auto* t = tracks_->getTrack(i))
//                TrackInputProcessorCore::prepareTrack(*t, sampleRate, samplesPerBlock);
//        if (auto* m = tracks_->getMasterTrack())
//            TrackInputProcessorCore::prepareTrack(*m, sampleRate, samplesPerBlock);
//
//   4. In AudioEngine's TrackManager::Listener::trackAdded() handler:
//
//        TrackInputProcessorCore::prepareTrack(*track, currentSampleRate_,
//                                              currentBlockSize_);
//
//      (Adapt member names to whatever the engine stores them as.)
//
// THE ONLY DIFF FROM THE ORIGINAL
//   A new block of ~6 lines is inserted between "Mix in any incoming send
//   contributions" and "Capture pre-FX snapshot". The block is marked with
//   "INPUT TRIM + INPUT METER (Batch 5 wiring)" comments so it is easy to
//   find later. Everything else in the method is byte-identical to the
//   version pasted from the workspace.
// ============================================================================

void AudioEngine::processTrackNode(RoutingNode* node, int numSamples,
                                   bool isPlaying, SamplePosition position, bool anySolo,
                                   const FolderBusSnapshot* fbSnap)
{
    auto* track = tracks_->getTrack(node->trackId);
    if (!track) return;

    auto* nodeBufPtr = findNodeBuffer(node->id);
    if (nodeBufPtr == nullptr) return;

    // Render clips into node buffer
    // THREAD SAFETY: clips_ OwnedArray is protected by ClipManager::clipLock_.
    // Hold a ScopedTryLock for the entire getClipsOnTrack + renderClip loop so
    // no Clip* can be deleted mid-iteration on the message thread.
    // If the lock can't be obtained this block produces silence for this track (rare, acceptable).
    trackBuffer_.clear(0, numSamples);
    if (clips_)
    {
        juce::ScopedTryLock cl(clips_->getLock());
        if (cl.isLocked())
        {
            auto clipsOnTrack = clips_->getClipsOnTrack(track->getID());
            for (auto* clip : clipsOnTrack)
            {
                if (clip->isMuted()) continue;
                if (isPlaying)
                    renderClip(clip, position, numSamples);
            }
        }
        else
        {
            DBG("[AudioEngine] Skipped clip render for track " + track->getID()
                + ": clip manager locked by message thread");
        }
    }

    // Mix in any incoming send contributions accumulated into this node's buffer.
    // Upstream tracks that send to this track wrote into nodeBuffers_[node->id]
    // earlier in the topological processing pass. Add them to trackBuffer_ now
    // so they pass through this track's plugin chain, fader, and pan.
    {
        auto* incomingBufPtr = findNodeBuffer(node->id);
        if (incomingBufPtr == nullptr) return;
        auto& incomingBuf = *incomingBufPtr;
        auto* srcL = incomingBuf.getReadPointer(0);
        auto* srcR = incomingBuf.getReadPointer(juce::jmin(1, incomingBuf.getNumChannels() - 1));
        auto* dstL = trackBuffer_.getWritePointer(0);
        auto* dstR = trackBuffer_.getWritePointer(juce::jmin(1, trackBuffer_.getNumChannels() - 1));
        for (int s = 0; s < numSamples; ++s)
        {
            dstL[s] += srcL[s];
            dstR[s] += srcR[s];
        }
    }

    // ── INPUT TRIM + INPUT METER (Batch 5 wiring) ───────────────────────
    // Pre-FX input gain stage. Applies the per-track InputTrimCore to the
    // track buffer and updates the per-track InputMeterCore atomics. The UI
    // thread reads those atomics from the InputTrimFloatingPanel (and any
    // future panel that displays input levels for a track).
    //
    // Position is intentional: AFTER incoming sends mix in (so sends are
    // also subject to the input trim, treating trim as a true track input
    // gain), and BEFORE the preFx snapshot (so PreFX taps see the trimmed
    // signal — consistent with the trim being part of the input chain).
    {
        auto* tbL = trackBuffer_.getWritePointer(0);
        auto* tbR = trackBuffer_.getWritePointer(juce::jmin(1, trackBuffer_.getNumChannels() - 1));
        TrackInputProcessorCore::processTrack(*track, tbL, tbR, numSamples);
    }
    // ── END Batch 5 wiring ──────────────────────────────────────────────

    // Capture pre-FX snapshot before the plugin chain runs (for PreFX tap)
    if (preFxBuffer_.getNumSamples() < numSamples || preFxBuffer_.getNumChannels() < 2)
        preFxBuffer_.setSize(2, juce::jmax(numSamples, blockSize_));
    for (int ch = 0; ch < 2; ++ch)
        preFxBuffer_.copyFrom(ch, 0, trackBuffer_, ch, 0, numSamples);

    // MIDI playback: generate note events for MIDI/instrument tracks and feed to instrument plugin
    juce::MidiBuffer trackMidi;
    if (midiPlayback_ && (track->getRole() == TrackRole::MIDI || track->getRole() == TrackRole::Instrument))
    {
        PianoRollPlaybackCore::TransportInfo ti;
        ti.isPlaying          = isPlaying;
        ti.timelinePosSamples = (juce::int64) position;
        ti.numSamples         = numSamples;
        midiPlayback_->processBlock (track->getID(), ti, trackMidi);
    }
    if (midiInput_ && (track->getRole() == TrackRole::MIDI || track->getRole() == TrackRole::Instrument))
        midiInput_->processBlock(track->getID(), trackMidi, numSamples);
    // Virtual keyboard — always active for the selected MIDI track
    if (virtualKeyboard_ && track->getRole() == TrackRole::MIDI)
        virtualKeyboard_->processBlock (track->getID(), trackMidi, numSamples);

    // Plugin chain (inserts) — pass the sidechain buffer so processors
    // with a sidechain input bus can read it via processBlockWithSidechain().
    if (pluginChains_)
    {
        auto it = pluginChains_->find(track->getID());
        if (it != pluginChains_->end() && it->second)
        {
            auto* node2 = routing_->getNodeByTrackIdFast(track->getID());
            const bool hasMidiInput = (track->getRole() == TrackRole::MIDI || track->getRole() == TrackRole::Instrument) && !trackMidi.isEmpty();
            bool hasSidechainInput = false;
            if (node2 != nullptr)
            {
                const auto& sidechainInputs = routing_->getSidechainInputsRef(node2->id);
                hasSidechainInput = std::any_of(sidechainInputs.begin(),
                                                sidechainInputs.end(),
                                                [](RoutingConnection* conn)
                                                {
                                                    return conn != nullptr
                                                        && conn->active.load(std::memory_order_relaxed)
                                                        && !conn->bypassed.load(std::memory_order_relaxed);
                                                });
            }

            if (hasMidiInput)
                it->second->processBlockWithMidi(trackBuffer_, trackMidi, numSamples);

            else if (hasSidechainInput)
            {
                if (auto* sc = findSidechainBuffer(node2->id))
                    it->second->processBlockWithSidechain(trackBuffer_, *sc, numSamples);
                else
                    it->second->processBlock(trackBuffer_, numSamples);
            }
            else
                it->second->processBlock(trackBuffer_, numSamples);
        }
    }

    // Volume and pan — push current track values to the smoother.
    // The smoother generates per-sample gain ramps later in this block.
    auto& volumeRamp = getOrCreateVolumeRamp(track->getID());
    volumeRamp.setTargetVolume(track->getVolume());
    volumeRamp.setTargetPan(track->getPan());

    // Metering uses the smoother's current gain from the previous generated ramp.
    // A one-buffer visual lag is acceptable for meters and avoids double ramp work.
    const float leftGain  = volumeRamp.getCurrentLeftGain();
    const float rightGain = volumeRamp.getCurrentRightGain();

    // Peak metering — always update for visual display, even when muted
    float leftPeak  = trackBuffer_.getMagnitude(0, 0, numSamples) * leftGain;
    float rightPeak = trackBuffer_.getMagnitude(
        juce::jmin(1, trackBuffer_.getNumChannels() - 1), 0, numSamples) * rightGain;
    float displayLeft = juce::jmax(leftPeak, track->getPeakLevelLeft() * 0.92f);
    float displayRight = juce::jmax(rightPeak, track->getPeakLevelRight() * 0.92f);
    track->setPeakLevels(displayLeft, displayRight);

    // Mute/solo logic — snapshot-aware (FolderBus effective sets take precedence)
    bool audible;
    if (fbSnap)
    {
        bool effMuted = fbSnap->effectivelyMutedTracks.count(track->getID()) > 0;
        audible = !effMuted;
        if (!fbSnap->effectiveSoloSet.empty())
            audible = audible && fbSnap->effectiveSoloSet.count(track->getID()) > 0;
    }
    else
    {
        audible = !track->isMuted();
        if (anySolo) audible = track->isSoloed();
    }

    auto& muteFade = getOrCreateMuteFade(track->getID());
    const float* muteRamp = muteFade.generate(!audible, numSamples);
    if (!audible && muteFade.isFullyMuted()) return;

    // ── Pre-fader sends ──────────────────────────────────────────────
    const auto& outConns = routing_->getOutputConnectionsRef(node->id);
    for (auto* conn : outConns)
    {
        if (!conn->active.load(std::memory_order_relaxed) || conn->bypassed.load(std::memory_order_relaxed)) continue;
        if (conn->type != ConnectionType::PreSend) continue;

        auto* destBufPtr = findNodeBuffer(conn->destNodeId);
        if (destBufPtr == nullptr) continue;

        // Pre-fader send: signal before fader, with send gain
        const float* sendGain = getConnectionGainRamp(conn, numSamples);
        auto& destBuf = *destBufPtr;
        auto* srcL = trackBuffer_.getReadPointer(0);
        auto* srcR = trackBuffer_.getReadPointer(juce::jmin(1, trackBuffer_.getNumChannels() - 1));
        auto* dstL = destBuf.getWritePointer(0);
        auto* dstR = destBuf.getWritePointer(juce::jmin(1, destBuf.getNumChannels() - 1));
        for (int s = 0; s < numSamples; ++s)
        {
            dstL[s] += srcL[s] * sendGain[s] * muteRamp[s];
            dstR[s] += srcR[s] * sendGain[s] * muteRamp[s];
        }
    }

    // ── Apply gain to trackBuffer for post-fader routing ─────────────
    auto* tbL = trackBuffer_.getWritePointer(0);
    auto* tbR = trackBuffer_.getWritePointer(juce::jmin(1, trackBuffer_.getNumChannels() - 1));
    volumeRamp.applyToStereoBuffer(tbL, tbR, numSamples);
    for (int s = 0; s < numSamples; ++s)
    {
        tbL[s] *= muteRamp[s];
        tbR[s] *= muteRamp[s];
    }

    // ── Route to outputs (Direct + Post-fader sends) ────────────────
    for (auto* conn : outConns)
    {
        if (!conn->active.load(std::memory_order_relaxed) || conn->bypassed.load(std::memory_order_relaxed)) continue;
        if (conn->type == ConnectionType::PreSend || conn->type == ConnectionType::Sidechain)
            continue;

        auto* destBufPtr = findNodeBuffer(conn->destNodeId);
        if (destBufPtr == nullptr) continue;

        const float* routeGain = getConnectionGainRamp(conn, numSamples);
        auto& destBuf = *destBufPtr;
        const float* routeSrcL = tbL;
        const float* routeSrcR = tbR;
        if (conn->destNodeId == "master")
        {
            if ((int)masterPdcScratchL_.size() < numSamples)
            {
                masterPdcScratchL_.assign((size_t)numSamples, 0.0f);
                masterPdcScratchR_.assign((size_t)numSamples, 0.0f);
            }
            std::memcpy(masterPdcScratchL_.data(), tbL, sizeof(float) * (size_t)numSamples);
            std::memcpy(masterPdcScratchR_.data(), tbR, sizeof(float) * (size_t)numSamples);
            masterPdc_.processEdge(conn->id, masterPdcScratchL_.data(), masterPdcScratchR_.data(), numSamples);
            routeSrcL = masterPdcScratchL_.data();
            routeSrcR = masterPdcScratchR_.data();
        }
        auto* dstL = destBuf.getWritePointer(0);
        auto* dstR = destBuf.getWritePointer(juce::jmin(1, destBuf.getNumChannels() - 1));
        for (int s = 0; s < numSamples; ++s)
        {
            dstL[s] += routeSrcL[s] * routeGain[s];
            dstR[s] += routeSrcR[s] * routeGain[s];
        }
    }

    // ── Sidechain sends ──────────────────────────────────────────────
    for (auto* conn : outConns)
    {
        if (!conn->active.load(std::memory_order_relaxed) || conn->bypassed.load(std::memory_order_relaxed)) continue;
        if (conn->type != ConnectionType::Sidechain) continue;

        auto* scBufPtr = findSidechainBuffer(conn->destNodeId);
        if (scBufPtr == nullptr) continue;

        const float* scGain = getConnectionGainRamp(conn, numSamples);
        auto& scBuf  = *scBufPtr;
        auto* scL    = scBuf.getWritePointer(0);
        auto* scR    = scBuf.getWritePointer(juce::jmin(1, scBuf.getNumChannels() - 1));

        const float* tapL = nullptr;
        const float* tapR = nullptr;
        if (conn->tapPoint == TapPoint::PostMixer)
        {
            tapL = tbL;
            tapR = tbR;
        }
        else if (conn->tapPoint == TapPoint::PreFX)
        {
            tapL = preFxBuffer_.getReadPointer(0);
            tapR = preFxBuffer_.getReadPointer(juce::jmin(1, preFxBuffer_.getNumChannels() - 1));
        }
        else // PostFX — post-plugin, pre-fader
        {
            tapL = trackBuffer_.getReadPointer(0);
            tapR = trackBuffer_.getReadPointer(juce::jmin(1, trackBuffer_.getNumChannels() - 1));
        }

        auto pdcIt = pdcLines_.find(conn->destNodeId);
        if (pdcIt != pdcLines_.end() && pdcIt->second.delaySamples > 0)
        {
            auto& line = pdcIt->second;
            line.push(tapL, tapR, numSamples);
            if ((int)pdcScratchL_.size() < numSamples)
            {
                pdcScratchL_.assign(numSamples, 0.f);
                pdcScratchR_.assign(numSamples, 0.f);
            }
            line.read(pdcScratchL_.data(), pdcScratchR_.data(), numSamples);
            for (int s = 0; s < numSamples; ++s)
            {
                scL[s] += pdcScratchL_[s] * scGain[s];
                scR[s] += pdcScratchR_[s] * scGain[s];
            }
        }
        else
        {
            for (int s = 0; s < numSamples; ++s)
            {
                scL[s] += tapL[s] * scGain[s];
                scR[s] += tapR[s] * scGain[s];
            }
        }
    }
}
