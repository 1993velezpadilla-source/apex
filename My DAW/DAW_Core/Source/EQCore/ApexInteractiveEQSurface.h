#pragma once
#include <JuceHeader.h>

namespace APEX {

// ============================================================================
// ApexInteractiveEQSurface — a reusable interactive parametric-EQ surface
// that overlays a spectrum analyzer.
//
// Reuse boundary (Phase 3): G10 uses ONLY
//     HPF node, LPF node, up to maxBells Bell nodes (G10: 3).
// Dynamic EQ, M/S, spectral processing, band solo, extra filter types etc.
// belong to the future full APEX Parametric EQ and are deliberately NOT
// implemented here.
//
// The surface is parameter-agnostic: the owning product wires a Model of
// accessor callbacks (message-thread only) to its own parameter objects. All
// edits go through the canonical host automation gesture pipeline via the
// set* callbacks (begin -> notify -> end), so audio, node positions, the
// nominal curve, readouts, host automation and saved state all stay in sync.
//
// Layers (truth model):
//   - The UNDERLYING spectrum is the actual measured post-output audio
//     (owned by the host analyzer). The surface never draws a spectrum.
//   - The surface draws ONLY the NOMINAL static response of its own filters
//     (HPF + Bells + LPF) — never claimed to be the exact transfer of a
//     nonlinear musical/color path.
//
// Coordinate space: the surface uses the SAME logical space as the analyzer
// it overlays (log-frequency axis, minFreqHz..maxFreqHz). It is a child of
// the analyzer component, so the existing Content transform handles scaling,
// DPI and hit-testing with no extra machinery.
// ============================================================================

// One authoritative graph geometry shared by the analyzer AND the interactive
// surface. Grid, spectrum, Mini EQ curve, nodes and hit testing all use the
// SAME plot rectangle so visual and mouse coordinates can never drift apart.
//
// The analyzer component lives in the logical 1000x612 design space (the
// Content transform scales it), so these reserves are logical pixels.
struct ApexEqPlotGeometry
{
    // Axis-label reserves (logical px).
    static constexpr float kAxisLeft   = 40.0f;   // dB labels (readable size)
    static constexpr float kAxisRight  = 20.0f;   // LPF OFF handle + readout
    static constexpr float kAxisTop    = 10.0f;
    static constexpr float kAxisBottom = 30.0f;   // frequency labels

    // Node affordances: OFF handles sit fully INSIDE the plot with a visible
    // radius and a larger forgiving hit radius. Never at x=0 / x=width.
    static constexpr float kOffHandleInset = 12.0f;  // from plot edge to center
    static constexpr float kNodeRadius     = 5.0f;   // visible radius
    static constexpr float kHitRadius      = 14.0f;  // interaction radius

    static juce::Rectangle<float> plotBounds (float width, float height) noexcept
    {
        return { kAxisLeft, kAxisTop,
                 width  - kAxisLeft - kAxisRight,
                 height - kAxisTop  - kAxisBottom };
    }
};

class ApexInteractiveEQSurface; // forward declaration (BellControlPanel owner)

// ============================================================================
// BellControlPanel — the compact floating control panel for the SELECTED Bell.
//
// A child of the interactive surface (ONE surface-local geometry authority):
// it anchors near/above the node, flips below when the top would clip, clamps
// near the edges and ALWAYS stays inside the analyzer, following the node
// while it is dragged. The selected Bell's readout box is replaced by this
// panel.
//
// Exposes: B1/B2/B3, Frequency, Gain, Q (drag / wheel / double-click numeric
// entry: Enter commits, Escape cancels, Tab cycles Frequency -> Gain -> Q),
// Bypass, Reset, Delete. Every action routes through the surface's ONE
// command authority (ApexInteractiveEQSurface::runBellCommand), so the panel
// button, the context menu and the Delete key share the exact same code path.
// ============================================================================
class BellControlPanel final : public juce::Component
{
public:
    enum class Field { freq, gain, q };

    explicit BellControlPanel (ApexInteractiveEQSurface& owner);
    ~BellControlPanel() override
    {
        // Close any open editors QUIETLY (discard the in-flight edit).  The
        // commit path (commitText -> setValue -> owner_.notifyEdited ->
        // rebuildCurve) must NEVER run here: the panel is being destroyed as
        // a member of the surface, and the surface's curve/geometry members
        // are already freed — a commit would be a heap use-after-free
        // (caught by AddressSanitizer).
        closeEditorsQuietly();
    }

    /** Discard any in-flight numeric edits without touching the model or the
        owner surface.  Safe during teardown. */
    void closeEditorsQuietly()
    {
        freq_.closeEditorQuietly();
        gain_.closeEditorQuietly();
        q_.closeEditorQuietly();
    }

    void updatePanel();    // re-read the model for the selected Bell
    void updatePosition(); // smart non-obstructive placement near the node
    void paint (juce::Graphics&) override;
    void resized() override;

    // Interaction-state contract:
    //   full — the compact inspector (B#, Freq/Gain/Q, Bypass, Reset, Delete)
    //   hud  — a tiny live-values readout while the Bell is being dragged
    enum class Mode { full, hud };

    void setMode (Mode m)
    {
        if (mode_ == m)
            return;
        mode_ = m;
        const bool full = (m == Mode::full);
        title_.setVisible (full);
        captionFreq_.setVisible (full);
        freq_.setVisible (full);
        captionGain_.setVisible (full);
        gain_.setVisible (full);
        captionQ_.setVisible (full);
        q_.setVisible (full);
        bypassButton_.setVisible (full);
        resetButton_.setVisible (full);
        deleteButton_.setVisible (full);
        setSize (full ? kPanelW : kHudW, full ? kPanelH : kHudH);
        resized();
        repaint();
    }

    Mode getMode() const noexcept { return mode_; }

    /** Mark a panel-origin wheel adjustment (freezes the panel for the
        notch; placement recomputes once afterwards). */
    void setWheelInteraction (bool active) noexcept { wheelInteractionActive_ = active; }

    /** True while ANY panel-origin interaction is in flight (value drag,
        wheel adjustment, numeric editing).  While true, the panel's screen
        position is LOCKED: the Bell/curve move underneath it, the panel
        does not. */
    bool isAnyInteractionActive() const noexcept
    {
        return wheelInteractionActive_
            || freq_.isDragging() || gain_.isDragging() || q_.isDragging()
            || freq_.isEditing() || gain_.isEditing() || q_.isEditing();
    }

    /** Placement family of the current panel position (anchor pointer +
        tests).  FULL-inspector families are restricted to ABOVE, LEFT and
        RIGHT — top-left/top-right map to ABOVE; BELOW is never chosen. */
    enum class Placement { above, below, left, right, clamped };

    /** The current placement family (for the anchor pointer and tests). */
    Placement getPlacement() const noexcept { return placement_; }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    // Value helpers (used by the nested ValueControl).
    float getUnitValue (Field f) const;
    juce::String formatValue (Field f, float unitValue) const;
    float adjustFreqUnit (float currentHz, float dxPixels) const;
    float adjustVerticalUnit (Field f, float currentUnit, float dyPixels) const;
    void beginGesture (Field f);
    void setValue (Field f, float unitValue, bool endGesture);
    void commitText (Field f, const juce::String& text);
    void focusNextValue (Field f);
    bool findEditingField (Field& out) const;
    void commitEditingField (Field f);
    juce::KeyListener& getTabListener() noexcept { return tabListener_; }

