// =============================================================================
//  ApexTunePitchTraceComponent.cpp
//  Full implementation: scale grid, waveform, pitch trace, color-coded note
//  blocks, drag-to-retune, double-click scale snap.
//
//  Drop-in: Source/VocalTuneUI/ApexTunePitchTraceComponent.cpp
//  REPLACES the Phase 6a stub.
//
//  Paint layer order (bottom -> top):
//   1. Background fill
//   2. Scale grid lines (every semitone; thicker on octaves; tinted on
//      in-scale degrees)
//   3. Waveform (faint)
//   4. Pitch trace (detected F0 line over time)
//   5. Note blocks (color-coded by detected deviation; sibilant tint;
//      selected ring)
//
//  Y math MUST match ApexTunePianoGridComponent::midiToY().
// =============================================================================

#include "ApexTunePitchTraceComponent.h"
#include "ApexTuneColors.h"
#include "../VocalTuneCore/ApexScaleSnapCore.h"
#include <algorithm>
#include <cmath>

namespace apex { namespace vocaltune {

// -----------------------------------------------------------------------------
ApexTunePitchTraceComponent::ApexTunePitchTraceComponent()
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

ApexTunePitchTraceComponent::~ApexTunePitchTraceComponent() = default;

// -----------------------------------------------------------------------------
void ApexTunePitchTraceComponent::setClipState (ApexTuneClipState* state)
{
    state_ = state;
    selectedNoteIndex_ = -1;
    draggedNoteIndex_  = -1;
    repaint();
}

void ApexTunePitchTraceComponent::setAnalysis (const ApexTuneAnalysis* analysis)
{
    analysis_ = analysis;
    repaint();
}

void ApexTunePitchTraceComponent::setSourceAudio (const float* mono, int numSamples, double sampleRate)
{
    sourceAudio_ = mono;
    sourceLen_   = numSamples;
    sourceSr_    = sampleRate;
    resized();   // rebuild waveform peaks at current width
    repaint();
}

void ApexTunePitchTraceComponent::setMidiRange (int lowMidi, int highMidi)
{
    if (highMidi <= lowMidi) return;
    lowMidi_  = lowMidi;
    highMidi_ = highMidi;
    repaint();
}

void ApexTunePitchTraceComponent::setScaleInfo (const juce::String& scaleRoot, const juce::String& scaleType)
{
    scaleRoot_ = scaleRoot;
    scaleType_ = scaleType;
    repaint();
}

void ApexTunePitchTraceComponent::setPlayheadSample (int64_t sample)
{
    if (playheadSample_ == sample) return;
    playheadSample_ = sample;
    repaint();
}

void ApexTunePitchTraceComponent::setRenderInProgress (bool inProgress)
{
    if (renderInProgress_ == inProgress) return;
    renderInProgress_ = inProgress;
    repaint();
}

bool ApexTunePitchTraceComponent::hasSelectedNotes() const
{
    return ! selectedNoteIndices_.empty();
}

bool ApexTunePitchTraceComponent::isNoteSelected (int noteIndex) const
{
    return noteIndexSelected (noteIndex);
}

std::vector<int> ApexTunePitchTraceComponent::getSelectedNoteIndices() const
{
    return selectedNoteIndices_;
}

void ApexTunePitchTraceComponent::selectAllNotes()
{
    if (state_ == nullptr) return;
    selectedNoteIndices_.clear();
    for (int i = 0; i < (int) state_->notes.size(); ++i)
        if (state_->notes[(size_t) i].voiced)
            selectedNoteIndices_.push_back (i);
    selectedNoteIndex_ = selectedNoteIndices_.empty() ? -1 : selectedNoteIndices_.front();
    if (onNoteSelected) onNoteSelected (selectedNoteIndex_);
    repaint();
}

void ApexTunePitchTraceComponent::clearSelectedNotes()
{
    clearSelection();
    if (onNoteSelected) onNoteSelected (-1);
    repaint();
}

bool ApexTunePitchTraceComponent::nudgeSelectedNotes (float deltaMidi, bool fine)
{
    if (state_ == nullptr) return false;
    if (selectedNoteIndices_.empty()) return false;

    const float scaled = fine ? deltaMidi * 0.1f : deltaMidi;
    bool changed = false;
    if (onNoteEditStarted) onNoteEditStarted();
    for (int idx : selectedNoteIndices_)
    {
        if (idx < 0 || idx >= (int) state_->notes.size()) continue;
        auto& note = state_->notes[(size_t) idx];
        const float newTarget = juce::jlimit ((float) lowMidi_, (float) highMidi_, note.targetMidi + scaled);
        if (std::abs (newTarget - note.targetMidi) < 1.0e-4f) continue;
        note.targetMidi = newTarget;
        note.centsOffset = (note.targetMidi - note.detectedMidi) * 100.0f;
        changed = true;
    }
    if (changed && onNoteEdited) onNoteEdited();
    if (changed) repaint();
    return changed;
}

bool ApexTunePitchTraceComponent::splitSelectedNotes()
{
    if (state_ == nullptr || selectedNoteIndices_.empty()) return false;

    auto indices = selectedNoteIndices_;
    std::sort (indices.begin(), indices.end(), std::greater<int>());

    bool changed = false;
    if (onNoteEditStarted) onNoteEditStarted();
    for (int idx : indices)
    {
        if (idx < 0 || idx >= (int) state_->notes.size()) continue;
        auto& note = state_->notes[(size_t) idx];
        const int64_t len = note.endSample - note.startSample;
        if (len < 8) continue;

        ApexTuneNote right = note;
        const int64_t mid = note.startSample + len / 2;
        note.endSample = mid;
        right.startSample = mid;
        right.noteId = note.noteId + "_b";
        state_->notes.insert (state_->notes.begin() + idx + 1, right);
        changed = true;
    }

    if (! changed) return false;

    selectedNoteIndices_.clear();
    for (int originalIdx : indices)
    {
        if (originalIdx < 0) continue;
        selectedNoteIndices_.push_back (originalIdx);
        if (originalIdx + 1 < (int) state_->notes.size())
            selectedNoteIndices_.push_back (originalIdx + 1);
    }
    std::sort (selectedNoteIndices_.begin(), selectedNoteIndices_.end());
    selectedNoteIndices_.erase (std::unique (selectedNoteIndices_.begin(), selectedNoteIndices_.end()), selectedNoteIndices_.end());
    selectedNoteIndex_ = selectedNoteIndices_.empty() ? -1 : selectedNoteIndices_.front();
    if (onNoteEdited) onNoteEdited();
    if (onNoteSelected) onNoteSelected (selectedNoteIndex_);
    repaint();
    return true;
}

bool ApexTunePitchTraceComponent::mergeSelectedNotes()
{
    if (state_ == nullptr || selectedNoteIndices_.size() < 2) return false;

    auto indices = selectedNoteIndices_;
    std::sort (indices.begin(), indices.end());
    indices.erase (std::unique (indices.begin(), indices.end()), indices.end());

    std::vector<ApexTuneNote> mergedNotes;
    mergedNotes.reserve (state_->notes.size());

    const int firstSelected = indices.front();
    const int lastSelected  = indices.back();
    for (size_t i = 1; i < indices.size(); ++i)
        if (indices[i] != indices[i - 1] + 1)
            return false;
    bool changed = false;

    if (onNoteEditStarted) onNoteEditStarted();
    for (int i = 0; i < (int) state_->notes.size(); ++i)
    {
        if (i == firstSelected)
        {
            ApexTuneNote merged = state_->notes[(size_t) i];
            for (int j = firstSelected + 1; j <= lastSelected; ++j)
            {
                if (std::find (indices.begin(), indices.end(), j) == indices.end())
                    continue;
                const auto& next = state_->notes[(size_t) j];
                merged.endSample = juce::jmax (merged.endSample, next.endSample);
                merged.targetMidi = (merged.targetMidi + next.targetMidi) * 0.5f;
                merged.detectedMidi = (merged.detectedMidi + next.detectedMidi) * 0.5f;
                merged.centsOffset = (merged.targetMidi - merged.detectedMidi) * 100.0f;
                merged.sibilantProtected = merged.sibilantProtected || next.sibilantProtected;
                changed = true;
            }
            mergedNotes.push_back (merged);
            i = lastSelected;
            continue;
        }

        if (std::find (indices.begin(), indices.end(), i) == indices.end())
            mergedNotes.push_back (state_->notes[(size_t) i]);
    }

    if (! changed) return false;

    state_->notes = std::move (mergedNotes);
    selectSingleNote (juce::jlimit (0, juce::jmax (0, (int) state_->notes.size() - 1), firstSelected));
    if (onNoteEdited) onNoteEdited();
    if (onNoteSelected) onNoteSelected (selectedNoteIndex_);
    repaint();
    return true;
}

// -----------------------------------------------------------------------------
//  Edge-trim hit testing.  Returns note index or -1.
//  isLeft is set to true if the left edge was hit, false for right.
// -----------------------------------------------------------------------------
int ApexTunePitchTraceComponent::findEdgeAt (juce::Point<float> p, bool& isLeft) const
{
    if (state_ == nullptr) return -1;
    constexpr float kEdgeZone = 7.0f;   // pixels

    for (int i = (int) state_->notes.size() - 1; i >= 0; --i)
    {
        const auto& n = state_->notes[(size_t) i];
        if (! n.voiced) continue;

        const float x1   = sampleToX (n.startSample);
        const float x2   = sampleToX (n.endSample);
        const float ytop = midiToY ((float) n.targetMidi + 0.5f);
        const float ybot = midiToY ((float) n.targetMidi - 0.5f);

        if (p.y < ytop || p.y > ybot) continue;

        if (std::abs (p.x - x1) <= kEdgeZone) { isLeft = true;  return i; }
        if (std::abs (p.x - x2) <= kEdgeZone) { isLeft = false; return i; }
    }
    return -1;
}

void ApexTunePitchTraceComponent::mouseMove (const juce::MouseEvent& e)
{
    if (state_ == nullptr) { setMouseCursor (juce::MouseCursor::NormalCursor); return; }
    bool isLeft = false;
    const int edge = findEdgeAt (e.position, isLeft);
    if (edge >= 0)
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (findNoteAt (e.position) >= 0)
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

// -----------------------------------------------------------------------------
//  Coordinate mapping. midiToY/yToMidi formula MUST match ApexTunePianoGrid.
// -----------------------------------------------------------------------------
float ApexTunePitchTraceComponent::midiToY (float midi) const noexcept
{
    const int   numRows = highMidi_ - lowMidi_ + 1;
    const float h       = (float) getHeight();
    if (numRows <= 0 || h <= 0.0f) return 0.0f;

    const float topMidi = (float) highMidi_ + 0.5f;
    const float botMidi = (float) lowMidi_  - 0.5f;
    const float frac    = (topMidi - midi) / (topMidi - botMidi);
    return h * juce::jlimit (0.0f, 1.0f, frac);
}

float ApexTunePitchTraceComponent::yToMidi (float y) const noexcept
{
    const int   numRows = highMidi_ - lowMidi_ + 1;
    const float h       = (float) getHeight();
    if (numRows <= 0 || h <= 0.0f) return (float) lowMidi_;

    const float topMidi = (float) highMidi_ + 0.5f;
    const float botMidi = (float) lowMidi_  - 0.5f;
    const float frac    = juce::jlimit (0.0f, 1.0f, y / h);
    return topMidi - frac * (topMidi - botMidi);
}

float ApexTunePitchTraceComponent::sampleToX (int64_t s) const noexcept
{
    if (sourceLen_ <= 0 || getWidth() <= 0) return 0.0f;
    const double fullFrac = juce::jlimit (0.0, 1.0, (double) s / (double) sourceLen_);
    const double visible = juce::jmax (0.001, (double) viewEndFrac_ - (double) viewStartFrac_);
    const double frac = (fullFrac - (double) viewStartFrac_) / visible;
    return (float) ((double) getWidth() * frac);
}

int64_t ApexTunePitchTraceComponent::xToSample (float x) const noexcept
{
    if (sourceLen_ <= 0 || getWidth() <= 0) return 0;
    const double visible = juce::jmax (0.001, (double) viewEndFrac_ - (double) viewStartFrac_);
    const double frac = juce::jlimit (0.0, 1.0, (double) viewStartFrac_ + ((double) x / (double) getWidth()) * visible);
    return (int64_t) (frac * (double) sourceLen_);
}

juce::Rectangle<float> ApexTunePitchTraceComponent::getScrollbarThumbBounds() const noexcept
{
    const float w = (float) getWidth();
    const float h = (float) getHeight();
    const float barH = 9.0f;
    const float y = h - barH - 2.0f;
    const float x = w * viewStartFrac_;
    const float thumbW = juce::jmax (24.0f, w * (viewEndFrac_ - viewStartFrac_));
    return { x, y, juce::jmin (thumbW, w - x), barH };
}

void ApexTunePitchTraceComponent::selectSingleNote (int noteIndex)
{
    selectedNoteIndex_ = noteIndex;
    selectedNoteIndices_.clear();
    if (noteIndex >= 0) selectedNoteIndices_.push_back (noteIndex);
}

void ApexTunePitchTraceComponent::toggleNoteSelection (int noteIndex)
{
    if (noteIndex < 0) return;
    auto it = std::find (selectedNoteIndices_.begin(), selectedNoteIndices_.end(), noteIndex);
    if (it != selectedNoteIndices_.end())
        selectedNoteIndices_.erase (it);
    else
        selectedNoteIndices_.push_back (noteIndex);

    selectedNoteIndex_ = selectedNoteIndices_.empty() ? -1 : selectedNoteIndices_.back();
}

void ApexTunePitchTraceComponent::clearSelection()
{
    selectedNoteIndex_ = -1;
    selectedNoteIndices_.clear();
}

bool ApexTunePitchTraceComponent::noteIndexSelected (int noteIndex) const
{
    return std::find (selectedNoteIndices_.begin(), selectedNoteIndices_.end(), noteIndex) != selectedNoteIndices_.end();
}

void ApexTunePitchTraceComponent::updateMarqueeSelection (bool additive)
{
    if (state_ == nullptr) return;
    const juce::Rectangle<float> marquee = juce::Rectangle<float>::leftTopRightBottom (
        juce::jmin (marqueeStart_.x, marqueeCurrent_.x),
        juce::jmin (marqueeStart_.y, marqueeCurrent_.y),
        juce::jmax (marqueeStart_.x, marqueeCurrent_.x),
        juce::jmax (marqueeStart_.y, marqueeCurrent_.y));

    std::vector<int> newSelection = additive ? selectedNoteIndices_ : std::vector<int>{};
    for (int i = 0; i < (int) state_->notes.size(); ++i)
    {
        const auto& n = state_->notes[(size_t) i];
        if (! n.voiced) continue;
        const juce::Rectangle<float> rect (sampleToX (n.startSample),
                                           midiToY ((float) n.targetMidi + 0.5f),
                                           juce::jmax (1.0f, sampleToX (n.endSample) - sampleToX (n.startSample)),
                                           juce::jmax (1.0f, midiToY ((float) n.targetMidi - 0.5f) - midiToY ((float) n.targetMidi + 0.5f)));
        if (marquee.intersects (rect) && std::find (newSelection.begin(), newSelection.end(), i) == newSelection.end())
            newSelection.push_back (i);
    }

    selectedNoteIndices_ = std::move (newSelection);
    selectedNoteIndex_ = selectedNoteIndices_.empty() ? -1 : selectedNoteIndices_.front();
}

// -----------------------------------------------------------------------------
int ApexTunePitchTraceComponent::findNoteAt (juce::Point<float> p) const
{
    if (state_ == nullptr) return -1;

    // Iterate in reverse so top-most note (last drawn) wins on overlaps.
    for (int i = (int) state_->notes.size() - 1; i >= 0; --i)
    {
        const auto& n = state_->notes[(size_t) i];
        if (! n.voiced) continue;

        const float x1   = sampleToX (n.startSample);
        const float x2   = sampleToX (n.endSample);
        const float ytop = midiToY ((float) n.targetMidi + 0.5f);
        const float ybot = midiToY ((float) n.targetMidi - 0.5f);

        if (p.x >= x1 && p.x <= x2 && p.y >= ytop && p.y <= ybot)
            return i;
    }
    return -1;
}

// -----------------------------------------------------------------------------
//  Waveform peaks: precomputed in resized() at current pixel resolution.
//  Recomputed when sourceAudio or width changes.
// -----------------------------------------------------------------------------
struct PeakPair { float minV; float maxV; };

// We store peaks as a member std::vector to avoid re-computing per paint.
// Forward-declared in the header would be nice but PeakPair is local-only;
// stash it in a file-static map keyed by 'this'. Simpler: re-compute on
// each paint -- waveform is cheap enough for V1.

// -----------------------------------------------------------------------------
//  Scale-grid helper. Returns true if the given pitch class is in the active
//  scale (relative to scaleRoot_).
// -----------------------------------------------------------------------------
namespace
{
    bool pitchClassInScale (int midi,
                            const juce::String& scaleRoot,
                            const juce::String& scaleType)
    {
        const auto degrees = ApexScaleSnapCore::degreesForScale (scaleType);
        if (degrees.empty()) return true;   // chromatic = every note in scale

        const int rootPc = ApexScaleSnapCore::pitchClassFromRoot (scaleRoot);
        if (rootPc < 0) return true;

        const int relPc = (((midi - rootPc) % 12) + 12) % 12;
        return std::find (degrees.begin(), degrees.end(), relPc) != degrees.end();
    }
}

// -----------------------------------------------------------------------------
void ApexTunePitchTraceComponent::paint (juce::Graphics& g)
{
    g.fillAll (Col::VocalTune::background());

    const float w = (float) getWidth();
    const float h = (float) getHeight();
    if (w <= 0.0f || h <= 0.0f) return;

    // Minimap / overview strip for full clip navigation context.
    if (sourceAudio_ != nullptr && sourceLen_ > 0 && w >= 1.0f)
    {
        const juce::Rectangle<float> mini (0.0f, 2.0f, w, 18.0f);
        g.setColour (Col::VocalTune::toolbarBg().withAlpha (0.70f));
        g.fillRoundedRectangle (mini, 3.0f);
        g.setColour (Col::VocalTune::waveform().withAlpha (0.70f));
        const float miniMidY = mini.getCentreY();
        const float miniScale = mini.getHeight() * 0.42f;
        for (int x = 0; x < (int) w; ++x)
        {
            const int64_t s0 = (int64_t) x * (int64_t) sourceLen_ / (int64_t) juce::jmax (1, (int) w);
            const int64_t s1 = (int64_t) (x + 1) * (int64_t) sourceLen_ / (int64_t) juce::jmax (1, (int) w);
            float peak = 0.0f;
            for (int64_t s = s0; s < juce::jmin ((int64_t) sourceLen_, s1); ++s)
                peak = juce::jmax (peak, std::abs (sourceAudio_[s]));
            g.drawLine ((float) x, miniMidY - peak * miniScale, (float) x, miniMidY + peak * miniScale);
        }
        const juce::Rectangle<float> visible (w * viewStartFrac_, mini.getY(), w * (viewEndFrac_ - viewStartFrac_), mini.getHeight());
        g.setColour (Col::VocalTune::noteSelectedRing().withAlpha (0.20f));
        g.fillRoundedRectangle (visible, 3.0f);
        g.setColour (Col::VocalTune::noteSelectedRing().withAlpha (0.55f));
        g.drawRoundedRectangle (visible, 3.0f, 1.0f);
    }

    // -------------------------------------------------------------------------
    // 1. Scale grid lines.
    // -------------------------------------------------------------------------
    for (int midi = lowMidi_; midi <= highMidi_; ++midi)
    {
        const float y      = midiToY ((float) midi);
        const int   pc     = ((midi % 12) + 12) % 12;
        const bool  octave = (pc == 0);
        const bool  inScale = pitchClassInScale (midi, scaleRoot_, scaleType_);

        juce::Colour col;
        if (octave)        col = Col::VocalTune::scaleLineOctave();
        else if (inScale)  col = Col::VocalTune::scaleLine();
        else               col = Col::VocalTune::scaleLine().withAlpha (0.25f);

        g.setColour (col);
        g.drawHorizontalLine ((int) y, 0.0f, w);
    }

    // -------------------------------------------------------------------------
    // 2. Waveform (faint, behind everything).
    // -------------------------------------------------------------------------
    if (sourceAudio_ != nullptr && sourceLen_ > 0 && w >= 1.0f)
    {
        g.setColour (Col::VocalTune::waveform());

        const int pxCols = (int) w;
        const float midY  = h * 0.5f;
        const float scale = h * 0.18f;   // waveform amplitude reaches ~36% of height

        for (int x = 0; x < pxCols; ++x)
        {
            const int64_t s0 = xToSample ((float) x);
            const int64_t s1 = xToSample ((float) x + 1.0f);
            const int64_t end = juce::jmin ((int64_t) sourceLen_, s1);
            if (s0 >= end) continue;

            float minV = 0.0f, maxV = 0.0f;
            for (int64_t i = s0; i < end; ++i)
            {
                const float v = sourceAudio_[i];
                if (v < minV) minV = v;
                if (v > maxV) maxV = v;
            }
            g.drawLine ((float) x, midY - maxV * scale,
                        (float) x, midY - minV * scale);
        }
    }

    // -------------------------------------------------------------------------
    // 3. Detected pitch trace.
    // -------------------------------------------------------------------------
    if (analysis_ != nullptr && analysis_->hopSamples > 0)
    {
        juce::Path path;
        bool hasPoint = false;

        for (size_t i = 0; i < analysis_->frames.size(); ++i)
        {
            const auto& f = analysis_->frames[i];
            if (! f.voiced || f.f0Hz <= 0.0f)
            {
                hasPoint = false;
                continue;
            }

            const int64_t samplePos = (int64_t) i * (int64_t) analysis_->hopSamples;
            const float   x = sampleToX (samplePos);
            const float   y = midiToY (hzToMidi (f.f0Hz));

            if (! hasPoint) { path.startNewSubPath (x, y); hasPoint = true; }
            else            { path.lineTo (x, y); }
        }

        g.setColour (Col::VocalTune::pitchTrace());
        g.strokePath (path, juce::PathStrokeType (1.4f));
    }

    // -------------------------------------------------------------------------
    // 4. Note blocks (color-coded by detected deviation, sibilant tint,
    //    selected ring).
    // -------------------------------------------------------------------------
    if (state_ != nullptr)
    {
        // Pitch transition curves between adjacent notes.
        for (size_t i = 1; i < state_->notes.size(); ++i)
        {
            const auto& a = state_->notes[i - 1];
            const auto& b = state_->notes[i];
            if (! a.voiced || ! b.voiced) continue;
            const int64_t gap = b.startSample - a.endSample;
            if (gap < 0 || gap > (int64_t) (sourceSr_ * 0.25)) continue;
            const float x1 = sampleToX (a.endSample);
            const float y1 = midiToY (a.targetMidi);
            const float x2 = sampleToX (b.startSample);
            const float y2 = midiToY (b.targetMidi);
            juce::Path transition;
            transition.startNewSubPath (x1, y1);
            transition.cubicTo (x1 + (x2 - x1) * 0.35f, y1,
                                x1 + (x2 - x1) * 0.65f, y2,
                                x2, y2);
            g.setColour (Col::VocalTune::targetLine().withAlpha (0.50f));
            g.strokePath (transition, juce::PathStrokeType (1.0f));
        }

        for (size_t i = 0; i < state_->notes.size(); ++i)
        {
            const auto& n = state_->notes[i];
            if (! n.voiced) continue;

            const float x1   = sampleToX (n.startSample);
            const float x2   = sampleToX (n.endSample);
            const float ytop = midiToY ((float) n.targetMidi + 0.5f);
            const float ybot = midiToY ((float) n.targetMidi - 0.5f);
            const juce::Rectangle<float> rect (x1, ytop,
                                               juce::jmax (1.0f, x2 - x1),
                                               juce::jmax (1.0f, ybot - ytop));

            // Color reflects the ORIGINAL detected deviation from in-tune,
            // so the user can see at a glance how off-pitch each note was.
            const float absCents
                = std::abs ((n.detectedMidi - std::round (n.detectedMidi)) * 100.0f);
            juce::Colour col = Col::VocalTune::noteForCentsOff (absCents);

            // Sibilant-protected notes get a purple tint.
            if (n.sibilantProtected)
                col = col.interpolatedWith (Col::VocalTune::noteSibilantTint(), 0.45f);

            const bool selected = noteIndexSelected ((int) i);
            g.setColour (selected ? col.brighter (0.18f).withAlpha (0.92f)
                                  : col.withAlpha (0.78f));
            g.fillRoundedRectangle (rect, 2.0f);

            // ---- Drift tint: stronger left-to-right fade when driftAmount < 1 ----
            if (n.driftAmount < 0.98f && rect.getWidth() > 4.0f)
            {
                const float fadeAlpha = (1.0f - n.driftAmount) * 0.28f;
                juce::ColourGradient driftGrad (
                    juce::Colours::transparentBlack,            rect.getX(), 0.0f,
                    juce::Colours::white.withAlpha (fadeAlpha), rect.getRight(), 0.0f,
                    false);
                g.setGradientFill (driftGrad);
                g.fillRoundedRectangle (rect, 2.0f);
                g.setColour (juce::Colours::transparentBlack);  // reset
            }

            // ---- Modulation wavy line: amplitude scales with modulationAmount ----
            if (n.modulationAmount > 0.05f && rect.getWidth() > 8.0f)
            {
                const float waveAmp   = juce::jlimit (0.5f, 3.5f, n.modulationAmount * 3.0f);
                const float wavePeriod = 14.0f;
                const float ycenter   = rect.getCentreY();
                juce::Path wave;
                bool waveStarted = false;
                for (float px = rect.getX(); px <= rect.getRight(); px += 1.0f)
                {
                    const float phase = (px - rect.getX()) / wavePeriod * juce::MathConstants<float>::twoPi;
                    const float yw    = ycenter + std::sin (phase) * waveAmp;
                    if (! waveStarted) { wave.startNewSubPath (px, yw); waveStarted = true; }
                    else                 wave.lineTo (px, yw);
                }
                g.setColour (juce::Colours::white.withAlpha (0.22f));
                g.strokePath (wave, juce::PathStrokeType (1.0f));
            }

            g.setColour (Col::VocalTune::noteOutline());
            g.drawRoundedRectangle (rect, 2.0f, selected ? 1.5f : 1.0f);

            // Faint marker at original detectedMidi if target was moved.
            if (std::abs (n.targetMidi - n.detectedMidi) > 0.05f)
            {
                const float detY = midiToY (n.detectedMidi);
                g.setColour (Col::VocalTune::targetLine());
                g.drawHorizontalLine ((int) detY, x1, x2);
            }

            // Selected ring on top
            if (selected)
            {
                g.setColour (Col::VocalTune::noteSelectedRing().withAlpha (0.95f));
                g.drawRoundedRectangle (rect.expanded (1.0f), 2.5f, 1.5f);
            }

            // Trim-edge handles: subtle bright lines on left/right edges
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawVerticalLine ((int) rect.getX(),     rect.getY(), rect.getBottom());
            g.drawVerticalLine ((int) rect.getRight(), rect.getY(), rect.getBottom());
        }
    }

    if (playheadSample_ >= 0 && sourceLen_ > 0)
    {
        const float x = sampleToX (playheadSample_);
        if (x >= 0.0f && x <= w)
        {
            g.setColour (Col::VocalTune::playhead());
            g.drawVerticalLine ((int) std::round (x), 0.0f, h);
        }
    }

    if (marqueeActive_)
    {
        const juce::Rectangle<float> marquee = juce::Rectangle<float>::leftTopRightBottom (
            juce::jmin (marqueeStart_.x, marqueeCurrent_.x),
            juce::jmin (marqueeStart_.y, marqueeCurrent_.y),
            juce::jmax (marqueeStart_.x, marqueeCurrent_.x),
            juce::jmax (marqueeStart_.y, marqueeCurrent_.y));
        g.setColour (Col::VocalTune::noteSelectedRing().withAlpha (0.12f));
        g.fillRect (marquee);
        g.setColour (Col::VocalTune::noteSelectedRing().withAlpha (0.65f));
        g.drawRect (marquee, 1.0f);
    }

    // Visible horizontal scrollbar / zoom locator
    const auto thumb = getScrollbarThumbBounds();
    g.setColour (Col::VocalTune::panelDivider().withAlpha (0.45f));
    g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, thumb.getY(), w, thumb.getHeight()), 3.0f);
    g.setColour (Col::VocalTune::noteSelectedRing().withAlpha (0.55f));
    g.fillRoundedRectangle (thumb, 3.0f);

