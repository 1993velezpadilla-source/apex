#include "PianoRollComponent.h"
#include "PianoRollScaleHelper.h"
#include "../ThemeCore/Theme.h"
#include "../UICore/CursorThemeCore.h"
#include "../CommandCore/CommandManager.h"

namespace DAW {

PianoRollComponent::PianoRollComponent()
    : gridViewport_(*this, gridRenderer_)
{
    addAndMakeVisible(keyboard_);
    addAndMakeVisible(ruler_);
    addAndMakeVisible(gridViewport_);
    addAndMakeVisible(horizontalScrollBar_);
    addAndMakeVisible(verticalScrollBar_);

    horizontalScrollBar_.addListener(this);
    verticalScrollBar_.addListener(this);
    horizontalScrollBar_.setAutoHide(false);
    verticalScrollBar_.setAutoHide(false);

    startTimerHz(60);   // playhead repaint + scroll follow — 60 Hz for smooth playhead
    updateRenderState();
}

void PianoRollComponent::GridViewportComponent::paint(juce::Graphics& g)
{
    renderer_.paint(g, getLocalBounds());
    owner_.noteRenderer_.paint(g, getLocalBounds(), owner_.clipModel_);
    owner_.selectionRenderer_.paint(g, owner_.marqueeRect_);
    owner_.paintPlayhead(g);
}

void PianoRollComponent::GridViewportComponent::mouseDown(const juce::MouseEvent& e)
{
    owner_.handleGridMouseDown(e);
}

void PianoRollComponent::GridViewportComponent::mouseDrag(const juce::MouseEvent& e)
{
    owner_.handleGridMouseDrag(e);
}

void PianoRollComponent::GridViewportComponent::mouseUp(const juce::MouseEvent& e)
{
    owner_.handleGridMouseUp(e);
}

void PianoRollComponent::GridViewportComponent::mouseMove(const juce::MouseEvent& e)
{
    owner_.handleGridMouseMove(e);
}

bool PianoRollComponent::GridViewportComponent::keyPressed(const juce::KeyPress& key)
{
    return owner_.handleGridKeyPress(key);
}

PianoRollComponent::~PianoRollComponent()
{
    horizontalScrollBar_.removeListener(this);
    verticalScrollBar_.removeListener(this);
}

void PianoRollComponent::setClipModel(PianoRollClipModel* model)
{
    clipModel_ = model;
    updateRenderState();
    repaint();
}

void PianoRollComponent::setTicksPerGridDivision(juce::int64 ticks)
{
    ticksPerGridDivision_ = juce::jmax<juce::int64>(1, ticks);
    updateRenderState();
    repaint();
}

void PianoRollComponent::setSnapEnabled(bool enabled)
{
    if (snapEnabled_ == enabled)
        return;

    snapEnabled_ = enabled;
    repaint();
}

void PianoRollComponent::setScaleSnapEnabled(bool enabled)
{
    if (scaleSnapEnabled_ == enabled)
        return;

    scaleSnapEnabled_ = enabled;
    repaint();
}

void PianoRollComponent::setNoteColorMode(NoteColorMode mode)
{
    if (noteColorMode_ == mode)
        return;

    noteColorMode_ = mode;
    repaint();
}

void PianoRollComponent::setScaleRoot(const juce::String& root)
{
    if (scaleRoot_ == root)
        return;

    scaleRoot_ = root;
    repaint();
}

void PianoRollComponent::setScaleName(const juce::String& name)
{
    if (scaleName_ == name)
        return;

    scaleName_ = name;
    repaint();
}

bool PianoRollComponent::keyPressed(const juce::KeyPress& key)
{
    return handleGridKeyPress(key);
}

void PianoRollComponent::paint(juce::Graphics& g)
{
    auto& theme = Theme::getInstance();
    g.setColour(theme.colors.background);
    g.fillRect(getLocalBounds());
}

void PianoRollComponent::resized()
{
    updateLayout();
}

void PianoRollComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown())
    {
        // Ctrl+scroll = horizontal zoom
        const float factor = wheel.deltaY > 0.0f ? 1.15f : 1.0f / 1.15f;
        setPixelsPerTick(pixelsPerTick_ * factor);
        return;
    }
    if (e.mods.isAltDown())
    {
        // Alt+scroll = vertical zoom (row height)
        const float factor = wheel.deltaY > 0.0f ? 1.15f : 1.0f / 1.15f;
        setKeyRowHeight(keyRowHeight_ * factor);
        return;
    }

    if (std::abs(wheel.deltaX) > std::abs(wheel.deltaY))
    {
        scrollX_ = juce::jmax(0.0f, scrollX_ - wheel.deltaX * 128.0f);
        horizontalScrollBar_.setCurrentRangeStart(scrollX_);
    }
    else
    {
        firstVisibleMidiNote_ = juce::jlimit(0, 127, firstVisibleMidiNote_ + (wheel.deltaY > 0.0f ? 3 : -3));
        verticalScrollBar_.setCurrentRangeStart(127 - firstVisibleMidiNote_);
    }

    updateRenderState();
    repaint();
}