    // ---- ValueControl: drag / wheel / double-click numeric entry ----------
    class ValueControl final : public juce::Component
    {
    public:
        ValueControl (BellControlPanel& panel, Field field)
            : panel_ (panel), field_ (field)
        {
            setInterceptsMouseClicks (true, true);
            setWantsKeyboardFocus (true);
        }

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat();
            // Premium hotzone: NO persistent box. A subtle highlight appears
            // only on hover; the value text is the visual anchor. The real
            // TextEditor appears only during numeric editing.
            if (isMouseOver (true))
            {
                g.setColour (juce::Colour (0x14FFFFFF));
                g.fillRoundedRectangle (b, 3.0f);
            }
            g.setFont (juce::Font (11.0f, juce::Font::bold));
            g.setColour (juce::Colour (0xFFE9E4F0));
            g.drawText (panel_.formatValue (field_, panel_.getUnitValue (field_)),
                        b, juce::Justification::centredRight);
        }

        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() || ! e.mods.isLeftButtonDown() || editing_)
                return;
            grabKeyboardFocus();
            dragActive_ = true;
            dragOrigin_ = e.position;
            dragStartUnit_ = panel_.getUnitValue (field_);
            gestureStarted_ = false;
        }

        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            dragActive_ = false;
            showEditor();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! dragActive_ || editing_)
                return;
            const bool fine = e.mods.isShiftDown();
            const float scale = fine ? 0.08f : 1.0f;
            float unit;
            if (field_ == Field::freq)
                unit = panel_.adjustFreqUnit (dragStartUnit_, (e.position.x - dragOrigin_.x) * scale);
            else
                unit = panel_.adjustVerticalUnit (field_, dragStartUnit_, (e.position.y - dragOrigin_.y) * scale);
            if (! gestureStarted_)
            {
                panel_.beginGesture (field_);
                gestureStarted_ = true;
            }
            panel_.setValue (field_, unit, false);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragActive_ && gestureStarted_)
                panel_.setValue (field_, panel_.getUnitValue (field_), true);
            dragActive_ = false;
            gestureStarted_ = false;
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            if (editing_)
                return;
            const bool fine = e.mods.isShiftDown();
            float unit = panel_.getUnitValue (field_);
            if (field_ == Field::freq)
                unit = panel_.adjustFreqUnit (unit, wheel.deltaY * (fine ? 20.0f : 200.0f));
            else
                unit = panel_.adjustVerticalUnit (field_, unit, wheel.deltaY * (fine ? 0.5f : 2.0f));
            // Panel-origin adjustment: keep the panel frozen for the notch.
            panel_.setWheelInteraction (true);
            panel_.beginGesture (field_);
            panel_.setValue (field_, unit, true);
            panel_.setWheelInteraction (false);
        }

        void focusGained (juce::Component::FocusChangeType cause) override
        {
            // Tab traversal continues numeric editing in the next field;
            // mouse clicks keep the double-click-to-edit rule.
            if (cause == juce::Component::focusChangedByTabKey && ! editing_)
                showEditor();
        }

        void showEditor()
        {
            if (editing_)
                return;
            editing_ = true;
            // Replacing the unique_ptr destroys any previous (hidden) editor
            // HERE — outside its own callbacks, so no re-entrancy.
            editor_ = std::make_unique<juce::TextEditor>();
            addAndMakeVisible (editor_.get());
            editor_->setBounds (getLocalBounds().reduced (1));
            editor_->setFont (juce::Font (11.0f, juce::Font::bold));
            editor_->setJustification (juce::Justification::centred);
            // APEX-skin the editor: no default JUCE/Windows look.
            editor_->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xFF0E0E13));
            editor_->setColour (juce::TextEditor::textColourId, juce::Colour (0xFFE9E4F0));
            editor_->setColour (juce::TextEditor::highlightColourId, juce::Colour (0x66C8A8E8));
            editor_->setColour (juce::TextEditor::highlightedTextColourId, juce::Colour (0xFF141419));
            editor_->setColour (juce::TextEditor::outlineColourId, juce::Colour (0x66C8A8E8));
            editor_->setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xB0C8A8E8));
            editor_->setBorder (juce::BorderSize<int> (3));
            editor_->setText (panel_.formatValue (field_, panel_.getUnitValue (field_)), false);
            editor_->setSelectAllWhenFocused (true);
            editor_->addKeyListener (&panel_.getTabListener()); // Tab cycles fields
            editor_->onReturnKey = [this] { commitEditor(); };
            editor_->onEscapeKey = [this]
            {
                if (! editing_)
                    return;
                editing_ = false;
                editor_->setVisible (false); // destruction deferred to showEditor
                panel_.updatePanel();        // revert the display, no parameter change
            };
            editor_->onFocusLost = [this] { commitEditor(); };
            editor_->grabKeyboardFocus();
        }

        void commitEditor()
        {
            if (! editing_)
                return;
            const juce::String text = editor_->getText();
            // Commit WHILE the editor still counts as active: the notifyEdited
            // inside commitText repositions the panel, and the placement lock
            // keyed on isEditing() must still be engaged — otherwise the panel
            // would move under the user mid-commit / mid-Tab-chain.
            panel_.commitText (field_, text);
            editing_ = false;
            // Hide instead of destroying: this may run from the editor's own
            // onReturnKey/onFocusLost callback, where destroying the editor
            // would be undefined behaviour. The next showEditor (or the
            // ValueControl's destruction) releases it.
            editor_->setVisible (false);
        }

        /** Discard the in-flight edit and release the TextEditor WITHOUT
            touching the model or the panel's owner.  Called from the panel
            destructor, where a commit would cascade into the already-freed
            surface geometry (use-after-free). */
        void closeEditorQuietly()
        {
            if (! editing_)
                return;
            editing_ = false;
            editor_.reset(); // destroy the editor; no commit, no callbacks
        }

        bool isEditing() const noexcept { return editing_; }
        bool isDragging() const noexcept { return dragActive_; }
        juce::TextEditor* getEditor() const noexcept { return editor_.get(); }

    private:
        BellControlPanel& panel_;
        const Field field_;
        bool dragActive_ = false;
        bool gestureStarted_ = false;
        bool editing_ = false;
        juce::Point<float> dragOrigin_;
        float dragStartUnit_ = 0.0f;
        std::unique_ptr<juce::TextEditor> editor_;
    };

    /** Test/introspection: the value control for a field (numeric entry).
        Defined after ValueControl so the nested type is complete. */
    ValueControl* getValueControl (Field f) noexcept
    {
        switch (f)
        {
        case Field::freq: return &freq_;
        case Field::gain: return &gain_;
        case Field::q:    return &q_;
        }
        return nullptr;
    }

    class TabTraversalListener final : public juce::KeyListener
    {
    public:
        explicit TabTraversalListener (BellControlPanel& panel) : panel_ (panel) {}

        bool keyPressed (const juce::KeyPress& key, juce::Component*) override
        {
            if (key != juce::KeyPress::tabKey)
                return false;
            Field f;
            if (! panel_.findEditingField (f))
                return false;
            panel_.commitEditingField (f); // commit, then continue editing next
            panel_.focusNextValue (f);
            return true;
        }

    private:
        BellControlPanel& panel_;
    };

    // Compact APEX power control for the individual Bell bypass.
    class PowerButton final : public juce::Button
    {
    public:
        explicit PowerButton() : juce::Button ("Bypass") {}

        void paintButton (juce::Graphics& g, bool hover, bool down) override
        {
            const auto b = getLocalBounds().toFloat().reduced (1.5f);
            const bool on = getToggleState();

            // Visible pill body — a real clickable button, never a thin
            // line.  Active = violet-tinted; bypassed = dim translucent.
            const float r = b.getHeight() * 0.5f;
            g.setColour (on ? juce::Colour (0xFFC8A8E8).withAlpha (0.22f)
                            : juce::Colour (0x18FFFFFF));
            g.fillRoundedRectangle (b, r);
            g.setColour (on ? juce::Colour (0x66C8A8E8).withAlpha (hover ? 0.9f : 0.6f)
                            : juce::Colour (0x33FFFFFF).withAlpha (hover ? 0.9f : 0.5f));
            g.drawRoundedRectangle (b, r, 1.0f);

            // Compact power icon inside the pill.
            const float cx = b.getCentreX();
            const float cy = b.getCentreY();
            const float iconR = juce::jmin (b.getWidth(), b.getHeight()) * 0.28f;
            g.setColour (on ? juce::Colour (0xFFC8A8E8) : juce::Colour (0x70B8A0D8));
            juce::Path ring;
            ring.addArc (cx - iconR, cy - iconR * 0.55f, iconR * 2.0f, iconR * 2.0f,
                         0.15f, juce::MathConstants<float>::pi - 0.15f);
            g.strokePath (ring, juce::PathStrokeType (1.4f,
                          juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.drawLine (cx, cy - iconR * 0.85f, cx, cy - iconR * 0.15f, 1.4f);
        }
    };

    // Compact secondary action: RESET (neutral) / DELETE (destructive).
    class MiniActionButton final : public juce::Button
    {
    public:
        MiniActionButton (const juce::String& name, juce::Colour textColour,
                          juce::Colour accentColour)
            : juce::Button (name), textColour_ (textColour), accentColour_ (accentColour) {}

        void paintButton (juce::Graphics& g, bool hover, bool down) override
        {
            const auto b = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (hover || down ? juce::Colour (0x24FFFFFF) : juce::Colour (0x10FFFFFF));
            g.fillRoundedRectangle (b, 4.0f);
            g.setColour (accentColour_.withAlpha (hover || down ? 0.7f : 0.35f));
            g.drawRoundedRectangle (b, 4.0f, 1.0f);
            g.setFont (juce::Font (9.0f, juce::Font::bold));
            g.setColour (hover || down ? textColour_.brighter (0.15f) : textColour_);
            g.drawText (getButtonText(), b, juce::Justification::centred);
        }

    private:
        juce::Colour textColour_;
        juce::Colour accentColour_;
    };

private:
    static constexpr int kPanelW = 144;
    static constexpr int kPanelH = 96;
    static constexpr int kHudW = 104;   // micro HUD during drag
    static constexpr int kHudH = 46;

    Mode mode_ = Mode::full;

    // Panel-origin gesture placement lock: while the user interacts WITH
    // the panel (value drag / wheel / numeric edit), the panel stays put —
    // the Bell and curve move underneath it, the controls stay under the
    // user's hand.  lockedPanelBounds_ is captured at the first update
    // during the gesture and preserved (minimally clamped) until the
    // gesture ends, then smart placement runs once.
    bool wheelInteractionActive_ = false;
    bool frozenDuringInteraction_ = false;
    juce::Rectangle<int> lockedPanelBounds_ = {};

    // Stability key: the panel only repositions when the selection, node
    // position, mode or surface size actually changed (or the current bounds
    // became invalid).  Cursor movement NEVER reflows the panel.
    struct PlacementKey
    {
        int bell = -1;
        Mode mode = Mode::full;
        juce::Point<int> node = {};
        juce::Point<int> size = {};
    };
    PlacementKey lastPlacementKey_;

    ApexInteractiveEQSurface& owner_;
    juce::Label title_;
    juce::Label captionFreq_;
    ValueControl freq_;
    juce::Label captionGain_;
    ValueControl gain_;
    juce::Label captionQ_;
    ValueControl q_;
    PowerButton bypassButton_;
    MiniActionButton resetButton_ { "RESET", juce::Colour (0xFFA29AB0), juce::Colour (0xFF5A5470) };
    MiniActionButton deleteButton_ { "DELETE", juce::Colour (0xFFD08A8A), juce::Colour (0xFF6A3A46) };
    TabTraversalListener tabListener_ { *this };
    Placement placement_ = Placement::above;
};

class ApexInteractiveEQSurface final : public juce::Component
{
public:
    struct BellBinding
    {
        std::function<float()> getEnabled;
        std::function<float()> getFreqNorm;
        std::function<float()> getGainNorm;
        std::function<float()> getQNorm;
        std::function<void (float value, bool gestureStart, bool gestureEnd)> setEnabled;
        std::function<void (float value, bool gestureStart, bool gestureEnd)> setFreqNorm;
        std::function<void (float value, bool gestureStart, bool gestureEnd)> setGainNorm;
        std::function<void (float value, bool gestureStart, bool gestureEnd)> setQNorm;

        // v3: individual Bell bypass (bypass != delete — the Bell stays
        // present, Freq/Gain/Q persist, only the DSP contribution fades).
        std::function<float()> getBypassed;
        std::function<void (float value, bool gestureStart, bool gestureEnd)> setBypassed;
    };

    struct Model
    {
        float minFreqHz = 20.0f;
        float maxFreqHz = 20000.0f;

        // Control laws (normalized <-> units). G10 wires G10MiniEqCore's
        // static laws; the future APEX Parametric EQ wires its own.
        std::function<float (float)> normToFreq;      // 0..1 -> Hz (log)
        std::function<float (float)> freqToNorm;      // Hz -> 0..1
        std::function<float (float)> gainNormToDb;    // 0..1 -> dB
        std::function<float (float)> dbToGainNorm;    // dB -> 0..1
        std::function<float (float)> qNormToQ;        // 0..1 -> Q
        std::function<float (float)> qToQNorm;        // Q -> 0..1 (panel numeric entry)
        float qMin = 0.10f;                           // panel clamp (G10: 0.10)
        float qMax = 40.0f;                           // panel clamp (G10: 40.0)
        std::function<float (float)> hpfNormToHz;     // 0=OFF, (0,1] -> Hz
        std::function<float (float)> lpfNormToHz;     // 1=OFF, [0,1) -> Hz

        // HPF / LPF.
        std::function<float()> getHpfNorm;
        std::function<void (float, bool, bool)> setHpfNorm;
        std::function<float()> getLpfNorm;
        std::function<void (float, bool, bool)> setLpfNorm;

        // Bells (fixed capacity; only the first maxBells entries are used).
        BellBinding bells[3];
        int maxBells = 3;
        float defaultQNorm = 0.5f;

        // Bell lifecycle (presentation-independent).
        std::function<bool (float freqNorm, float gainNorm)> createBellAt; // false = limit reached
        std::function<void (int bellIndex)> removeBell;
        std::function<int()> enabledBellCount;

        // Nominal static magnitude (linear gain) of the whole cascade at Hz.
        std::function<float (float hz, float sampleRate)> nominalMagnitude;

        bool isValid() const noexcept
        {
            return normToFreq && freqToNorm && gainNormToDb && dbToGainNorm
                && qNormToQ && qToQNorm && hpfNormToHz && lpfNormToHz
                && getHpfNorm && setHpfNorm && getLpfNorm && setLpfNorm
                && createBellAt && removeBell && enabledBellCount && nominalMagnitude;
        }
    };

    explicit ApexInteractiveEQSurface()
    {
        setOpaque (false);
        setInterceptsMouseClicks (true, true); // children are this surface's own drawing
        setWantsKeyboardFocus (true);
        trace ("created bounds=" + getBounds().toString());

        // Floating control panel for the selected Bell (hidden until a Bell
        // is selected; ONE surface-local geometry authority — see
        // BellControlPanel::updatePosition).
        bellPanel_ = std::make_unique<BellControlPanel> (*this);
        addChildComponent (bellPanel_.get());
    }

    // Temporary real-host diagnostics (message thread only). Logger::
    // outputDebugString works in Release builds too (unlike DBG), so a
    // DebugView / debugger attach can prove raw event delivery in the real
    // host. Remove once the real user confirms interaction.
    static void trace (const juce::String& message)
    {
        juce::Logger::outputDebugString ("[G10EQ] " + message);
    }

    void setModel (Model model)
    {
        model_ = std::move (model);
        selectedKind_ = NodeKind::none;
        selectedIndex_ = -1;
        rebuildCurve();
        updateBellPanel();
        repaint();
    }

    const Model& getModel() const noexcept { return model_; }

    void setSampleRate (double sr) noexcept { sampleRate_ = juce::jmax (1.0, sr); }

    /** Message thread (~30 Hz from the owning editor timer): re-read the
        model, rebuild the nominal curve cache, repaint. Keeps automation-
        driven edits live without any mouse interaction; the floating panel
        re-anchors to the node at the same cadence. */
    void refresh()
    {
        rebuildCurve();
        updateBellPanel();
        repaint();
    }

    // ---- Test introspection -------------------------------------------------

    int getEnabledBellCount() const noexcept
    {
        return model_.enabledBellCount ? model_.enabledBellCount() : 0;
    }

    float freqToX (float hz) const noexcept
    {
        const auto plot = ApexEqPlotGeometry::plotBounds ((float) getWidth(), (float) getHeight());
        const float logMin = std::log (juce::jmax (1.0f, model_.minFreqHz));
        const float logMax = std::log (juce::jmax (1.0f, model_.maxFreqHz));
        const float clamped = juce::jlimit (model_.minFreqHz, model_.maxFreqHz, hz);
        const float t = (std::log (clamped) - logMin) / (logMax - logMin);
        return plot.getX() + t * plot.getWidth();
    }

    float gainToY (float gainDb) const noexcept
    {
        // The nominal curve lives in the central band of the plot:
        // +12 dB at ~15% height, -12 dB at ~85% height, 0 dB at 50%.
        const auto plot = ApexEqPlotGeometry::plotBounds ((float) getWidth(), (float) getHeight());
        return plot.getY() + plot.getHeight() * (0.5f - (gainDb / 12.0f) * 0.35f);
    }

    /** CANONICAL response-dB → plot-Y transform. THE single authority for
        every response pixel: the nominal curve, Bell node centres, hit
        targets, panel anchor and the HPF/LPF cut-filter anchors all use
        exactly this function, so the node physically occupies the same
        screen pixels as the displayed response. */
    float responseDbToPlotY (float db) const noexcept
    {
        return gainToY (db);
    }

    /** The exact plot-space coordinate of the nominal response at a
        frequency — the canonical curve point.  The rendered polyline is
        built from these same coordinates (see rebuildCurve), so the curve
        pixel and this point are identical. */
    juce::Point<float> getNominalCurvePointAtHz (float hz) const noexcept
    {
        const float db = juce::jlimit (-12.0f, 12.0f, nominalDbAtHz (hz));
        return { freqToX (hz), responseDbToPlotY (db) };
    }

    /** Render-level accessor: the actual curve vertices used by paint()
        (also used by the inspector's smart non-obstructive placement). */
    const std::vector<juce::Point<float>>& getRenderedCurvePoints() const noexcept
    {
        return curvePoints_;
    }

    int getSelectedBellIndex() const noexcept
    {
        return selectedKind_ == NodeKind::bell ? selectedIndex_ : -1;
    }

    // ---- ONE command authority ---------------------------------------------
    // Every Bell action — the Delete/Backspace key, the panel's Bypass /
    // Reset / Delete buttons and the right-click menu items — routes through
    // this single method, so every UI route behaves identically.

    enum class BellCommand
    {
        toggleBypass, // bypass != delete: Bell stays, F/G/Q persist, DSP fades
        resetGain,    // gain -> 0 dB (stays enabled/bypassed as-is)
        resetQ,       // Q -> default Q (1.0)
        resetBand,    // gain -> 0 dB AND Q -> default Q (frequency unchanged)
        deleteBell    // Bell disappears, slot becomes reusable
    };

    void runBellCommand (BellCommand cmd, int bellIndex)
    {
        if (bellIndex < 0 || bellIndex >= model_.maxBells)
            return;
        const auto& bell = model_.bells[bellIndex];

        switch (cmd)
        {
        case BellCommand::toggleBypass:
            if (bell.setBypassed)
            {
                const bool now = bell.getBypassed ? bell.getBypassed() >= 0.5f : false;
                bell.setBypassed (now ? 0.0f : 1.0f, true, true);
            }
            break;
        case BellCommand::resetGain:
            if (bell.setGainNorm)
                bell.setGainNorm (model_.dbToGainNorm (0.0f), true, true);
            break;
        case BellCommand::resetQ:
            if (bell.setQNorm)
                bell.setQNorm (model_.defaultQNorm, true, true);
            break;
        case BellCommand::resetBand:
            if (bell.setGainNorm)
                bell.setGainNorm (model_.dbToGainNorm (0.0f), true, true);
            if (bell.setQNorm)
                bell.setQNorm (model_.defaultQNorm, true, true);
            break;
        case BellCommand::deleteBell:
            if (model_.removeBell)
                model_.removeBell (bellIndex);
            selectedKind_ = NodeKind::none;
            selectedIndex_ = -1;
            break;
        }

        rebuildCurve();
        updateBellPanel();
        repaint();
    }

    /** The effective sample rate the nominal curve and node geometry use
        (kept current by the owning editor's timer). */
    double getEffectiveSampleRate() const noexcept { return sampleRate_; }

    /** ONE production Bell-position authority. All three paths — the visible
        node (drawBellNodes), the hit test and the panel anchor — consume the
        EXACT plot-space point of the rendered response curve at the Bell's
        frequency (getNominalCurvePointAtHz).  VISIBLE CENTER == HIT-TEST
        CENTER == PANEL ANCHOR == CURVE PIXEL. */
    juce::Point<float> getBellNodePosition (int bellIndex) const noexcept
    {
        if (bellIndex < 0 || bellIndex >= model_.maxBells)
            return {};
        const auto& bell = model_.bells[bellIndex];
        if (! bell.getFreqNorm || ! bell.getEnabled || bell.getEnabled() < 0.5f)
            return {};
        return getNominalCurvePointAtHz (model_.normToFreq (bell.getFreqNorm()));
    }

    /** Surface-local position of the selected node (panel anchor; empty
        point when nothing Bell-shaped is selected). */
    juce::Point<float> getSelectedNodePosition() const noexcept
    {
        return getBellNodePosition (selectedIndex_);
    }

    /** The floating Bell control panel (child of this surface). */
    BellControlPanel* getBellPanel() noexcept { return bellPanel_.get(); }
    const BellControlPanel* getBellPanel() const noexcept { return bellPanel_.get(); }

    /** ONE authority for the context-menu anchor: a 2x2 screen rect at the
        cursor — the menu therefore appears BESIDE the Bell/cursor, never at
        the component corner. */
    juce::Rectangle<int> contextMenuAnchorAt (const juce::MouseEvent& e) const noexcept
    {
        return { e.getScreenX() - 1, e.getScreenY() - 1, 2, 2 };
    }

    /** Message thread: canonical post-edit hook (panel value changes). */
    void notifyEdited()
    {
        rebuildCurve();
        updateBellPanel();
        repaint();
    }

    // ---- Public node operations ---------------------------------------------

    /** Create a Bell at the given normalized frequency (gain 0 dB).
        Returns false when the 3-bell hard limit is reached. */
    bool createBellAt (float freqNorm)
    {
        return model_.createBellAt ? model_.createBellAt (freqNorm, 0.5f) : false;
    }

    /** Create a Bell at the given normalized frequency AND gain — the
        double-click contract: X → Frequency, Y → Gain.  Returns false when
        the 3-bell hard limit is reached. */
    bool createBellAt (float freqNorm, float gainNorm)
    {
        return model_.createBellAt ? model_.createBellAt (freqNorm, gainNorm) : false;
    }

    /** Inverse of gainToY: click Y (surface-local) → normalized Bell gain
        (clamped to the ±12 dB parameter range).  Uses the SAME canonical
        plot geometry as the visible graph, so a double-click creates the
        Bell exactly where the user clicked. */
    float yToGainNorm (float y) const noexcept
    {
        const auto plot = ApexEqPlotGeometry::plotBounds ((float) getWidth(), (float) getHeight());
        const float t = juce::jlimit (0.0f, 1.0f,
            (y - plot.getY()) / juce::jmax (1.0f, plot.getHeight()));
        const float db = juce::jlimit (-12.0f, 12.0f, (0.5f - t) * (12.0f / 0.35f));
        return model_.dbToGainNorm ? model_.dbToGainNorm (db) : 0.5f;
    }

    /** Remove a Bell (enabled=false; its values persist for reuse). */
    void removeBell (int bellIndex)
    {
        if (model_.removeBell)
            model_.removeBell (bellIndex);
    }

    /** Reset a Bell's gain to exactly 0 dB; stays enabled, freq/Q preserved. */
    void resetBellGain (int bellIndex)
    {
        if (bellIndex < 0 || bellIndex >= model_.maxBells)
            return;
        const auto& bell = model_.bells[bellIndex];
        if (bell.setGainNorm)
            bell.setGainNorm (model_.dbToGainNorm (0.0f), true, true);
    }

    /** Set the HPF cutoff (normalized; 0 = OFF) through the gesture pipeline. */
    void setHpfNorm (float value)
    {
        if (model_.setHpfNorm)
            model_.setHpfNorm (juce::jlimit (0.0f, 1.0f, value), true, true);
    }

    /** Set the LPF cutoff (normalized; 1 = OFF) through the gesture pipeline. */
    void setLpfNorm (float value)
    {
        if (model_.setLpfNorm)
            model_.setLpfNorm (juce::jlimit (0.0f, 1.0f, value), true, true);
    }

    // ---- Readout formatting (shared by tests and rendering) ----------------

    static juce::String formatHz (float hz) noexcept
    {
        if (hz >= 10000.0f)
            return juce::String (hz / 1000.0f, 1) + " kHz";
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, 2) + " kHz";
        return juce::String (juce::roundToInt (hz)) + " Hz";
    }

    static juce::String formatDb (float db) noexcept
    {
        return juce::String (db >= 0.0f ? "+" : "-")
             + juce::String (std::abs (db), 1) + " dB";
    }

    static juce::String formatQ (float q) noexcept
    {
        return juce::String (q, q >= 10.0f ? 1 : 2);
    }

private:
    enum class NodeKind { none, hpf, lpf, bell };

    struct HitResult { NodeKind kind = NodeKind::none; int index = -1; };

    static juce::String describeHit (const HitResult& hit)
    {
        switch (hit.kind)
        {
            case NodeKind::bell: return "bell" + juce::String (hit.index + 1);
            case NodeKind::hpf:  return "hpf";
            case NodeKind::lpf:  return "lpf";
            default:             return "none";
        }
    }

    // ---- Geometry ----------------------------------------------------------

    float xToFreqNorm (float x) const noexcept
    {
        const auto plot = ApexEqPlotGeometry::plotBounds ((float) getWidth(), (float) getHeight());
        if (plot.getWidth() <= 0.0f) return 0.5f;
        const float logMin = std::log (juce::jmax (1.0f, model_.minFreqHz));
        const float logMax = std::log (juce::jmax (1.0f, model_.maxFreqHz));
        const float t = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());
        const float freq = std::exp (logMin + t * (logMax - logMin));
        return model_.freqToNorm ? model_.freqToNorm (freq) : t;
    }

    // ---- Rendering ----------------------------------------------------------

    void paint (juce::Graphics& g) override
    {
        if (! model_.isValid())
            return;

        drawNominalCurve (g);
        drawHpfLpfNodes (g);
        drawBellNodes (g);
        drawStatusMessage (g);
    }

    void drawNominalCurve (juce::Graphics& g)
    {
        if (curvePath_.isEmpty())
            return;

        // Two restrained strokes: dim underlay + accent core (G10 language).
        g.setColour (juce::Colour (0x30FFFFFF));
        g.strokePath (curvePath_, juce::PathStrokeType (2.2f));
        g.setColour (juce::Colour (0xCCB8A0D8)); // restrained violet-maroon accent
        g.strokePath (curvePath_, juce::PathStrokeType (1.1f));
    }

    void drawHpfLpfNodes (juce::Graphics& g)
    {
        const float hpfNorm = model_.getHpfNorm();
        const float lpfNorm = model_.getLpfNorm();
        const bool hpfActive = hpfNorm > 0.0f;
        const bool lpfActive = lpfNorm < 1.0f;

        // OFF handles: always visible near the graph edges, fully inside the
        // plot, dimmer than active, clearly draggable (larger hit target).
        if (! hpfActive)
        {
            const auto plot = ApexEqPlotGeometry::plotBounds ((float) getWidth(), (float) getHeight());
            const float x = plot.getX() + ApexEqPlotGeometry::kOffHandleInset;
            drawCutoffHandle (g, x, "HPF\nOFF", NodeKind::hpf, 0, false);
        }
        else
        {
            drawCutoffNode (g, model_.hpfNormToHz (hpfNorm), "HPF", NodeKind::hpf, 0);
        }

        if (! lpfActive)
        {
            const auto plot = ApexEqPlotGeometry::plotBounds ((float) getWidth(), (float) getHeight());
            const float x = plot.getRight() - ApexEqPlotGeometry::kOffHandleInset;
            drawCutoffHandle (g, x, "LPF\nOFF", NodeKind::lpf, 1, false);
        }
        else
        {
            drawCutoffNode (g, model_.lpfNormToHz (lpfNorm), "LPF", NodeKind::lpf, 1);
        }
    }

    // APEX-owned angular cut glyph shared by the HPF/LPF nodes and their OFF
    // handles: HPF = rising chevron (cuts below), LPF = mirrored falling
    // chevron (cuts above). Deliberately NOT circular — the user must
    // immediately distinguish HPF / Bell / LPF.
    // APEX cut-filter control point: a compact angular DIAMOND with a
    // directional cut slash (HPF slash rises = cuts below, LPF slash falls
    // = cuts above). Deliberately NOT circular — Bells own the circle, so
    // HPF / Bell / LPF are identified instantly. States: OFF dim / ACTIVE
    // strong / SELECTED premium APEX highlight.
    static void buildCutGlyph (juce::Path& diamond, juce::Line<float>& slash,
                               float x, float y, float s, bool isHpf) noexcept
    {
        diamond.startNewSubPath (x, y - s);
        diamond.lineTo (x + s, y);
        diamond.lineTo (x, y + s);
        diamond.lineTo (x - s, y);
        diamond.closeSubPath();
        if (isHpf)
            slash = juce::Line<float> (x - s * 0.55f, y + s * 0.5f,
                                       x + s * 0.55f, y - s * 0.5f);
        else
            slash = juce::Line<float> (x - s * 0.55f, y - s * 0.5f,
                                       x + s * 0.55f, y + s * 0.5f);
    }

    void drawCutoffNode (juce::Graphics& g, float hz, const juce::String& label,
                         NodeKind kind, int index)
    {
        const float x = freqToX (hz);
        const float y = gainToY (nominalDbAtHz (hz));
        const bool selected = (selectedKind_ == kind && selectedIndex_ == index);
        const bool hovered = (hoverKind_ == kind && hoverIndex_ == index);
        const bool active = selected || hovered;
        const bool isHpf = (kind == NodeKind::hpf);

        // Vertical marker line.
        juce::Path marker;
        marker.startNewSubPath (x, y - 13.0f);
        marker.lineTo (x, y + 13.0f);
        g.setColour (selected ? juce::Colour (0xE0C8A8E8)
                    : active ? juce::Colour (0xA0B8A0D8)
                             : juce::Colour (0x70B8A0D8));
        g.strokePath (marker, juce::PathStrokeType (selected ? 2.0f : 1.1f));

        // Selected aura.
        if (selected)
        {
            g.setColour (juce::Colour (0x24C8A8E8));
            g.drawEllipse (x - 10.5f, y - 10.5f, 21.0f, 21.0f, 3.0f);
        }

        // Diamond body + directional cut slash (intentional, elegant, and
        // immediately recognizable as a cutoff filter).
        const float s = selected ? 5.5f : 4.5f;
        juce::Path diamond;
        juce::Line<float> slash;
        buildCutGlyph (diamond, slash, x, y, s, isHpf);
        g.setColour (selected ? juce::Colour (0xFFE0C8F0)
                    : active ? juce::Colour (0xC8B8A0D8)
                             : juce::Colour (0x90B8A0D8));
        g.strokePath (diamond, juce::PathStrokeType (selected ? 2.0f : 1.3f,
                      juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colour (0xD8141419));
        g.fillPath (diamond);
        g.setColour (selected ? juce::Colour (0xFFC8A8E8)
                    : active ? juce::Colour (0xC0B8A0D8)
                             : juce::Colour (0x80B8A0D8));
        g.drawLine (slash, selected ? 1.8f : 1.2f);

        if (active)
        {
            drawNodeLabel (g, label + "\n" + formatHz (hz), x, y - 16.0f,
                          false, kind, index);
        }
    }

    void drawCutoffHandle (juce::Graphics& g, float x, const juce::String& label,
                           NodeKind kind, int index, bool /*active*/)
    {
        // OFF handle: the SAME diamond cut-control family, dim but clearly
        // visible, fully INSIDE the plot (never at x=0 / x=width).
        const auto plot = ApexEqPlotGeometry::plotBounds ((float) getWidth(), (float) getHeight());
        const float y = plot.getY() + plot.getHeight() * 0.82f;
        const bool selected = (selectedKind_ == kind && selectedIndex_ == index);
        const bool hovered = (hoverKind_ == kind && hoverIndex_ == index);
        const bool active = selected || hovered;
        const bool isHpf = (kind == NodeKind::hpf);

        // Subtle vertical line at the graph edge (inside the plot).
        g.setColour (active ? juce::Colour (0x50B8A0D8) : juce::Colour (0x22B8A0D8));
        g.drawLine (x, plot.getY() + 6.0f, x, plot.getBottom() - 4.0f, 0.6f);

        const float s = selected ? 4.6f : 3.8f;
        juce::Path diamond;
        juce::Line<float> slash;
        buildCutGlyph (diamond, slash, x, y, s, isHpf);
        g.setColour (active ? juce::Colour (0xA0B8A0D8) : juce::Colour (0x48B8A0D8));
        g.strokePath (diamond, juce::PathStrokeType (selected ? 1.8f : 1.1f,
                      juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colour (0xB0141419));
        g.fillPath (diamond);
        g.setColour (active ? juce::Colour (0x80B8A0D8) : juce::Colour (0x38B8A0D8));
        g.drawLine (slash, 1.0f);

        if (active)
        {
            g.setFont (juce::Font (8.5f));
            g.setColour (juce::Colour (0x80B8A0D8));
            const float tw = g.getCurrentFont().getStringWidth ("OFF");
            g.drawSingleLineText ("OFF", juce::roundToInt (x - tw * 0.5f),
                                  juce::roundToInt (y + 12.0f));
        }
    }

    void drawBellNodes (juce::Graphics& g)
    {
        for (int i = 0; i < model_.maxBells; ++i)
        {
            const auto& bell = model_.bells[i];
            if (! bell.getEnabled || bell.getEnabled() < 0.5f)
                continue;

            const float freqNorm = bell.getFreqNorm ? bell.getFreqNorm() : 0.5f;
            const float hz = model_.normToFreq (freqNorm);
            const auto node = getBellNodePosition (i);
            const float x = node.x;
            const float y = node.y;
            const bool isSelected = (selectedKind_ == NodeKind::bell && selectedIndex_ == i);
            const bool isHovered = (hoverKind_ == NodeKind::bell && hoverIndex_ == i);
            const bool bypassed = bell.getBypassed ? bell.getBypassed() >= 0.5f : false;

            // Premium circular EQ node: soft aura + elegant ring + precise
            // filled core. NORMAL elegant / HOVER brighter / SELECTED strong
            // APEX aura / BYPASSED dim hollow ring (Bell present, out of
            // circuit). The interaction target stays larger than the visible
            // node (hitTestNodes uses a 12 px radius).
            const float r = isSelected ? 6.0f : 5.0f;

            if (isSelected)
            {
                g.setColour (juce::Colour (0x34C8A8E8));
                g.drawEllipse (x - r - 4.0f, y - r - 4.0f,
                               (r + 4.0f) * 2.0f, (r + 4.0f) * 2.0f, 1.8f);
            }
            else if (isHovered && ! bypassed)
            {
                g.setColour (juce::Colour (0x2EC8A8E8));
                g.drawEllipse (x - r - 4.0f, y - r - 4.0f,
                               (r + 4.0f) * 2.0f, (r + 4.0f) * 2.0f, 1.6f);
            }

            if (bypassed)
            {
                g.setColour (isHovered ? juce::Colour (0x70C8A8E8) : juce::Colour (0x3AB8A0D8));
                g.drawEllipse (x - r, y - r, r * 2.0f, r * 2.0f, 1.4f);
            }
            else
            {
                g.setColour (isSelected ? juce::Colour (0xFFE0C8F0)
                            : isHovered ? juce::Colour (0xE0C8A8E8)
                                        : juce::Colour (0xA8B8A0D8));
                g.drawEllipse (x - r, y - r, r * 2.0f, r * 2.0f,
                               isSelected ? 2.0f : 1.4f);
                g.setColour (juce::Colour (0xE6141419));
                g.fillEllipse (x - r + 1.0f, y - r + 1.0f,
                               (r - 1.0f) * 2.0f, (r - 1.0f) * 2.0f);
            }

            // Precise centre dot.
            g.setColour (isSelected ? juce::Colour (0xFFC8A8E8) : juce::Colour (0xA0B8A0D8));
            g.fillEllipse (x - 1.1f, y - 1.1f, 2.2f, 2.2f);

            // Compact readable B-label near the node. HOVER shows NO box at
            // all — a pure node highlight (the old black readout box is
            // gone; the inspector appears only when the Bell is SELECTED).
            const juce::String tag = "B" + juce::String (i + 1);
            g.setFont (juce::Font ((isSelected || isHovered) ? 8.5f : 8.0f,
                                   (isSelected || isHovered) ? juce::Font::bold
                                                             : juce::Font::plain));
            g.setColour (isSelected ? juce::Colour (0xFFC8A8E8)
                        : isHovered ? juce::Colour (0xB0B8A0D8)
                                    : juce::Colour (0x70B8A0D8));
            const float tw = g.getCurrentFont().getStringWidth (tag);
            g.drawSingleLineText (tag, juce::roundToInt (x - tw * 0.5f),
                                  juce::roundToInt (y - r - 2.0f));
        }
    }

    void drawNodeLabel (juce::Graphics& g, const juce::String& text, float x, float y,
                        bool small, NodeKind, int)
    {
        const float fontSize = small ? 8.5f : 9.5f;
        g.setFont (juce::Font (fontSize, juce::Font::bold));
        const float w = g.getCurrentFont().getStringWidth (text) + 8.0f;
        const float h = fontSize + 6.0f;
        float bx = juce::jlimit (0.0f, (float) getWidth() - w, x - w / 2.0f);
        float by = y - h - 4.0f;
        if (by < 0.0f)
            by = y + 14.0f;
        if (by + h > (float) getHeight())
            by = (float) getHeight() - h;

        g.setColour (juce::Colour (0xC0000000));
        g.fillRoundedRectangle (bx, by, w, h, 3.0f);
        g.setColour (juce::Colour (0xD8D8D8D8));
        g.drawText (text, juce::Rectangle<float> (bx, by, w, h), juce::Justification::centred);
    }

    void drawStatusMessage (juce::Graphics& g)
    {
        if (statusMessage_.isEmpty())
            return;
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        if (now - statusShownAtMs_ > 1800)
        {
            statusMessage_.clear();
            return;
        }
        g.setFont (juce::Font (9.0f, juce::Font::bold));
        const float w = g.getCurrentFont().getStringWidth (statusMessage_) + 12.0f;
        const float h = 16.0f;
        const float bx = ((float) getWidth() - w) / 2.0f;
        const float by = 4.0f;
        g.setColour (juce::Colour (0xC0000000));
        g.fillRoundedRectangle (bx, by, w, h, 3.0f);
        g.setColour (juce::Colour (0xFFD0A8E0));
        g.drawText (statusMessage_, juce::Rectangle<float> (bx, by, w, h), juce::Justification::centred);
    }

    // ---- Nominal curve cache --------------------------------------------------

    void rebuildCurve()
    {
        curvePath_.clear();
        curvePoints_.clear();
        if (! model_.isValid() || getWidth() <= 0)
            return;

        constexpr int kPoints = 257;
        const float logMin = std::log (juce::jmax (1.0f, model_.minFreqHz));
        const float logMax = std::log (juce::jmax (1.0f, model_.maxFreqHz));

        // ONE coordinate authority: the curve is sampled in the SAME plot
        // space the nodes use (freqToX + responseDbToPlotY), so a node and
        // the curve can never disagree in X or Y.
        //
        // Every enabled Bell centre frequency is INSERTED into the sample
        // set.  With only the fixed log grid a narrow-Q peak (e.g. Q 40 at
        // 1 kHz, ~25 Hz wide) can fall entirely BETWEEN two samples — the
        // polyline would visibly miss the peak while the node sits on the
        // analytical top, which is exactly the "node floats above the
        // curve" failure.  Inserting the Bell frequencies guarantees the
        // rendered path passes through the analytical response at every
        // Bell centre.
        // ONE coordinate authority: the curve is sampled in the SAME plot
        // space the nodes use (freqToX + responseDbToPlotY), so a node and
        // the curve can never disagree in X or Y.
        //
        // Every enabled Bell centre frequency is INSERTED into the sample
        // set.  With only the fixed log grid a narrow-Q peak (e.g. Q 40 at
        // 1 kHz, ~25 Hz wide) can fall entirely BETWEEN two samples — the
        // polyline would visibly miss the peak while the node sits on the
        // analytical top, which is exactly the "node floats above the
        // curve" failure.  Inserting the Bell frequencies guarantees the
        // rendered path passes through the analytical response at every
        // Bell centre.
        std::vector<float> sampleHz;
        sampleHz.reserve (kPoints + model_.maxBells);
        for (int i = 0; i < kPoints; ++i)
            sampleHz.push_back (std::exp (logMin + (logMax - logMin)
                                          * (float) i / (float) (kPoints - 1)));
        for (int i = 0; i < model_.maxBells; ++i)
        {
            const auto& bell = model_.bells[i];
            if (bell.getFreqNorm && bell.getEnabled && bell.getEnabled() >= 0.5f)
                sampleHz.push_back (model_.normToFreq (bell.getFreqNorm()));
        }
        std::sort (sampleHz.begin(), sampleHz.end());

        bool started = false;
        for (const float hz : sampleHz)
        {
            const float db = juce::jlimit (-12.0f, 12.0f, nominalDbAtHz (hz));
            const float x = freqToX (hz);
            const float y = responseDbToPlotY (db);
            if (! started)
            {
                curvePath_.startNewSubPath (x, y);
                curvePoints_.emplace_back (x, y);
                started = true;
            }
            else
            {
                curvePath_.lineTo (x, y);
                curvePoints_.emplace_back (x, y);
            }
        }
    }

    float nominalDbAtHz (float hz) const noexcept
    {
        if (! model_.nominalMagnitude)
            return 0.0f;
        const float g = model_.nominalMagnitude (hz, (float) sampleRate_);
        return 20.0f * std::log10 (juce::jmax (1.0e-30f, g));
    }

    // ---- Interaction -----------------------------------------------------------

    HitResult hitTestNodes (const juce::Point<float>& pos) const
    {
        // Bells first (topmost), then HPF/LPF.
        for (int i = model_.maxBells - 1; i >= 0; --i)
        {
            const auto& bell = model_.bells[i];
            if (! bell.getEnabled || ! bell.getFreqNorm || ! bell.getGainNorm
                || bell.getEnabled() < 0.5f)
                continue;
            const float hz = model_.normToFreq (bell.getFreqNorm());
            // ONE geometry authority shared by drawBellNodes, getSelectedNode-
            // Position and hitTestNodes — see getBellNodePosition below.
            const auto p = getBellNodePosition (i);
            if (pos.getDistanceFrom (p) <= 12.0f)
                return { NodeKind::bell, i };
        }

        // HPF: always hittable (OFF handle at left plot edge or active node)
        if (model_.getHpfNorm)
        {
            const float hpfNorm = model_.getHpfNorm();
            float x;
            if (hpfNorm > 0.0f)
                x = freqToX (model_.hpfNormToHz (hpfNorm));
            else
            {
                const auto plot = ApexEqPlotGeometry::plotBounds ((float) getWidth(), (float) getHeight());
                x = plot.getX() + ApexEqPlotGeometry::kOffHandleInset;
            }
            if (std::abs (pos.x - x) <= ApexEqPlotGeometry::kHitRadius)
                return { NodeKind::hpf, 0 };
        }

        // LPF: always hittable (OFF handle at right plot edge or active node)
        if (model_.getLpfNorm)
        {
            const float lpfNorm = model_.getLpfNorm();
            float x;
            if (lpfNorm < 1.0f)
                x = freqToX (model_.lpfNormToHz (lpfNorm));
            else
            {
                const auto plot = ApexEqPlotGeometry::plotBounds ((float) getWidth(), (float) getHeight());
                x = plot.getRight() - ApexEqPlotGeometry::kOffHandleInset;
            }
            if (std::abs (pos.x - x) <= ApexEqPlotGeometry::kHitRadius)
                return { NodeKind::lpf, 1 };
        }

        return {};
    }

    // Mouse/gesture handlers are PUBLIC so real-hierarchy tests can deliver
    // synthetic events through the exact handler path the framework invokes
    // (the routing itself is proven via Component::getComponentAt).
public:
    void mouseMove (const juce::MouseEvent& e) override
    {
        lastMousePos_ = e.position;
        const auto hit = hitTestNodes (e.position);
        hoverKind_ = hit.kind;
        hoverIndex_ = hit.index;
        trace ("mouseMove pos=" + juce::String (e.position.x, 1) + "," + juce::String (e.position.y, 1)
               + " bounds=" + getBounds().toString() + " hit=" + describeHit (hit));
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverKind_ = NodeKind::none;
        hoverIndex_ = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        lastMousePos_ = e.position;
        const auto hit = hitTestNodes (e.position);
        trace ("mouseDown pos=" + juce::String (e.position.x, 1) + "," + juce::String (e.position.y, 1)
               + " visible=" + juce::String ((int) isVisible())
               + " parent=" + (getParentComponent() ? getParentComponent()->getName() : "null")
               + " hit=" + describeHit (hit));
        grabKeyboardFocus();

        // v3: Alt + click toggles the individual Bell bypass (ONE command
        // authority — the same path as the panel button and the menu).
        if (e.mods.isAltDown() && hit.kind == NodeKind::bell)
        {
            runBellCommand (BellCommand::toggleBypass, hit.index);
            return;
        }

        if (e.mods.isPopupMenu())
        {
            if (hit.kind == NodeKind::bell)
            {
                selectedKind_ = NodeKind::bell;
                selectedIndex_ = hit.index;
                const bool bypassed = model_.bells[hit.index].getBypassed
                    ? model_.bells[hit.index].getBypassed() >= 0.5f : false;
                juce::PopupMenu menu;
                menu.addItem (1, bypassed ? "Enable Bell " + juce::String (hit.index + 1)
                                         : "Bypass Bell " + juce::String (hit.index + 1));
                menu.addSeparator();
                menu.addItem (2, "Reset Gain");
                menu.addItem (3, "Reset Q");
                menu.addItem (4, "Reset Band");
                menu.addSeparator();
                menu.addItem (5, "Delete Bell " + juce::String (hit.index + 1));
                // Anchor the menu BESIDE the Bell/cursor (never the bottom-left
                // of the component) — single authority: contextMenuAnchorAt.
                const juce::Rectangle<int> anchor = contextMenuAnchorAt (e);
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (anchor),
                    [this, hit] (int result)
                    {
                        switch (result)
                        {
                        case 1: runBellCommand (BellCommand::toggleBypass, hit.index); break;
                        case 2: runBellCommand (BellCommand::resetGain, hit.index); break;
                        case 3: runBellCommand (BellCommand::resetQ, hit.index); break;
                        case 4: runBellCommand (BellCommand::resetBand, hit.index); break;
                        case 5: runBellCommand (BellCommand::deleteBell, hit.index); break;
                        default: break;
                        }
                    });
            }
            else
            {
                selectedKind_ = NodeKind::none;
                selectedIndex_ = -1;
                updateBellPanel();
                repaint();
            }
            return;
        }

        selectedKind_ = hit.kind;
        selectedIndex_ = hit.index;
        dragKind_ = hit.kind;
        dragIndex_ = hit.index;
        dragOrigin_ = e.position;
        dragStartFreqNorm_ = dragKind_ == NodeKind::bell
            ? model_.bells[dragIndex_].getFreqNorm() : 0.5f;
        dragStartGainNorm_ = dragKind_ == NodeKind::bell
            ? model_.bells[dragIndex_].getGainNorm() : 0.5f;
        dragStartQNorm_ = dragKind_ == NodeKind::bell
            ? model_.bells[dragIndex_].getQNorm() : 0.5f;
        dragStartHpfNorm_ = model_.getHpfNorm();
        dragStartLpfNorm_ = model_.getLpfNorm();
        gestureActive_ = false;
        qGestureActive_ = false;
        updateBellPanel();
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        lastMousePos_ = e.position;
        if (dragKind_ == NodeKind::none)
            return;

        trace ("mouseDrag pos=" + juce::String (e.position.x, 1) + "," + juce::String (e.position.y, 1)
               + " drag=" + describeHit ({ dragKind_, dragIndex_ }));

        const bool fine = e.mods.isShiftDown();
        const float dx = e.position.x - dragOrigin_.x;
        const float dy = e.position.y - dragOrigin_.y;
        const float xScale = fine ? 0.08f : 1.0f;
        const float yScale = fine ? 0.08f : 1.0f;
        const auto plot = ApexEqPlotGeometry::plotBounds ((float) getWidth(), (float) getHeight());
        const float plotW = juce::jmax (1.0f, plot.getWidth());
        const float plotH = juce::jmax (1.0f, plot.getHeight());

        beginGestureIfNeeded();

        if (dragKind_ == NodeKind::bell)
        {
            const auto& bell = model_.bells[dragIndex_];
            const float dNormX = (dx / plotW) * xScale;
            const float dNormY = -(dy / (plotH * 0.35f)) * 0.5f * yScale;
            if (e.mods.isCtrlDown())
            {
                // Ctrl + vertical drag = Q (log/perceptual mapping). The Q
                // gesture begins lazily on the first Ctrl sample and ends on
                // release (or when Ctrl is released mid-drag).
                if (! qGestureActive_)
                {
                    bell.setQNorm (dragStartQNorm_, true, false);
                    qGestureActive_ = true;
                }
                bell.setQNorm (juce::jlimit (0.0f, 1.0f, dragStartQNorm_ + dNormY), false, false);
            }
            else
            {
                if (qGestureActive_)
                {
                    bell.setQNorm (bell.getQNorm(), false, true);
                    qGestureActive_ = false;
                }
                const float freqNorm = juce::jlimit (0.0f, 1.0f, dragStartFreqNorm_ + dNormX);
                const float gainNorm = juce::jlimit (0.0f, 1.0f, dragStartGainNorm_ + dNormY);
                bell.setFreqNorm (freqNorm, false, false);
                bell.setGainNorm (gainNorm, false, false);
            }
            updateBellPanel(); // the panel follows the node while dragging
        }
        else if (dragKind_ == NodeKind::hpf)
        {
            // Dragging the OFF handle RIGHT engages the HPF and sets the
            // cutoff from the drag; dragging an active node moves its cutoff.
            const float dNormX = (dx / plotW) * xScale;
            model_.setHpfNorm (juce::jlimit (0.0f, 1.0f, dragStartHpfNorm_ + dNormX), false, false);
        }
        else if (dragKind_ == NodeKind::lpf)
        {
            const float dNormX = (dx / plotW) * xScale;
            model_.setLpfNorm (juce::jlimit (0.0f, 1.0f, dragStartLpfNorm_ + dNormX), false, false);
        }

        rebuildCurve();
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragKind_ == NodeKind::bell && qGestureActive_)
        {
            model_.bells[dragIndex_].setQNorm (model_.bells[dragIndex_].getQNorm(), false, true);
            qGestureActive_ = false;
        }
        if (dragKind_ != NodeKind::none)
            endGesture();
        dragKind_ = NodeKind::none;
        dragIndex_ = -1;
        updateBellPanel();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto hit = hitTestNodes (e.position);
        trace ("mouseDoubleClick pos=" + juce::String (e.position.x, 1) + "," + juce::String (e.position.y, 1)
               + " hit=" + describeHit (hit));
        if (hit.kind == NodeKind::bell)
        {
            // Reset gain to 0 dB (keep freq/Q, stay enabled) — command route.
            runBellCommand (BellCommand::resetGain, hit.index);
            selectedKind_ = NodeKind::bell;
            selectedIndex_ = hit.index;
            updateBellPanel();
            repaint();
            return;
        }

        if (hit.kind == NodeKind::hpf)
        {
            model_.setHpfNorm (0.0f, true, true); // OFF
            selectedKind_ = NodeKind::none;
            updateBellPanel();
            repaint();
            return;
        }

        if (hit.kind == NodeKind::lpf)
        {
            model_.setLpfNorm (1.0f, true, true); // OFF
            selectedKind_ = NodeKind::none;
            updateBellPanel();
            repaint();
            return;
        }

        // Empty space: create a Bell EXACTLY where the user clicked.
        // X → Frequency, Y → Gain (the same plot geometry as the graph).
        const int count = model_.enabledBellCount();
        if (count >= model_.maxBells)
        {
            showStatus ("3/3 Bells active");
            return;
        }

        const float freqNorm = xToFreqNorm (e.position.x);
        const float gainNorm = yToGainNorm (e.position.y);
        if (model_.createBellAt (freqNorm, gainNorm))
        {
            // Select the newly created bell (first enabled at the requested
            // frequency; fall back to the first enabled slot).
            for (int i = 0; i < model_.maxBells; ++i)
            {
                const auto& bell = model_.bells[i];
                if (bell.getEnabled && bell.getEnabled() >= 0.5f)
                {
                    if (std::abs (bell.getFreqNorm() - freqNorm) < 1.0e-4f)
                    {
                        selectedKind_ = NodeKind::bell;
                        selectedIndex_ = i;
                        break;
                    }
                    if (selectedKind_ != NodeKind::bell)
                    {
                        selectedKind_ = NodeKind::bell;
                        selectedIndex_ = i;
                    }
                }
            }
        }
        updateBellPanel();
        rebuildCurve();
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        lastMousePos_ = e.position;
        const auto hit = hitTestNodes (e.position);
        trace ("mouseWheel pos=" + juce::String (e.position.x, 1) + "," + juce::String (e.position.y, 1)
               + " deltaY=" + juce::String (wheel.deltaY, 3) + " hit=" + describeHit (hit));
        if (hit.kind != NodeKind::bell)
            return;

        const auto& bell = model_.bells[hit.index];
        const bool fine = e.mods.isShiftDown();
        const float delta = wheel.deltaY * (fine ? 0.01f : 0.05f);
        const float qNorm = juce::jlimit (0.0f, 1.0f, bell.getQNorm() + delta);
        bell.setQNorm (qNorm, true, true);
        selectedKind_ = NodeKind::bell;
        selectedIndex_ = hit.index;
        updateBellPanel();
        rebuildCurve();
        repaint();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (selectedKind_ == NodeKind::bell
            && (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey))
        {
            // Delete key / Backspace -> the SAME delete command as the panel
            // button and the context menu.
            runBellCommand (BellCommand::deleteBell, selectedIndex_);
            return true;
        }
        return false;
    }

    void removeSelectedNode()
    {
        if (selectedKind_ != NodeKind::bell)
            return;
        runBellCommand (BellCommand::deleteBell, selectedIndex_);
    }

    void beginGestureIfNeeded()
    {
        if (gestureActive_)
            return;
        gestureActive_ = true;

        if (dragKind_ == NodeKind::bell)
        {
            const auto& bell = model_.bells[dragIndex_];
            bell.setFreqNorm (dragStartFreqNorm_, true, false);
            bell.setGainNorm (dragStartGainNorm_, true, false);
        }
        else if (dragKind_ == NodeKind::hpf)
        {
            model_.setHpfNorm (dragStartHpfNorm_, true, false);
        }
        else if (dragKind_ == NodeKind::lpf)
        {
            model_.setLpfNorm (dragStartLpfNorm_, true, false);
        }
    }

    void endGesture()
    {
        if (! gestureActive_)
            return;
        gestureActive_ = false;

        if (dragKind_ == NodeKind::bell)
        {
            const auto& bell = model_.bells[dragIndex_];
            bell.setFreqNorm (bell.getFreqNorm(), false, true);
            bell.setGainNorm (bell.getGainNorm(), false, true);
        }
        else if (dragKind_ == NodeKind::hpf)
        {
            model_.setHpfNorm (model_.getHpfNorm(), false, true);
        }
        else if (dragKind_ == NodeKind::lpf)
        {
            model_.setLpfNorm (model_.getLpfNorm(), false, true);
        }
    }

    void showStatus (const juce::String& message)
    {
        statusMessage_ = message;
        statusShownAtMs_ = juce::Time::getMillisecondCounter();
        repaint();
    }

    // ---- Floating control panel --------------------------------------------
    void updateBellPanel()
    {
        if (bellPanel_ == nullptr)
            return;
        // Interaction-state contract:
        //   selected + idle        → full compact inspector (smart placement)
        //   selected + dragging    → tiny micro HUD (live values only)
        //   nothing selected       → hidden
        const bool interacting = (dragKind_ == NodeKind::bell && dragIndex_ >= 0);
        bellPanel_->setMode (interacting ? BellControlPanel::Mode::hud
                                         : BellControlPanel::Mode::full);
        bellPanel_->updatePanel();
        bellPanel_->updatePosition();
    }

    void resized() override
    {
        // Re-clamp the floating panel when the analyzer/surface resizes.
        if (bellPanel_ != nullptr)
            bellPanel_->updatePosition();
    }

    Model model_;
    double sampleRate_ = 48000.0;

    NodeKind selectedKind_ = NodeKind::none;
    int selectedIndex_ = -1;
    NodeKind hoverKind_ = NodeKind::none;
    int hoverIndex_ = -1;

    // Drag state.
    NodeKind dragKind_ = NodeKind::none;
    int dragIndex_ = -1;
    juce::Point<float> dragOrigin_;
    float dragStartFreqNorm_ = 0.5f;
    float dragStartGainNorm_ = 0.5f;
    float dragStartQNorm_ = 0.5f;
    float dragStartHpfNorm_ = 0.0f;
    float dragStartLpfNorm_ = 1.0f;
    bool gestureActive_ = false;
    bool qGestureActive_ = false; // Ctrl-drag Q gesture in flight

    juce::Point<float> lastMousePos_ = {}; // avoidance-zone cursor tracking

