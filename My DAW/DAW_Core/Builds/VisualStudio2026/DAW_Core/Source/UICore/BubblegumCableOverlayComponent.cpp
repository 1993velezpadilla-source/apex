// ...existing code...
            if (srcTrack)
            {
                const float peakLin = srcTrack->getPeakLevel();  // 0..1 linear
                // Try to get actual send gain; fall back to record.sendLevel01
                // (which is 1.0 for Direct/master, 0..1 for regular sends).
                float gain = bgV2_->sendLevel.getLevel(
                    *bgV2_->routingGraph,
                    record.sourceTrackId,
                    record.destinationTrackId);
                if (gain <= 0.0f)
                    gain = record.sendLevel01;  // covers Direct→Master cables
                // Normalise: regular send gain is 0..2 (unity=1), sendLevel01 is 0..1.
                // record.sendLevel01 for sends = gain*0.5, for Direct = 1.0.
                // Using record.sendLevel01 directly is already 0..1 normalised.
                audioSignal = juce::jlimit(0.0f, 1.0f, peakLin);
            }
// ...existing code...