void PianoRollComponent::scrollBarMoved(juce::ScrollBar* scrollBar, double newRangeStart)
{
    if (scrollBar == &horizontalScrollBar_)
        scrollX_ = (float) juce::jmax(0.0, newRangeStart);
    else if (scrollBar == &verticalScrollBar_)
        firstVisibleMidiNote_ = juce::jlimit(0, 127, 127 - (int) std::round(newRangeStart));

    updateRenderState();
    repaint();
}

void PianoRollComponent::updateLayout()
{
    auto bounds = getLocalBounds();
    auto scrollBarArea = bounds.removeFromBottom(scrollBarThickness_);
    auto rightScrollBar = bounds.removeFromRight(scrollBarThickness_);
    auto rulerArea = bounds.removeFromTop(rulerHeight_);
    auto keyboardArea = bounds.removeFromLeft(keyboardWidth_);

    keyboard_.setBounds(keyboardArea);
    ruler_.setBounds(rulerArea.withTrimmedLeft(keyboardWidth_));
    gridViewport_.setBounds(bounds);
    horizontalScrollBar_.setBounds(scrollBarArea.withTrimmedLeft(keyboardWidth_));
    verticalScrollBar_.setBounds(rightScrollBar.withTrimmedTop(rulerHeight_));

    horizontalScrollBar_.setRangeLimits(0.0, 8192.0);
    horizontalScrollBar_.setCurrentRange(scrollX_, juce::jmax(128.0, (double) gridViewport_.getWidth()));
    verticalScrollBar_.setRangeLimits(0.0, 127.0);
    verticalScrollBar_.setCurrentRange(127 - firstVisibleMidiNote_, 24.0);

    updateRenderState();
}

void PianoRollComponent::updateRenderState()
{
    const int ppq = clipModel_ != nullptr ? clipModel_->getPPQ() : 960;

    gridRenderer_.setKeyRowHeight(keyRowHeight_);
    gridRenderer_.setTicksPerQuarterNote(ppq);
    gridRenderer_.setTicksPerGridDivision(ticksPerGridDivision_);
    gridRenderer_.setPixelsPerTick(pixelsPerTick_);
    gridRenderer_.setScrollOffset(scrollX_, firstVisibleMidiNote_);
    gridRenderer_.setScale(scaleRoot_, scaleName_);

    noteRenderer_.setKeyRowHeight(keyRowHeight_);
    noteRenderer_.setPixelsPerTick(pixelsPerTick_);
    noteRenderer_.setScrollOffset(scrollX_, firstVisibleMidiNote_);
    noteRenderer_.setColorMode(static_cast<PianoRollNoteRenderer::ColorMode>((int) noteColorMode_));

    keyboard_.setKeyRowHeight(keyRowHeight_);
    keyboard_.setFirstVisibleMidiNote(firstVisibleMidiNote_);
    keyboard_.setScale(scaleRoot_, scaleName_);

    ruler_.setTicksPerQuarterNote(ppq);
    ruler_.setTicksPerGridDivision(ticksPerGridDivision_);
    ruler_.setPixelsPerTick(pixelsPerTick_);
    ruler_.setScrollOffset(scrollX_);
}