public:
    /** Surface-local cursor position (for smart non-obstructive placement). */
    juce::Point<float> getLastMousePos() const noexcept { return lastMousePos_; }

private:

    std::unique_ptr<BellControlPanel> bellPanel_;

    juce::Path curvePath_;

    /** The exact vertices of the rendered nominal curve (the same points
        paint() strokes).  Exposed so tests can verify that a Bell node
        centre physically coincides with a rendered curve vertex. */
    std::vector<juce::Point<float>> curvePoints_;

    juce::String statusMessage_;
    juce::uint32 statusShownAtMs_ = 0;
};

// ============================================================================
// BellControlPanel — implementation. Defined after ApexInteractiveEQSurface
// so the owner is complete; the surface itself only needs the panel's
// declaration (created in the surface constructor).
// ============================================================================

inline BellControlPanel::BellControlPanel (ApexInteractiveEQSurface& owner)
    : owner_ (owner),
      freq_ (*this, Field::freq),
      gain_ (*this, Field::gain),
      q_ (*this, Field::q)
{
    setInterceptsMouseClicks (true, true);
    setSize (kPanelW, kPanelH);

    title_.setText ("B1", juce::dontSendNotification);
    title_.setFont (juce::Font (11.0f, juce::Font::bold));
    title_.setColour (juce::Label::textColourId, juce::Colour (0xFFC8A8E8));
    title_.setInterceptsMouseClicks (false, false);

    captionFreq_.setText ("FREQ", juce::dontSendNotification);
    captionGain_.setText ("GAIN", juce::dontSendNotification);
    captionQ_.setText ("Q", juce::dontSendNotification);
    for (auto* c : { &captionFreq_, &captionGain_, &captionQ_ })
    {
        c->setFont (juce::Font (7.5f, juce::Font::bold));
        c->setColour (juce::Label::textColourId, juce::Colour (0xFF8A8498));
        c->setInterceptsMouseClicks (false, false);
    }

    addAndMakeVisible (title_);
    addAndMakeVisible (captionFreq_);
    addAndMakeVisible (freq_);
    addAndMakeVisible (captionGain_);
    addAndMakeVisible (gain_);
    addAndMakeVisible (captionQ_);
    addAndMakeVisible (q_);

    // All controls route through the ONE command authority.
    bypassButton_.setClickingTogglesState (true);
    bypassButton_.setTooltip ("Bypass Bell (Alt+click the node)");
    bypassButton_.onClick = [this]
    {
        owner_.runBellCommand (ApexInteractiveEQSurface::BellCommand::toggleBypass,
                               owner_.getSelectedBellIndex());
    };
    resetButton_.setTooltip ("Reset gain and Q");
    resetButton_.onClick = [this]
    {
        owner_.runBellCommand (ApexInteractiveEQSurface::BellCommand::resetBand,
                               owner_.getSelectedBellIndex());
    };
    deleteButton_.setTooltip ("Delete Bell");
    deleteButton_.onClick = [this]
    {
        owner_.runBellCommand (ApexInteractiveEQSurface::BellCommand::deleteBell,
                               owner_.getSelectedBellIndex());
    };
    addAndMakeVisible (bypassButton_);
    addAndMakeVisible (resetButton_);
    addAndMakeVisible (deleteButton_);

    resized(); // lay the children out immediately — never an empty panel
}

