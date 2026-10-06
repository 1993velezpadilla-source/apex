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
    const double frac = (double) s / (double) sourceLen_;
    return (float) ((double) getWidth() * juce::jlimit (0.0, 1.0, frac));
}

int64_t ApexTunePitchTraceComponent::xToSample (float x) const noexcept
{
    if (sourceLen_ <= 0 || getWidth() <= 0) return 0;
    const double frac = juce::jlimit (0.0, 1.0, (double) x / (double) getWidth());
    return (int64_t) (frac * (double) sourceLen_);
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
            const int64_t s0 = (int64_t) x       * (int64_t) sourceLen_ / (int64_t) pxCols;
            const int64_t s1 = (int64_t) (x + 1) * (int64_t) sourceLen_ / (int64_t) pxCols;
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

            g.setColour (col.withAlpha (0.78f));
            g.fillRect (rect);

            g.setColour (Col::VocalTune::noteOutline());
            g.drawRect (rect, 1.0f);

            // Faint marker at original detectedMidi if target was moved.
            if (std::abs (n.targetMidi - n.detectedMidi) > 0.05f)
            {
                const float detY = midiToY (n.detectedMidi);
                g.setColour (Col::VocalTune::targetLine());
                g.drawHorizontalLine ((int) detY, x1, x2);
            }

            // Selected ring on top
            if ((int) i == selectedNoteIndex_)
            {
                g.setColour (Col::VocalTune::noteSelectedRing());
                g.drawRect (rect, 2.0f);
            }
        }
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

    const int idx = findNoteAt (e.position);
    if (idx < 0)
    {
        if (selectedNoteIndex_ != -1)
        {
            selectedNoteIndex_ = -1;
            if (onNoteSelected) onNoteSelected (-1);
            repaint();
        }
        return;
    }

    selectedNoteIndex_     = idx;
    draggedNoteIndex_      = idx;
    dragStartTargetMidi_   = state_->notes[(size_t) idx].targetMidi;
    dragStartMouseMidi_    = yToMidi (e.position.y);

    if (onNoteSelected) onNoteSelected (idx);
    repaint();
}

// -----------------------------------------------------------------------------
void ApexTunePitchTraceComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (state_ == nullptr || draggedNoteIndex_ < 0) return;
    if (draggedNoteIndex_ >= (int) state_->notes.size()) return;

    auto& note = state_->notes[(size_t) draggedNoteIndex_];

    const float currentMouseMidi = yToMidi (e.position.y);
    const float delta            = currentMouseMidi - dragStartMouseMidi_;

    // Shift held = fine drag (cents-level precision)
    const bool fine = e.mods.isShiftDown();
    const float scaledDelta = fine ? delta * 0.10f : delta;

    const float newTarget = juce::jlimit ((float) lowMidi_,
                                          (float) highMidi_,
                                          dragStartTargetMidi_ + scaledDelta);

    if (std::abs (newTarget - note.targetMidi) < 1.0e-4f) return;

    note.targetMidi  = newTarget;
    note.centsOffset = (note.targetMidi - note.detectedMidi) * 100.0f;

    if (onNoteEdited) onNoteEdited();
    repaint();
}

// -----------------------------------------------------------------------------
void ApexTunePitchTraceComponent::mouseUp (const juce::MouseEvent&)
{
    draggedNoteIndex_ = -1;
}

// -----------------------------------------------------------------------------
void ApexTunePitchTraceComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (state_ == nullptr) return;

    const int idx = findNoteAt (e.position);
    if (idx < 0) return;

    auto& note = state_->notes[(size_t) idx];
    ApexScaleSnapCore::snapNote (note, scaleRoot_, scaleType_);

    selectedNoteIndex_ = idx;
    if (onNoteSelected) onNoteSelected (idx);
    if (onNoteEdited)   onNoteEdited();
    repaint();
}

}} // namespace apex::vocaltune