void PianoRollComponent::handleGridMouseDown(const juce::MouseEvent& e)
{
    if (clipModel_ == nullptr)
        return;

    const auto hit = noteRenderer_.hitTest(e.position, gridViewport_.getLocalBounds(), clipModel_);

    if (e.mods.isRightButtonDown())
    {
        if (hit.noteId != 0)
        {
            clipModel_->removeNote(hit.noteId);
            if (activeNoteId_ == hit.noteId)
                activeNoteId_ = 0;
        }

        gridViewport_.repaint();
        return;
    }

    if (! e.mods.isLeftButtonDown())
        return;

    activeEditMode_ = ActiveEditMode::None;
    activeNoteId_ = 0;
    marqueeRect_ = {};
    pendingCreateNote_ = false;
    marqueeAdditive_ = false;
    dragOriginalNotes_.clear();
    gridViewport_.grabKeyboardFocus();
    beginUndoGesture();

    if (hit.noteId != 0)
    {
        if (e.mods.isShiftDown())
            toggleNoteSelection(hit.noteId);
        else
            selectSingleNote(hit.noteId);

        activeNoteId_ = hit.noteId;
        dragStartPosition_ = e.position;

        if (const auto* note = clipModel_->findNote(hit.noteId))
            dragOriginalNote_ = *note;

        dragOriginalNotes_ = getSelectedNotes();
        if (dragOriginalNotes_.isEmpty() && hit.noteId != 0)
            dragOriginalNotes_.add(dragOriginalNote_);

        if (hit.zone == PianoRollNoteRenderer::HitZone::LeftEdge)
            activeEditMode_ = ActiveEditMode::ResizeStart;
        else if (hit.zone == PianoRollNoteRenderer::HitZone::RightEdge)
            activeEditMode_ = ActiveEditMode::ResizeEnd;
        else if (hit.zone == PianoRollNoteRenderer::HitZone::Velocity)
            activeEditMode_ = ActiveEditMode::Velocity;
        else
            activeEditMode_ = ActiveEditMode::Move;

        gridViewport_.repaint();
        return;
    }

    dragStartPosition_ = e.position;
    pendingCreateNote_ = ! e.mods.isShiftDown();
    marqueeAdditive_ = e.mods.isShiftDown();
    if (marqueeAdditive_)
    {
        activeEditMode_ = ActiveEditMode::MarqueeSelect;
        marqueeRect_ = juce::Rectangle<float>(e.position.x, e.position.y, 0.0f, 0.0f);
    }
    gridViewport_.repaint();
}

void PianoRollComponent::handleGridMouseDrag(const juce::MouseEvent& e)
{
    if (clipModel_ == nullptr || ! e.mods.isLeftButtonDown())
        return;

    if (pendingCreateNote_ && e.getDistanceFromDragStart() >= dragThresholdPx_)
    {
        pendingCreateNote_ = false;
        activeEditMode_ = ActiveEditMode::MarqueeSelect;
        marqueeAdditive_ = false;
        marqueeRect_ = juce::Rectangle<float>(dragStartPosition_, e.position).getSmallestIntegerContainer().toFloat();
        setNotesSelected(noteRenderer_.getNotesIntersecting(marqueeRect_, gridViewport_.getLocalBounds(), clipModel_), true, true);
        gridViewport_.repaint();
        return;
    }

    if (activeEditMode_ == ActiveEditMode::MarqueeSelect)
    {
        marqueeRect_ = juce::Rectangle<float>(dragStartPosition_, e.position).getSmallestIntegerContainer().toFloat();
        auto hits = noteRenderer_.getNotesIntersecting(marqueeRect_, gridViewport_.getLocalBounds(), clipModel_);
        setNotesSelected(hits, true, ! marqueeAdditive_);
        gridViewport_.repaint();
        return;
    }

    if (activeNoteId_ == 0)
        return;

    auto editedNote = dragOriginalNote_;
    const auto deltaTicks = snapTickFromX(e.position.x) - snapTickFromX(dragStartPosition_.x);

    if (activeEditMode_ == ActiveEditMode::Move)
    {
        const int deltaPitch = midiNoteFromY(e.position.y) - midiNoteFromY(dragStartPosition_.y);
        applyMoveToNotes(dragOriginalNotes_, deltaTicks, deltaPitch);
    }
    else if (activeEditMode_ == ActiveEditMode::Velocity)
    {
        const int deltaVelocity = (int) std::round(dragStartPosition_.y - e.position.y);
        applyVelocityToNotes(dragOriginalNotes_, deltaVelocity, e.mods.isShiftDown());
    }
    else if (activeEditMode_ == ActiveEditMode::ResizeStart)
    {
        applyResizeToNotes(dragOriginalNotes_, deltaTicks, true);
    }
    else if (activeEditMode_ == ActiveEditMode::ResizeEnd)
    {
        applyResizeToNotes(dragOriginalNotes_, deltaTicks, false);
    }
    else
    {
        return;
    }

    gridViewport_.repaint();
}