inline void BellControlPanel::paint (juce::Graphics& g)
{
    // Idle selected + cursor elsewhere → slightly lower visual emphasis
    // (softer material/border); full clarity on hover or while dragging.
    const bool hovered = isMouseOver (true);
    const float emphasis = (mode_ == Mode::full && ! hovered) ? 0.72f : 1.0f;

    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const float corner = (mode_ == Mode::hud) ? 6.0f : 8.0f;

    // Soft depth shadow (two translucent layers).
    g.setColour (juce::Colour (0x66000000));
    g.fillRoundedRectangle (b.translated (0.0f, 2.0f), corner);
    g.setColour (juce::Colour (0x33000000));
    g.fillRoundedRectangle (b.translated (0.0f, 1.0f), corner);

    // Dark translucent APEX material.
    g.setColour (juce::Colour (0xEF0B0B0F).withAlpha (0.1f + 0.9f * emphasis));
    g.fillRoundedRectangle (b, corner);

    // Refined border + inner top highlight.
    g.setColour (juce::Colour (0xFFC8A8E8).withAlpha (0.5f * emphasis));
    g.drawRoundedRectangle (b, corner, 1.0f);
    g.setColour (juce::Colour (0x16FFFFFF));
    g.drawLine (b.getX() + 6.0f, b.getY() + 1.2f, b.getRight() - 6.0f, b.getY() + 1.2f, 0.8f);

    if (mode_ == Mode::hud)
    {
        // Micro HUD (live values only — no buttons, no captions):
        //   B2
        //   306 Hz   +11.1 dB
        //   Q 1.00
        g.setFont (juce::Font (9.0f, juce::Font::bold));
        g.setColour (juce::Colour (0xFFC8A8E8));
        g.drawText (title_.getText(), juce::Rectangle<float> (8, 3, 30, 12),
                    juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xFFE9E4F0));
        g.drawText (formatValue (Field::freq, getUnitValue (Field::freq))
                    + "  " + formatValue (Field::gain, getUnitValue (Field::gain)),
                    juce::Rectangle<float> (8, 17, getWidth() - 16, 12),
                    juce::Justification::centredLeft);
        g.drawText ("Q " + formatValue (Field::q, getUnitValue (Field::q)),
                    juce::Rectangle<float> (8, 31, getWidth() - 16, 12),
                    juce::Justification::centredLeft);
    }

    // Small anchor pointer toward the Bell — keeps the HUD/inspector
    // visually connected to the node it controls.
    const float px = b.getCentreX();
    juce::Path pointer;
    switch (placement_)
    {
    case Placement::below: // panel below the node -> pointer on the TOP edge
        pointer.startNewSubPath (px - 5.0f, b.getY() + 0.5f);
        pointer.lineTo (px, b.getY() - 5.0f);
        pointer.lineTo (px + 5.0f, b.getY() + 0.5f);
        break;
    case Placement::above: // pointer on the BOTTOM edge
        pointer.startNewSubPath (px - 5.0f, b.getBottom() - 0.5f);
        pointer.lineTo (px, b.getBottom() + 5.0f);
        pointer.lineTo (px + 5.0f, b.getBottom() - 0.5f);
        break;
    case Placement::left:  // panel left of the node -> pointer on the RIGHT
        pointer.startNewSubPath (b.getRight() - 0.5f, b.getCentreY() - 5.0f);
        pointer.lineTo (b.getRight() + 5.0f, b.getCentreY());
        pointer.lineTo (b.getRight() - 0.5f, b.getCentreY() + 5.0f);
        break;
    case Placement::right: // pointer on the LEFT edge
        pointer.startNewSubPath (b.getX() + 0.5f, b.getCentreY() - 5.0f);
        pointer.lineTo (b.getX() - 5.0f, b.getCentreY());
        pointer.lineTo (b.getX() + 0.5f, b.getCentreY() + 5.0f);
        break;
    case Placement::clamped:
    default:
        break;
    }
    if (! pointer.isEmpty())
    {
        g.setColour (juce::Colour (0xFFC8A8E8).withAlpha (0.5f * emphasis));
        g.fillPath (pointer);
    }
}