    if (renderInProgress_)
    {
        const juce::Rectangle<float> badge (w - 126.0f, 8.0f, 118.0f, 22.0f);
        g.setColour (Col::VocalTune::toolbarBg().withAlpha (0.88f));
        g.fillRoundedRectangle (badge, 5.0f);
        g.setColour (Col::VocalTune::noteSlight());
        g.drawRoundedRectangle (badge, 5.0f, 1.0f);
        g.setColour (Col::VocalTune::text());
        g.setFont (juce::Font (12.0f, juce::Font::plain));
        g.drawText ("Rendering...", badge, juce::Justification::centred, false);
    }
}

// -----------------------------------------------------------------------------
void ApexTunePitchTraceComponent::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0) return;
    // V1: nothing to lay out -- painting handles everything.
}

// -----------------------------------------------------------------------------
//  Mouse interaction.
// -----------------------------------------------------------------------------
void ApexTunePitchTraceComponent::mouseDown (const juce::MouseEvent& e)
{
    if (state_ == nullptr) return;

    if (getScrollbarThumbBounds().contains (e.position))
    {
        dragMode_ = DragMode::Scrollbar;
        scrollbarDragStartX_ = e.position.x;
        scrollbarDragStartViewStart_ = viewStartFrac_;
        scrollbarDragStartViewEnd_ = viewEndFrac_;
        return;
    }

    // ---- Edge trim takes priority over everything ----
    bool isLeft = false;
    const int edgeIdx = findEdgeAt (e.position, isLeft);
    if (edgeIdx >= 0)
    {
        if (onNoteEditStarted) onNoteEditStarted();
        dragMode_      = isLeft ? DragMode::TrimLeft : DragMode::TrimRight;
        trimNoteIndex_ = edgeIdx;
        trimOrigStart_ = state_->notes[(size_t) edgeIdx].startSample;
        trimOrigEnd_   = state_->notes[(size_t) edgeIdx].endSample;
        trimStartX_    = e.position.x;
        selectSingleNote (edgeIdx);
        if (onNoteSelected) onNoteSelected (selectedNoteIndex_);
        repaint();
        return;
    }

    const int idx = findNoteAt (e.position);
    if (idx < 0)
    {
        dragMode_ = DragMode::Marquee;
        marqueeActive_ = true;
        marqueeStart_ = e.position;
        marqueeCurrent_ = e.position;
        if (! (e.mods.isCommandDown() || e.mods.isCtrlDown() || e.mods.isShiftDown()))
            clearSelection();
        if (onNoteSelected) onNoteSelected (selectedNoteIndex_);
        repaint();
        return;
    }

    dragMode_ = e.mods.isAltDown() ? DragMode::NoteTime : DragMode::NotePitch;
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        toggleNoteSelection (idx);
    else if (e.mods.isShiftDown() && selectedNoteIndex_ >= 0)
    {
        const int a = juce::jmin (selectedNoteIndex_, idx);
        const int b = juce::jmax (selectedNoteIndex_, idx);
        selectedNoteIndices_.clear();
        for (int i = a; i <= b; ++i) selectedNoteIndices_.push_back (i);
        selectedNoteIndex_ = idx;
    }
    else if (! noteIndexSelected (idx))
        selectSingleNote (idx);

    draggedNoteIndex_      = idx;
    dragStartTargetMidi_   = state_->notes[(size_t) idx].targetMidi;
    dragStartMouseMidi_    = yToMidi (e.position.y);
    dragUndoStarted_       = false;
    dragStartTargetMidis_.clear();
    dragStartNoteStarts_.clear();
    dragStartNoteEnds_.clear();
    for (int selected : selectedNoteIndices_)
        if (selected >= 0 && selected < (int) state_->notes.size())
        {
            dragStartTargetMidis_.push_back (state_->notes[(size_t) selected].targetMidi);
            dragStartNoteStarts_.push_back (state_->notes[(size_t) selected].startSample);
            dragStartNoteEnds_.push_back (state_->notes[(size_t) selected].endSample);
        }
    timeDragStartX_ = e.position.x;

    if (onNoteSelected) onNoteSelected (selectedNoteIndex_);
    repaint();
}