void PianoRollComponent::handleGridMouseUp(const juce::MouseEvent& e)
{
    if (pendingCreateNote_)
    {
        clearSelection();
        createNoteAt(e.position);
    }

    if (undoGestureActive_)
    {
        juce::String description = "Edit MIDI";
        if (activeEditMode_ == ActiveEditMode::Move) description = "Move Notes";
        else if (activeEditMode_ == ActiveEditMode::ResizeStart || activeEditMode_ == ActiveEditMode::ResizeEnd) description = "Resize Notes";
        else if (activeEditMode_ == ActiveEditMode::Velocity) description = "Edit Velocity";
        else if (activeEditMode_ == ActiveEditMode::MarqueeSelect) description = "Select Notes";
        else if (pendingCreateNote_) description = "Create Note";
        commitUndoGesture(description);
    }

    activeEditMode_ = ActiveEditMode::None;
    activeNoteId_ = 0;
    pendingCreateNote_ = false;
    marqueeAdditive_ = false;
    dragOriginalNotes_.clear();
    marqueeRect_ = {};
    gridViewport_.repaint();
}

void PianoRollComponent::handleGridMouseMove(const juce::MouseEvent& e)
{
    if (clipModel_ == nullptr)
        return;

    const auto hit = noteRenderer_.hitTest(e.position, gridViewport_.getLocalBounds(), clipModel_);
    if (hit.zone == PianoRollNoteRenderer::HitZone::LeftEdge)
        gridViewport_.setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::LeftEdgeResizeCursor));
    else if (hit.zone == PianoRollNoteRenderer::HitZone::RightEdge)
        gridViewport_.setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::RightEdgeResizeCursor));
    else if (hit.zone == PianoRollNoteRenderer::HitZone::Velocity)
        gridViewport_.setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::UpDownResizeCursor));
    else if (hit.zone == PianoRollNoteRenderer::HitZone::Body)
        gridViewport_.setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::DraggingHandCursor));
    else
        gridViewport_.setMouseCursor(DAW::CursorThemeCore::getStandard(juce::MouseCursor::NormalCursor));
}

juce::int64 PianoRollComponent::snapTickFromX(float x) const
{
    const auto rawTick = (scrollX_ + x) / pixelsPerTick_;
    if (!snapEnabled_)
        return (juce::int64) std::floor(rawTick);

    return (juce::int64) std::floor((rawTick + (ticksPerGridDivision_ * 0.5f)) / (double) ticksPerGridDivision_) * ticksPerGridDivision_;
}

int PianoRollComponent::midiNoteFromY(float y) const
{
    const int row = juce::jmax(0, (int) std::floor(y / keyRowHeight_));
    return juce::jlimit(0, 127, firstVisibleMidiNote_ - row);
}

void PianoRollComponent::selectSingleNote(juce::int64 noteId)
{
    if (clipModel_ == nullptr)
        return;

    auto notes = clipModel_->getAllNotes();
    for (auto& note : notes)
    {
        const bool shouldSelect = note.id == noteId;
        if (note.selected != shouldSelect)
        {
            note.selected = shouldSelect;
            clipModel_->modifyNote(note.id, note);
        }
    }
}

void PianoRollComponent::createNoteAt(juce::Point<float> position)
{
    if (clipModel_ == nullptr)
        return;

    PianoRollClipModel::NoteEvent note;
    note.startTick = snapTickFromX(position.x);
    note.lengthTicks = ticksPerGridDivision_;
    note.pitch = (juce::uint8) snapPitchToScale(midiNoteFromY(position.y));
    note.velocity = 100;
    note.channel = 0;
    note.selected = true;

    activeNoteId_ = clipModel_->addNote(note);
    if (const auto* addedNote = clipModel_->findNote(activeNoteId_))
        dragOriginalNote_ = *addedNote;
}