inline void BellControlPanel::resized()
{
    const int w = getWidth();

    title_.setBounds (8, 6, 40, 13);
    bypassButton_.setBounds (w - 36, 3, 33, 16);

    auto placeRow = [this, w] (int index, juce::Label& caption, ValueControl& control)
    {
        const int y = 22 + index * 19;
        caption.setBounds (8, y + 2, 34, 11);
        control.setBounds (48, y, w - 54, 17);
    };
    placeRow (0, captionFreq_, freq_);
    placeRow (1, captionGain_, gain_);
    placeRow (2, captionQ_, q_);

    resetButton_.setBounds (8, 79, 60, 13);
    deleteButton_.setBounds (76, 79, 60, 13);
}

inline void BellControlPanel::updatePanel()
{
    const int idx = owner_.getSelectedBellIndex();
    if (idx < 0 || idx >= (int) owner_.getModel().maxBells || ! owner_.getModel().isValid())
    {
        setVisible (false);
        return;
    }
    setVisible (true);

    const auto& bell = owner_.getModel().bells[idx];
    title_.setText ("B" + juce::String (idx + 1), juce::dontSendNotification);
    const bool bypassed = bell.getBypassed ? bell.getBypassed() >= 0.5f : false;
    if (bypassButton_.getToggleState() != bypassed)
        bypassButton_.setToggleState (bypassed, juce::dontSendNotification);
    freq_.repaint();
    gain_.repaint();
    q_.repaint();
    updatePosition();
}