// -----------------------------------------------------------------------------
void ApexTunePitchTraceComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode_ == DragMode::Scrollbar)
    {
        const float visible = scrollbarDragStartViewEnd_ - scrollbarDragStartViewStart_;
        const float dxFrac = (e.position.x - scrollbarDragStartX_) / (float) juce::jmax (1, getWidth());
        viewStartFrac_ = scrollbarDragStartViewStart_ + dxFrac;
        viewEndFrac_ = viewStartFrac_ + visible;
        if (viewStartFrac_ < 0.0f) { viewEndFrac_ -= viewStartFrac_; viewStartFrac_ = 0.0f; }
        if (viewEndFrac_ > 1.0f) { const float over = viewEndFrac_ - 1.0f; viewStartFrac_ -= over; viewEndFrac_ = 1.0f; }
        repaint();
        return;
    }

    if (dragMode_ == DragMode::TrimLeft || dragMode_ == DragMode::TrimRight)
    {
        if (state_ == nullptr || trimNoteIndex_ < 0 || trimNoteIndex_ >= (int) state_->notes.size()) return;
        auto& note = state_->notes[(size_t) trimNoteIndex_];
        const float dx = e.position.x - trimStartX_;
        const int64_t deltaSamples = xToSample (std::max (0.0f, trimStartX_ + dx)) - xToSample (trimStartX_);

        if (dragMode_ == DragMode::TrimLeft)
        {
            const int64_t newStart = juce::jlimit ((int64_t) 0, trimOrigEnd_ - 8, trimOrigStart_ + deltaSamples);
            note.startSample = newStart;
        }
        else
        {
            const int64_t newEnd = juce::jlimit (trimOrigStart_ + 8, (int64_t) sourceLen_, trimOrigEnd_ + deltaSamples);
            note.endSample = newEnd;
        }
        if (onNoteEdited) onNoteEdited();
        repaint();
        return;
    }

    if (dragMode_ == DragMode::Marquee)
    {
        marqueeCurrent_ = e.position;
        updateMarqueeSelection (e.mods.isCommandDown() || e.mods.isCtrlDown() || e.mods.isShiftDown());
        if (onNoteSelected) onNoteSelected (selectedNoteIndex_);
        repaint();
        return;
    }

    if (state_ == nullptr || draggedNoteIndex_ < 0) return;
    if (draggedNoteIndex_ >= (int) state_->notes.size()) return;

    if (dragMode_ == DragMode::NoteTime)
    {
        bool changed = false;
        const int64_t deltaSamples = xToSample (e.position.x) - xToSample (timeDragStartX_);
        if (selectedNoteIndices_.empty()) selectSingleNote (draggedNoteIndex_);
        for (size_t i = 0; i < selectedNoteIndices_.size(); ++i)
        {
            const int idx = selectedNoteIndices_[i];
            if (idx < 0 || idx >= (int) state_->notes.size()) continue;
            auto& note = state_->notes[(size_t) idx];
            const int64_t start0 = i < dragStartNoteStarts_.size() ? dragStartNoteStarts_[i] : note.startSample;
            const int64_t end0   = i < dragStartNoteEnds_.size()   ? dragStartNoteEnds_[i]   : note.endSample;
            const int64_t len = juce::jmax ((int64_t) 8, end0 - start0);
            const int64_t newStart = juce::jlimit ((int64_t) 0, (int64_t) sourceLen_ - len, start0 + deltaSamples);
            if (newStart == note.startSample) continue;
            if (! changed && ! dragUndoStarted_ && onNoteEditStarted)
            {
                onNoteEditStarted();
                dragUndoStarted_ = true;
            }
            note.startSample = newStart;
            note.endSample = newStart + len;
            changed = true;
        }
        if (changed && onNoteEdited) onNoteEdited();
        repaint();
        return;
    }

    const float currentMouseMidi = yToMidi (e.position.y);
    const float delta            = currentMouseMidi - dragStartMouseMidi_;

    // Shift held = fine drag (cents-level precision)
    const bool fine = e.mods.isShiftDown();
    const float scaledDelta = fine ? delta * 0.10f : delta;

    bool changed = false;
    if (selectedNoteIndices_.empty()) selectSingleNote (draggedNoteIndex_);
    for (size_t i = 0; i < selectedNoteIndices_.size(); ++i)
    {
        const int idx = selectedNoteIndices_[i];
        if (idx < 0 || idx >= (int) state_->notes.size()) continue;

        auto& note = state_->notes[(size_t) idx];
        const float startTarget = i < dragStartTargetMidis_.size()
            ? dragStartTargetMidis_[i]
            : note.targetMidi;
        const float newTarget = juce::jlimit ((float) lowMidi_,
                                              (float) highMidi_,
                                              startTarget + scaledDelta);

        if (std::abs (newTarget - note.targetMidi) < 1.0e-4f) continue;

        if (! changed && ! dragUndoStarted_ && onNoteEditStarted)
        {
            onNoteEditStarted();
            dragUndoStarted_ = true;
        }
        note.targetMidi  = newTarget;
        note.centsOffset = (note.targetMidi - note.detectedMidi) * 100.0f;
        changed = true;
    }

    if (changed && onNoteEdited) onNoteEdited();
    repaint();
}