bool PianoRollComponent::handleGridKeyPress(const juce::KeyPress& key)
{
    if (clipModel_ == nullptr)
        return false;

    const auto selectedNotes = getSelectedNotes();
    bool mutated = false;
    beginUndoGesture();

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelectedNotes();
        gridViewport_.repaint();
        commitUndoGesture("Delete Notes");
        return true;
    }

    if (key == juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0))
    {
        auto allIds = juce::Array<juce::int64>();
        for (auto& note : clipModel_->getAllNotes())
            allIds.add(note.id);
        setNotesSelected(allIds, true, false);
        gridViewport_.repaint();
        commitUndoGesture("Select All Notes");
        return true;
    }

    if (key == juce::KeyPress('c', juce::ModifierKeys::commandModifier, 0))
    {
        copySelectedNotes();
        undoGestureActive_ = false;
        return true;
    }

    if (key == juce::KeyPress('v', juce::ModifierKeys::commandModifier, 0))
    {
        pasteNotes();
        return true;
    }

    if (key == juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0))
    {
        duplicateSelectedNotes();
        return true;
    }

    if (key == juce::KeyPress('q', juce::ModifierKeys::commandModifier, 0))
    {
        quantizeSelectedNotes();
        return true;
    }

    if (selectedNotes.isEmpty())
    {
        undoGestureActive_ = false;
        return false;
    }

    if (key.getKeyCode() == juce::KeyPress::leftKey)
    {
        const auto delta = key.getModifiers().isShiftDown() ? -1 : -ticksPerGridDivision_;
        if (key.getModifiers().isCommandDown())
            applyResizeToNotes(selectedNotes, delta, false);
        else
            applyMoveToNotes(selectedNotes, delta, 0);
        gridViewport_.repaint();
        commitUndoGesture(key.getModifiers().isCommandDown() ? "Resize Notes" : "Move Notes");
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::rightKey)
    {
        const auto delta = key.getModifiers().isShiftDown() ? 1 : ticksPerGridDivision_;
        if (key.getModifiers().isCommandDown())
            applyResizeToNotes(selectedNotes, delta, false);
        else
            applyMoveToNotes(selectedNotes, delta, 0);
        gridViewport_.repaint();
        commitUndoGesture(key.getModifiers().isCommandDown() ? "Resize Notes" : "Move Notes");
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::upKey)
    {
        applyMoveToNotes(selectedNotes, 0, key.getModifiers().isShiftDown() ? 12 : 1);
        gridViewport_.repaint();
        commitUndoGesture("Transpose Notes");
        return true;
    }

    if (key.getKeyCode() == juce::KeyPress::downKey)
    {
        applyMoveToNotes(selectedNotes, 0, key.getModifiers().isShiftDown() ? -12 : -1);
        gridViewport_.repaint();
        commitUndoGesture("Transpose Notes");
        return true;
    }

    undoGestureActive_ = false;
    return false;
}

void PianoRollComponent::beginUndoGesture()
{
    if (clipModel_ == nullptr || undoGestureActive_)
        return;

    undoGestureBeforeState_ = clipModel_->toValueTree();
    undoGestureActive_ = true;
}

void PianoRollComponent::commitUndoGesture(const juce::String& description)
{
    if (clipModel_ == nullptr || !undoGestureActive_)
        return;

    auto afterState = clipModel_->toValueTree();
    if (undoGestureBeforeState_.isEquivalentTo(afterState))
    {
        undoGestureActive_ = false;
        return;
    }

    DAW::CommandManager::getInstance().execute(std::make_unique<DAW::PianoRollStateCommand>(
        *clipModel_, undoGestureBeforeState_, afterState, description, true));
    undoGestureActive_ = false;
}

juce::Array<juce::int64> PianoRollComponent::getSelectedNoteIds() const
{
    juce::Array<juce::int64> ids;
    if (clipModel_ == nullptr)
        return ids;

    for (auto& note : clipModel_->getAllNotes())
        if (note.selected)
            ids.add(note.id);

    return ids;
}

juce::Array<PianoRollClipModel::NoteEvent> PianoRollComponent::getSelectedNotes() const
{
    juce::Array<PianoRollClipModel::NoteEvent> notes;
    if (clipModel_ == nullptr)
        return notes;

    for (auto& note : clipModel_->getAllNotes())
        if (note.selected)
            notes.add(note);

    return notes;
}