inline void BellControlPanel::updatePosition()
{
    if (! isVisible())
        return;

    const float pw = (float) getWidth();   // current mode size (full or HUD)
    const float ph = (float) getHeight();
    const float margin = 4.0f;
    const float gap = 12.0f;
    const float w = (float) owner_.getWidth();
    const float h = (float) owner_.getHeight();
    const juce::Point<float> node = owner_.getSelectedNodePosition();

    if (node.x <= 0.0f && node.y <= 0.0f)
        return;

    // ---- PANEL-ORIGIN GESTURE LOCK ----------------------------------------
    // While the user is interacting WITH the panel (value drag, wheel,
    // numeric editing), the panel MUST NOT move.  The Bell/curve move
    // underneath it; the control stays under the user's hand.  The bounds
    // are frozen at the first update of the gesture and preserved until the
    // gesture ends — then the normal smart placement runs exactly once.
    if (isAnyInteractionActive())
    {
        if (! frozenDuringInteraction_)
        {
            frozenDuringInteraction_ = true;
            lockedPanelBounds_ = getBounds();
        }
        // Minimal clamp only: if the host shrank during the gesture, keep
        // the panel inside the surface — never smart-reposition mid-gesture.
        const juce::Rectangle<int> clamped = lockedPanelBounds_.constrainedWithin (
            juce::Rectangle<int> (0, 0, (int) w, (int) h));
        setBounds (clamped);
        return;
    }
    frozenDuringInteraction_ = false;

    // ---- STABILITY: never reflow just because the cursor moved ------------
    // Reposition only when the selection, node position, mode or surface
    // size changed, or the current bounds became invalid (out of bounds).
    // The cursor is deliberately NOT part of this key — the panel must
    // never run away from the user.
    const int sel = owner_.getSelectedBellIndex();
    const juce::Point<int> nodeKey (juce::roundToInt (node.x * 0.5f),
                                    juce::roundToInt (node.y * 0.5f));
    const juce::Point<int> sizeKey (juce::roundToInt (pw), juce::roundToInt (ph));
    const bool boundsValid = getX() >= 0 && getRight() <= (int) w
                          && getY() >= 0 && getBottom() <= (int) h;
    if (boundsValid
        && sel == lastPlacementKey_.bell
        && mode_ == lastPlacementKey_.mode
        && nodeKey == lastPlacementKey_.node
        && sizeKey == lastPlacementKey_.size)
        return; // stable — keep the current placement

    // ---- RESTRICTED PLACEMENT FAMILIES ------------------------------------
    // FULL inspector (and the HUD) may ONLY use:
    //   ABOVE, TOP-RIGHT, TOP-LEFT, RIGHT, LEFT
    // BELOW / BOTTOM-LEFT / BOTTOM-RIGHT are FORBIDDEN — the inspector must
    // never sit under the selected Bell.  Priority order: ABOVE, TOP-RIGHT,
    // TOP-LEFT, RIGHT, LEFT.  The cursor is NOT an avoidance target: the
    // panel must stay easy to grab and use.
    struct Candidate { float x, y; Placement p; };
    const Candidate candidates[] =
    {
        { node.x - pw * 0.5f,     node.y - ph - gap, Placement::above  }, // ABOVE
        { node.x + gap,           node.y - ph - gap, Placement::above  }, // TOP-RIGHT
        { node.x - pw - gap,      node.y - ph - gap, Placement::above  }, // TOP-LEFT
        { node.x + gap,           node.y - ph * 0.5f, Placement::right }, // RIGHT
        { node.x - pw - gap,      node.y - ph * 0.5f, Placement::left  }, // LEFT
    };

    const float nodePad = 12.0f;
    const juce::Rectangle<float> nodeRect (node.x - nodePad, node.y - nodePad,
                                           nodePad * 2.0f, nodePad * 2.0f);
    const auto& m = owner_.getModel();
    const int selected = owner_.getSelectedBellIndex();

    float bestScore = std::numeric_limits<float>::max();
    juce::Rectangle<float> bestRect;
    Placement bestPlacement = Placement::above;
    bool found = false;

    for (int ci = 0; ci < (int) juce::numElementsInArray (candidates); ++ci)
    {
        const auto& c = candidates[ci];
        juce::Rectangle<float> r (c.x, c.y, pw, ph);
        r = r.constrainedWithin (juce::Rectangle<float> (margin, margin,
                                                         w - margin * 2.0f,
                                                         h - margin * 2.0f));
        if (r.getWidth() < pw * 0.6f || r.getHeight() < ph * 0.6f)
            continue; // too clamped to be useful

        float score = 0.0f;

        // Selected node: forbidden (huge penalty rejects overlapping spots).
        if (r.intersects (nodeRect))
            score += 1.0e6f;

        // Other Bell nodes.
        for (int i = 0; i < m.maxBells; ++i)
        {
            if (i == selected)
                continue;
            const auto np = owner_.getBellNodePosition (i);
            if (np.x > 0.0f && np.y > 0.0f)
            {
                const juce::Rectangle<float> nr (np.x - 10.0f, np.y - 10.0f, 20.0f, 20.0f);
                if (r.intersects (nr))
                    score += 4000.0f;
            }
        }
        // HPF / LPF handles.
        if (m.getHpfNorm && m.hpfNormToHz)
        {
            const float hp = m.getHpfNorm();
            if (hp > 0.0f)
            {
                const auto hpPos = owner_.getNominalCurvePointAtHz (m.hpfNormToHz (hp));
                const juce::Rectangle<float> hr (hpPos.x - 8.0f, hpPos.y - 8.0f, 16.0f, 16.0f);
                if (r.intersects (hr))
                    score += 3000.0f;
            }
        }
        if (m.getLpfNorm && m.lpfNormToHz)
        {
            const float lp = m.getLpfNorm();
            if (lp < 1.0f)
            {
                const auto lpPos = owner_.getNominalCurvePointAtHz (m.lpfNormToHz (lp));
                const juce::Rectangle<float> lr (lpPos.x - 8.0f, lpPos.y - 8.0f, 16.0f, 16.0f);
                if (r.intersects (lr))
                    score += 3000.0f;
            }
        }

        // Curve avoidance: a narrow peak or deep cut is content, not
        // wallpaper (mild — never allowed to outweigh usability).
        int curveHits = 0;
        for (const auto& v : owner_.getRenderedCurvePoints())
            if (r.contains (v))
                ++curveHits;
        score += (float) curveHits * 45.0f;

        // Mild distance preference + priority-order bias (ABOVE first).
        score += r.getCentre().getDistanceFrom (node) * 0.4f
               + (float) ci * 20.0f;

        if (score < bestScore)
        {
            bestScore = score;
            bestRect = r;
            bestPlacement = c.p;
            found = true;
        }
    }

    if (found && bestScore < 1.0e6f)
    {
        placement_ = bestPlacement;
        setBounds (juce::roundToInt (bestRect.getX()), juce::roundToInt (bestRect.getY()),
                   juce::roundToInt (bestRect.getWidth()), juce::roundToInt (bestRect.getHeight()));
        lastPlacementKey_ = { sel, mode_, nodeKey, sizeKey };
        return;
    }

    // Fallback: same restricted families in priority order, clamped — never
    // below the node.
    float x = node.x - pw * 0.5f;
    float y = node.y - ph - 14.0f;
    placement_ = Placement::above;
    if (y < margin)
    {
        // Above does not fit: try sides (still never below).
        if (node.x + pw + 14.0f <= w - margin)
        {
            x = node.x + 14.0f;
            placement_ = Placement::right;
        }
        else if (node.x - pw - 14.0f >= margin)
        {
            x = node.x - pw - 14.0f;
            placement_ = Placement::left;
        }
        else
        {
            x = juce::jlimit (margin, juce::jmax (margin, w - pw - margin),
                              node.x - pw * 0.5f);
            placement_ = Placement::clamped;
        }
    }
    x = juce::jlimit (margin, juce::jmax (margin, w - pw - margin), x);
    y = juce::jlimit (margin, juce::jmax (margin, h - ph - margin), y);
    setBounds (juce::roundToInt (x), juce::roundToInt (y),
               juce::roundToInt (pw), juce::roundToInt (ph));
    lastPlacementKey_ = { sel, mode_, nodeKey, sizeKey };
}