// -----------------------------------------------------------------------------
void ApexTunePitchTraceComponent::mouseUp (const juce::MouseEvent&)
{
    if (dragMode_ == DragMode::Marquee || marqueeActive_)
    {
        marqueeActive_ = false;
        dragMode_ = DragMode::None;
        repaint();
        return;
    }
    dragMode_ = DragMode::None;
    trimNoteIndex_ = -1;
    draggedNoteIndex_ = -1;
    dragUndoStarted_ = false;
    dragStartTargetMidis_.clear();
    dragStartNoteStarts_.clear();
    dragStartNoteEnds_.clear();
}

// -----------------------------------------------------------------------------
void ApexTunePitchTraceComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (state_ == nullptr) return;

    const int idx = findNoteAt (e.position);
    if (idx < 0) return;

    auto& note = state_->notes[(size_t) idx];
    if (onNoteEditStarted) onNoteEditStarted();
    ApexScaleSnapCore::snapNote (note, scaleRoot_, scaleType_);

    selectSingleNote (idx);
    if (onNoteSelected) onNoteSelected (selectedNoteIndex_);
    if (onNoteEdited)   onNoteEdited();
    repaint();
}

// -----------------------------------------------------------------------------
void ApexTunePitchTraceComponent::mouseWheelMove (const juce::MouseEvent& e,
                                                  const juce::MouseWheelDetails& wheel)
{
    if (sourceLen_ <= 0) return;

    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const float centerMidi = yToMidi (e.position.y);
        const int currentRange = juce::jmax (12, highMidi_ - lowMidi_);
        const int newRange = juce::jlimit (12, 84,
            currentRange + (wheel.deltaY > 0.0f ? -4 : 4));
        lowMidi_ = juce::jlimit (0, 127 - newRange, (int) std::round (centerMidi - newRange * 0.5f));
        highMidi_ = juce::jlimit (lowMidi_ + 1, 127, lowMidi_ + newRange);
        repaint();
        return;
    }

    const float visible = juce::jlimit (0.02f, 1.0f, viewEndFrac_ - viewStartFrac_);
    if (std::abs (wheel.deltaY) >= std::abs (wheel.deltaX))
    {
        const float mouseFrac = juce::jlimit (0.0f, 1.0f, e.position.x / (float) juce::jmax (1, getWidth()));
        const float anchor = viewStartFrac_ + mouseFrac * visible;
        const float zoomFactor = wheel.deltaY > 0.0f ? 0.82f : 1.22f;
        const float newVisible = juce::jlimit (0.02f, 1.0f, visible * zoomFactor);
        viewStartFrac_ = anchor - mouseFrac * newVisible;
        viewEndFrac_ = viewStartFrac_ + newVisible;
    }
    else
    {
        const float shift = -wheel.deltaX * visible;
        viewStartFrac_ += shift;
        viewEndFrac_ += shift;
    }

    if (viewStartFrac_ < 0.0f)
    {
        viewEndFrac_ -= viewStartFrac_;
        viewStartFrac_ = 0.0f;
    }
    if (viewEndFrac_ > 1.0f)
    {
        const float over = viewEndFrac_ - 1.0f;
        viewStartFrac_ -= over;
        viewEndFrac_ = 1.0f;
    }
    viewStartFrac_ = juce::jlimit (0.0f, 0.98f, viewStartFrac_);
    viewEndFrac_ = juce::jlimit (viewStartFrac_ + 0.02f, 1.0f, viewEndFrac_);
    repaint();
}

}} // namespace apex::vocaltune