int PianoRollComponent::snapPitchToScale(int midiNote) const
{
    if (!scaleSnapEnabled_)
        return juce::jlimit(0, 127, midiNote);

    return PianoRollScaleHelper::snapNoteToScale(midiNote, scaleRoot_, scaleName_);
}

void PianoRollComponent::applyMoveToNotes(const juce::Array<PianoRollClipModel::NoteEvent>& originalNotes,
                                          juce::int64 deltaTicks,
                                          int deltaPitch)
{
    if (clipModel_ == nullptr)
        return;

    for (auto& original : originalNotes)
    {
        auto edited = original;
        edited.startTick = juce::jmax<juce::int64>(0, original.startTick + deltaTicks);
        edited.pitch = (juce::uint8) snapPitchToScale((int) original.pitch + deltaPitch);
        clipModel_->modifyNote(original.id, edited);
    }
}

void PianoRollComponent::applyVelocityToNotes(const juce::Array<PianoRollClipModel::NoteEvent>& originalNotes,
                                              int deltaVelocity,
                                              bool absolute)
{
    if (clipModel_ == nullptr || originalNotes.isEmpty())
        return;

    const int absoluteVelocity = juce::jlimit(1, 127, (int) dragOriginalNote_.velocity + deltaVelocity);

    for (auto& original : originalNotes)
    {
        auto edited = original;
        edited.velocity = (juce::uint8) (absolute
            ? absoluteVelocity
            : juce::jlimit(1, 127, (int) original.velocity + deltaVelocity));
        clipModel_->modifyNote(original.id, edited);
    }
}

void PianoRollComponent::applyResizeToNotes(const juce::Array<PianoRollClipModel::NoteEvent>& originalNotes,
                                            juce::int64 deltaTicks,
                                            bool resizeStart)
{
    if (clipModel_ == nullptr)
        return;

    for (auto& original : originalNotes)
    {
        auto edited = original;
        if (resizeStart)
        {
            const auto noteEnd = original.startTick + original.lengthTicks;
            const auto newStart = juce::jlimit<juce::int64>(0, noteEnd - 1, original.startTick + deltaTicks);
            edited.startTick = newStart;
            edited.lengthTicks = juce::jmax<juce::int64>(1, noteEnd - newStart);
        }
        else
        {
            edited.lengthTicks = juce::jmax<juce::int64>(1, original.lengthTicks + deltaTicks);
        }

        clipModel_->modifyNote(original.id, edited);
    }
}

void PianoRollComponent::deleteSelectedNotes()
{
    if (clipModel_ == nullptr)
        return;

    for (auto noteId : getSelectedNoteIds())
        clipModel_->removeNote(noteId);
}

void PianoRollComponent::toggleNoteSelection(juce::int64 noteId)
{
    if (clipModel_ == nullptr || noteId == 0)
        return;

    if (const auto* note = clipModel_->findNote(noteId))
    {
        auto edited = *note;
        edited.selected = ! edited.selected;
        clipModel_->modifyNote(noteId, edited);
    }
}

void PianoRollComponent::setNotesSelected(const juce::Array<juce::int64>& noteIds, bool selected, bool clearExisting)
{
    if (clipModel_ == nullptr)
        return;

    auto notes = clipModel_->getAllNotes();
    for (auto& note : notes)
    {
        const bool shouldUpdate = noteIds.contains(note.id);
        const bool newSelected = shouldUpdate ? selected : (clearExisting ? false : note.selected);
        if (note.selected != newSelected)
        {
            note.selected = newSelected;
            clipModel_->modifyNote(note.id, note);
        }
    }
}

void PianoRollComponent::clearSelection()
{
    if (clipModel_ == nullptr)
        return;

    auto notes = clipModel_->getAllNotes();
    for (auto& note : notes)
    {
        if (note.selected)
        {
            note.selected = false;
            clipModel_->modifyNote(note.id, note);
        }
    }
}

//==============================================================================
// New features: zoom, playhead, copy/paste, quantize
//==============================================================================

void PianoRollComponent::setPixelsPerTick(float ppt)
{
    pixelsPerTick_ = juce::jlimit(0.02f, 4.0f, ppt);
    updateRenderState();
    repaint();
}