inline float BellControlPanel::getUnitValue (Field f) const
{
    const auto& model = owner_.getModel();
    const int idx = owner_.getSelectedBellIndex();
    if (idx < 0 || idx >= (int) model.maxBells)
        return 0.0f;
    const auto& bell = model.bells[idx];
    switch (f)
    {
    case Field::freq: return model.normToFreq (bell.getFreqNorm());
    case Field::gain: return model.gainNormToDb (bell.getGainNorm());
    case Field::q:    return model.qNormToQ (bell.getQNorm());
    }
    return 0.0f;
}

inline juce::String BellControlPanel::formatValue (Field f, float unitValue) const
{
    switch (f)
    {
    case Field::freq: return ApexInteractiveEQSurface::formatHz (unitValue);
    case Field::gain: return ApexInteractiveEQSurface::formatDb (unitValue);
    case Field::q:    return ApexInteractiveEQSurface::formatQ (unitValue);
    }
    return {};
}

inline float BellControlPanel::adjustFreqUnit (float currentHz, float dxPixels) const
{
    const auto& model = owner_.getModel();
    const auto plot = ApexEqPlotGeometry::plotBounds ((float) owner_.getWidth(),
                                                      (float) owner_.getHeight());
    const float plotW = juce::jmax (1.0f, plot.getWidth());
    const float norm = juce::jlimit (0.0f, 1.0f,
                                     model.freqToNorm (currentHz) + dxPixels / plotW);
    return model.normToFreq (norm);
}