void PianoRollComponent::setKeyRowHeight(float h)
{
    keyRowHeight_ = juce::jlimit(8.0f, 40.0f, h);
    updateRenderState();
    repaint();
}

void PianoRollComponent::setPlayheadTick(juce::int64 tick)
{
    if (playheadTick_ == tick)
        return;
    playheadTick_ = tick;
    // Auto-scroll to keep playhead visible
    const float headX = (float)(tick - (juce::int64)(scrollX_ / pixelsPerTick_)) * pixelsPerTick_ - scrollX_;
    const float viewW = (float)gridViewport_.getWidth();
    if (headX < 0.0f || headX > viewW * 0.85f)
    {
        scrollX_ = juce::jmax(0.0f, (float)tick * pixelsPerTick_ - viewW * 0.15f);
        horizontalScrollBar_.setCurrentRangeStart(scrollX_);
        updateRenderState();
    }
    gridViewport_.repaint();
    ruler_.repaint();
}

void PianoRollComponent::timerCallback()
{
    gridViewport_.repaint();
}

void PianoRollComponent::paintPlayhead(juce::Graphics& g)
{
    const float x = (float)playheadTick_ * pixelsPerTick_ - scrollX_;
    if (x < 0.0f || x > (float)gridViewport_.getWidth())
        return;
    g.setColour(juce::Colour(0xFFFFD700).withAlpha(0.85f));
    g.drawVerticalLine((int)x, 0.0f, (float)gridViewport_.getHeight());
}

void PianoRollComponent::copySelectedNotes()
{
    if (clipModel_ == nullptr) return;
    clipboard_ = getSelectedNotes();
}

void PianoRollComponent::pasteNotes()
{
    if (clipModel_ == nullptr || clipboard_.isEmpty()) return;

    beginUndoGesture();
    clearSelection();

    // Find minimum start tick of clipboard notes (paste relative offset = 0)
    juce::int64 minTick = clipboard_[0].startTick;
    for (auto& n : clipboard_)
        minTick = juce::jmin(minTick, n.startTick);

    // Paste at playhead position
    const juce::int64 pasteAnchor = snapEnabled_
        ? (playheadTick_ / ticksPerGridDivision_) * ticksPerGridDivision_
        : playheadTick_;

    for (auto note : clipboard_)
    {
        note.id = 0;   // will be reassigned by addNote
        note.startTick = pasteAnchor + (note.startTick - minTick);
        note.selected = true;
        clipModel_->addNote(note);
    }

    gridViewport_.repaint();
    commitUndoGesture("Paste Notes");
}

void PianoRollComponent::duplicateSelectedNotes()
{
    if (clipModel_ == nullptr) return;
    auto selected = getSelectedNotes();
    if (selected.isEmpty()) return;

    beginUndoGesture();

    // Find end of latest selected note
    juce::int64 maxEnd = 0;
    for (auto& n : selected)
        maxEnd = juce::jmax(maxEnd, n.startTick + n.lengthTicks);

    // Snap to grid boundary
    if (snapEnabled_ && ticksPerGridDivision_ > 0)
        maxEnd = ((maxEnd + ticksPerGridDivision_ - 1) / ticksPerGridDivision_) * ticksPerGridDivision_;

    juce::int64 minTick = selected[0].startTick;
    for (auto& n : selected)
        minTick = juce::jmin(minTick, n.startTick);

    clearSelection();

    for (auto note : selected)
    {
        note.id = 0;
        note.startTick = maxEnd + (note.startTick - minTick);
        note.selected = true;
        clipModel_->addNote(note);
    }

    gridViewport_.repaint();
    commitUndoGesture("Duplicate Notes");
}

void PianoRollComponent::quantizeSelectedNotes()
{
    if (clipModel_ == nullptr || ticksPerGridDivision_ <= 0) return;
    auto selected = getSelectedNotes();
    if (selected.isEmpty()) return;

    beginUndoGesture();
    for (auto note : selected)
    {
        const juce::int64 grid = ticksPerGridDivision_;
        note.startTick = ((note.startTick + grid / 2) / grid) * grid;
        clipModel_->modifyNote(note.id, note);
    }

    gridViewport_.repaint();
    commitUndoGesture("Quantize Notes");
}

} // namespace DAW