inline float BellControlPanel::adjustVerticalUnit (Field f, float currentUnit, float dyPixels) const
{
    const auto& model = owner_.getModel();
    const auto plot = ApexEqPlotGeometry::plotBounds ((float) owner_.getWidth(),
                                                      (float) owner_.getHeight());
    const float plotH = juce::jmax (1.0f, plot.getHeight());
    const float dNorm = -(dyPixels / (plotH * 0.35f)) * 0.5f;
    if (f == Field::gain)
    {
        const float norm = juce::jlimit (0.0f, 1.0f, model.dbToGainNorm (currentUnit) + dNorm);
        return model.gainNormToDb (norm);
    }
    const float qNorm = juce::jlimit (0.0f, 1.0f, model.qToQNorm (currentUnit) + dNorm);
    return model.qNormToQ (qNorm);
}

inline void BellControlPanel::beginGesture (Field f)
{
    const auto& model = owner_.getModel();
    const int idx = owner_.getSelectedBellIndex();
    if (idx < 0 || idx >= (int) model.maxBells)
        return;
    const auto& bell = model.bells[idx];
    switch (f)
    {
    case Field::freq: bell.setFreqNorm (bell.getFreqNorm(), true, false); break;
    case Field::gain: bell.setGainNorm (bell.getGainNorm(), true, false); break;
    case Field::q:    bell.setQNorm (bell.getQNorm(), true, false); break;
    }
}

inline void BellControlPanel::setValue (Field f, float unitValue, bool endGesture)
{
    const auto& model = owner_.getModel();
    const int idx = owner_.getSelectedBellIndex();
    if (idx < 0 || idx >= (int) model.maxBells)
        return;
    const auto& bell = model.bells[idx];
    switch (f)
    {
    case Field::freq:
        bell.setFreqNorm (juce::jlimit (0.0f, 1.0f, model.freqToNorm (unitValue)),
                          false, endGesture);
        break;
    case Field::gain:
        bell.setGainNorm (juce::jlimit (0.0f, 1.0f, model.dbToGainNorm (unitValue)),
                          false, endGesture);
        break;
    case Field::q:
        bell.setQNorm (juce::jlimit (0.0f, 1.0f, model.qToQNorm (unitValue)),
                       false, endGesture);
        break;
    }
    owner_.notifyEdited();
}

inline void BellControlPanel::commitText (Field f, const juce::String& text)
{
    const auto& model = owner_.getModel();
    juce::String t = text.trim();
    float unit = t.getFloatValue();

    if (! t.containsAnyOf ("0123456789") || ! std::isfinite (unit))
    {
        updatePanel(); // revert the display, change nothing
        return;
    }

    switch (f)
    {
    case Field::freq:
        if (t.containsIgnoreCase ("khz"))
            unit *= 1000.0f;
        unit = juce::jlimit (model.minFreqHz, model.maxFreqHz, unit);
        break;
    case Field::gain:
        unit = juce::jlimit (-12.0f, 12.0f, unit);
        break;
    case Field::q:
        unit = juce::jlimit (model.qMin, model.qMax, unit);
        break;
    }

    setValue (f, unit, true); // clamps to the model range as well
}

inline void BellControlPanel::focusNextValue (Field f)
{
    // Continue editing directly in the next field. (grabKeyboardFocus alone
    // would report focusChangedDirectly, which must not auto-open an editor;
    // showEditor is the exact same path the user's Tab/Enter reaches.)
    switch (f)
    {
    case Field::freq: gain_.showEditor(); break;
    case Field::gain: q_.showEditor(); break;
    case Field::q:    freq_.showEditor(); break;
    }
}

inline bool BellControlPanel::findEditingField (Field& out) const
{
    if (freq_.isEditing()) { out = Field::freq; return true; }
    if (gain_.isEditing()) { out = Field::gain; return true; }
    if (q_.isEditing())    { out = Field::q; return true; }
    return false;
}

inline void BellControlPanel::commitEditingField (Field f)
{
    switch (f)
    {
    case Field::freq: freq_.commitEditor(); break;
    case Field::gain: gain_.commitEditor(); break;
    case Field::q:    q_.commitEditor(); break;
    }
}

} // namespace APEX
