# Slime Side Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the detached rectangular FloatingPanelChrome FX Chain window with an organic slime side panel visually attached to the mixer edge — closed = small hanging drip, open = large irregular puddle containing the existing FX Chain UI.

**Architecture:** A new `SlimeSidePanel` component (child of MainComponent, like FloatingPanelChrome was) uses `juce::Path` with interpolated control points to draw organic slime shapes. It contains the existing `MixerPluginSidePanel` via raw pointer (unchanged — 1243 lines preserved). Animation via `juce::Timer` + normalized 0→1 interpolation. Toggle routing: MixerWindow toolbar → MixerPanel → MainComponent → SlimeSidePanel.

**Tech Stack:** JUCE 7+, C++17, juce::Path (cubicTo/quadraticTo), Timer-driven animation, hitTest()

## Global Constraints

- Do NOT modify audio processing, DSP, plugin hosting, routing, PDC, track model, mixer audio logic, recording, or monitoring
- Do NOT modify `MixerPluginSidePanel.h` (preserve existing 1243 lines of working FX chain code)
- Do NOT introduce heap allocations inside paint() or audio callbacks
- All paths must be deterministic — no random contour changes per repaint
- Cache/reuse paths when dimensions unchanged
- hitTest() returns true only within the organic shape (transparent areas pass clicks through)
- Maintain existing MixerWindow toolbar button toggling

---

### File Structure

**Create:**
- `Source/UICore/SlimeSidePanel.h` — New SlimeSidePanel component (organic path rendering, animation, open/closed state, contains MixerPluginSidePanel)

**Modify:**
- `Source/MainComponent.h` — Replace `sidePanelChrome_` (FloatingPanelChrome) with `slimeSidePanel_` (SlimeSidePanel)
- `Source/MainComponent.cpp` — Wire SlimeSidePanel creation, positioning, toggle routing; remove FloatingPanelChrome FX chain logic
- `Source/UICore/MixerPanel.h` — Wire `onToggleSidePanel` through MainComponent to SlimeSidePanel; add `getMasterStripCenterY()` and `getMasterStripRightEdge()` helpers for attachment point calc; add `slimeSidePanel_` raw pointer setter

---

### Task 1: Create SlimeSidePanel.h

**Files:**
- Create: `Source/UICore/SlimeSidePanel.h`

**Interfaces:**
- Consumes: `MixerPanel*` (for attachment point coordinates), `juce::Component*` (the existing MixerPluginSidePanel as content)
- Produces: `SlimeSidePanel` class with `toggle()`, `isOpen()`, `getState()`, `getContentArea()`

- [ ] **Step 1: Write the full SlimeSidePanel class**

The class uses `juce::Path` with 20 interpolated control points defining the organic slime boundary. Two static point sets define closed (small drip) and open (large puddle) shapes. A timer interpolates between them at 60fps.

```cpp
#pragma once
#include <JuceHeader.h>
#include "../ThemeCore/Theme.h"

namespace DAW {

class MixerPanel;

class SlimeSidePanel : public juce::Component, private juce::Timer
{
public:
    enum class VisualState { ClosedDrip, Opening, OpenPuddle, Closing };

    explicit SlimeSidePanel(juce::Component* fxContent);
    ~SlimeSidePanel() override = default;

    void toggle();
    bool isOpen() const noexcept { return state_ == VisualState::OpenPuddle; }
    VisualState getState() const noexcept { return state_; }
    void setMixerPanel(MixerPanel* mp) { mixerPanel_ = mp; }
    void setAttachmentPoint(juce::Point<int> pt) { attachPoint_ = pt; }

    /** Get the safe content rectangle for internal FX panel layout. */
    juce::Rectangle<int> getContentArea() const noexcept { return contentArea_; }

    /** Immediately snap to a given animation state (0=closed, 1=open). */
    void setAnimationState(float t);

    std::function<void(bool /*open*/)> onStateChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool hitTest(int x, int y) override;

private:
    void timerCallback() override;

    // Path control points — 20 points define the organic boundary clockwise.
    // Point 0 and the mid-point define the attachment zone at the mixer edge.
    static constexpr int kNumPts = 20;
    static constexpr int kAttachPtIndex = 0;  // point 0 is the attachment point

    struct PtArray { juce::Point<float> pts[kNumPts]; };

    PtArray closedPts_;   // closed drip point positions
    PtArray openPts_;     // open puddle point positions
    PtArray currentPts_;  // interpolated result
    juce::Path currentPath_;

    void defineClosedPoints(PtArray& p);
    void defineOpenPoints(PtArray& p);
    void interpolatePoints(float t);
    void rebuildPath();
    void syncBoundsAndContent();

    VisualState state_ = VisualState::ClosedDrip;
    float animationT_ = 0.0f;  // 0=closed, 1=open

    juce::Rectangle<int> contentArea_;
    juce::Component* fxContent_ = nullptr;
    MixerPanel* mixerPanel_ = nullptr;
    juce::Point<int> attachPoint_;

    static constexpr int kClosedW = 34;
    static constexpr int kClosedH = 60;
    static constexpr int kOpenW   = 280;
    static constexpr int kOpenH   = 510;
    static constexpr float kAnimDurationSec = 0.3f;
    static constexpr int kTimerIntervalMs = 16; // ~60fps

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlimeSidePanel)
};

} // namespace DAW
```

- [ ] **Step 2: Write constructor and core API**

```cpp
inline SlimeSidePanel::SlimeSidePanel(juce::Component* fxContent)
    : fxContent_(fxContent)
{
    jassert(fxContent_ != nullptr);
    setOpaque(false);
    addAndMakeVisible(fxContent_);
    defineClosedPoints(closedPts_);
    defineOpenPoints(openPts_);
    interpolatePoints(0.0f);
    rebuildPath();
    setSize(kClosedW, kClosedH);
}

inline void SlimeSidePanel::toggle()
{
    if (state_ == VisualState::OpenPuddle || state_ == VisualState::Opening)
    {
        state_ = VisualState::Closing;
        animationT_ = 1.0f;
    }
    else
    {
        state_ = VisualState::Opening;
        animationT_ = 0.0f;
    }
    startTimer(kTimerIntervalMs);
}

inline void SlimeSidePanel::setAnimationState(float t)
{
    animationT_ = juce::jlimit(0.0f, 1.0f, t);
    state_ = (t >= 1.0f) ? VisualState::OpenPuddle : VisualState::ClosedDrip;
    interpolatePoints(animationT_);
    rebuildPath();
    syncBoundsAndContent();
    repaint();
}
```

- [ ] **Step 3: Write point definitions (closed drip and open puddle)**

Attachment point is at index 0 in both sets, at the same local coordinate (0, 24) so interpolation doesn't drift. All y values are >= 0.

```cpp
inline void SlimeSidePanel::defineClosedPoints(PtArray& p)
{
    // Small hanging drip: ~34px wide, ~60px tall
    // Attachment (0, 24) is at the mixer's right edge center
    const float pts[kNumPts][2] = {
        {0, 24}, {4, 18}, {12, 20}, {22, 26},
        {30, 32}, {32, 42}, {28, 52}, {22, 58},
        {16, 62}, {10, 60}, {6, 56}, {2, 50},
        {0, 44}, {0, 38}, {0, 30}, {0, 24},
        {0, 0}, {0, 0}, {0, 0}, {0, 0}
    };
    for (int i = 0; i < kNumPts; ++i)
        p.pts[i] = { pts[i][0], pts[i][1] };
}

inline void SlimeSidePanel::defineOpenPoints(PtArray& p)
{
    // Large organic puddle: ~280px wide, ~510px tall
    // Attachment (0, 24) matches closed attachment y
    const float pts[kNumPts][2] = {
        {0, 24}, {14, 16}, {42, 20}, {76, 28},
        {92, 48}, {96, 72}, {90, 96}, {98, 118},
        {92, 144}, {84, 168}, {68, 186}, {52, 200},
        {38, 208}, {22, 196}, {12, 172}, {6, 144},
        {2, 108}, {0, 76}, {0, 48}, {0, 24}
    };
    for (int i = 0; i < kNumPts; ++i)
        p.pts[i] = { pts[i][0], pts[i][1] };
}
```

- [ ] **Step 4: Write interpolation, path building, and bounds sync**

```cpp
inline void SlimeSidePanel::interpolatePoints(float t)
{
    const float tt = juce::jlimit(0.0f, 1.0f, t);
    for (int i = 0; i < kNumPts; ++i)
    {
        currentPts_.pts[i].x = closedPts_.pts[i].x + (openPts_.pts[i].x - closedPts_.pts[i].x) * tt;
        currentPts_.pts[i].y = closedPts_.pts[i].y + (openPts_.pts[i].y - closedPts_.pts[i].y) * tt;
    }
}

inline void SlimeSidePanel::rebuildPath()
{
    currentPath_.clear();
    auto& p = currentPath_;
    p.startNewSubPath(currentPts_.pts[0]);

    // Catmull-Rom style cubic bezier: each segment uses adjacent points for smooth tangent
    const float tension = 0.28f;
    for (int i = 0; i < kNumPts; ++i)
    {
        const auto& curr = currentPts_.pts[i];
        const auto& next = currentPts_.pts[(i + 1) % kNumPts];
        const auto& prev = currentPts_.pts[(i - 1 + kNumPts) % kNumPts];
        const auto& afterNext = currentPts_.pts[(i + 2) % kNumPts];

        const float cpx1 = curr.x + (next.x - prev.x) * tension;
        const float cpy1 = curr.y + (next.y - prev.y) * tension;
        const float cpx2 = next.x - (afterNext.x - curr.x) * tension;
        const float cpy2 = next.y - (afterNext.y - curr.y) * tension;

        p.cubicTo(cpx1, cpy1, cpx2, cpy2, next.x, next.y);
    }
    p.closeSubPath();
}

inline void SlimeSidePanel::syncBoundsAndContent()
{
    auto bounds = currentPath_.getBounds();
    const int w = juce::jmax(32, (int)std::ceil(bounds.getWidth()));
    const int h = juce::jmax(40, (int)std::ceil(bounds.getHeight()));
    setSize(w, h);

    // Position fx content inside the safe region
    if (fxContent_)
    {
        const int inset = 14;
        const int contentW = w - inset * 2;
        const int contentH = h - inset * 2 - 10;
        contentArea_ = { inset, inset + 4, contentW, contentH };
        fxContent_->setBounds(contentArea_);
        fxContent_->setVisible(state_ == VisualState::OpenPuddle || state_ == VisualState::Opening);
    }
}
```

- [ ] **Step 5: Write paint(), resized(), hitTest(), timerCallback()**

```cpp
inline void SlimeSidePanel::paint(juce::Graphics& g)
{
    auto& t = Theme::getInstance();

    // Background body — deep green-black with gradient
    {
        juce::ColourGradient bodyGrad(
            juce::Colour(0xFF0D2A20).brighter(0.06f), 0, 0,
            juce::Colour(0xFF030A08), 0, (float)getHeight(),
            false);
        bodyGrad.addColour(0.25f, juce::Colour(0xFF0A1A14));
        bodyGrad.addColour(0.65f, juce::Colour(0xFF06120E));
        g.setGradientFill(bodyGrad);
        g.fillPath(currentPath_);
    }

    // Outer glow stroke — neon mint
    {
        g.setColour(juce::Colour(0xFF40E0A0).withAlpha(0.28f));
        g.strokePath(currentPath_, juce::PathStrokeType(1.5f));
    }

    // Top highlight lobe — subtle light on upper area
    {
        auto b = currentPath_.getBounds();
        juce::Path hl;
        hl.addEllipse(b.getX() + b.getWidth() * 0.04f,
                      b.getY() + b.getHeight() * 0.01f,
                      b.getWidth() * 0.30f,
                      b.getHeight() * 0.08f);
        g.setColour(juce::Colour(0xFF40E0A0).withAlpha(0.05f));
        g.fillPath(hl);
    }

    // Small decorative droplets near bottom edge (visible when mostly open)
    if (animationT_ > 0.6f)
    {
        auto b = currentPath_.getBounds();
        g.setColour(juce::Colour(0xFF40E0A0).withAlpha(0.10f));
        g.fillEllipse(b.getX() + b.getWidth() * 0.80f, b.getY() + b.getHeight() * 0.86f, 7, 7);
        g.setColour(juce::Colour(0xFF40E0A0).withAlpha(0.07f));
        g.fillEllipse(b.getX() + b.getWidth() * 0.22f, b.getY() + b.getHeight() * 0.91f, 4, 4);
    }
}

inline void SlimeSidePanel::resized()
{
    if (fxContent_)
        fxContent_->setBounds(getContentArea());
}

inline bool SlimeSidePanel::hitTest(int x, int y)
{
    return currentPath_.contains((float)x, (float)y);
}

inline void SlimeSidePanel::timerCallback()
{
    const float step = (float)kTimerIntervalMs / 1000.0f / kAnimDurationSec;

    if (state_ == VisualState::Opening)
    {
        animationT_ = juce::jmin(1.0f, animationT_ + step);
        if (animationT_ >= 1.0f)
        {
            animationT_ = 1.0f;
            state_ = VisualState::OpenPuddle;
            stopTimer();
            fxContent_->setVisible(true);
            if (onStateChanged) onStateChanged(true);
        }
    }
    else if (state_ == VisualState::Closing)
    {
        animationT_ = juce::jmax(0.0f, animationT_ - step);
        if (animationT_ <= 0.0f)
        {
            animationT_ = 0.0f;
            state_ = VisualState::ClosedDrip;
            stopTimer();
            if (onStateChanged) onStateChanged(false);
        }
    }
    else
    {
        stopTimer();
        return;
    }

    interpolatePoints(animationT_);
    rebuildPath();
    syncBoundsAndContent();
    repaint();
}
```

---

### Task 2: Wire into MainComponent

**Files:**
- Modify: `Source/MainComponent.h`
- Modify: `Source/MainComponent.cpp`

- [ ] **Step 1: Replace `sidePanelChrome_` declaration with `slimeSidePanel_`**

In `MainComponent.h` line 152, replace:
```cpp
std::unique_ptr<DAW::FloatingPanelChrome>  sidePanelChrome_;
```
with:
```cpp
std::unique_ptr<DAW::SlimeSidePanel>  slimeSidePanel_;
```

- [ ] **Step 2: Update the SlimeSidePanel creation block**

In `MainComponent.cpp`, locate the block around line 3014-3064 where sidePanelChrome_ is created and wired. Replace with:

```cpp
// ── Slime Side Panel ─────────────────────────────────────────────────
// pluginSidePanel_ (MixerPluginSidePanel) is re-parented into SlimeSidePanel
slimeSidePanel_ = std::make_unique<DAW::SlimeSidePanel>(pluginSidePanel_.get());
slimeSidePanel_->setMixerPanel(mixerPanel_.get());
slimeSidePanel_->onStateChanged = [this](bool open)
{
    if (mixerWindow_)
        mixerWindow_->sidePanelOpen = open;
    if (mixerPanel_)
    {
        if (open)
            mixerPanel_->showSidePanel();
        else
            mixerPanel_->hideSidePanel();
    }
};
addChildComponent(slimeSidePanel_.get());
```

- [ ] **Step 3: Replace all `sidePanelChrome_` references in positioning code**

In `MainComponent.cpp`, replace the old positioning logic (~line 6222):
```cpp
if (sidePanelChrome_ && sidePanelChrome_->isVisible() && !sidePanelChrome_->isMaximized())
    sidePanelChrome_->setBounds(getWidth() - 360, kMenuH + kTransportH + 18, 340, juce::jmin(560, getHeight() - 150));
```
with:
```cpp
if (slimeSidePanel_ && mixerPanel_)
{
    const int rightEdge = mixerPanel_->getMasterStripRightEdge();
    const int centerY = mixerPanel_->getMasterStripCenterY();
    const int attachX = ... // convert to MainComponent coordinates

    slimeSidePanel_->setAttachmentPoint({ attachX, centerY });
    slimeSidePanel_->setTopLeftPosition(attachX, centerY - 24); // 24 = attachment local y
}
```

- [ ] **Step 4: Replace toggle callbacks**

Replace all `sidePanelChrome_->restorePanel()`, `sidePanelChrome_->closePanel()`, `sidePanelChrome_->minimizePanel()` calls with `slimeSidePanel_->toggle()`, `slimeSidePanel_->setAnimationState(0|1)` as appropriate.

Key replacements:
- In `MainComponent` resized/refreshLayout: replace with `slimeSidePanel_` positioning logic
- In sidebar toggle handler (~line 2522-2530): call `slimeSidePanel_->toggle()`
- In bubble taskbar quick access (~line 3282-3296): call `slimeSidePanel_->toggle()`

---

### Task 3: Add helper methods to MixerPanel

**Files:**
- Modify: `Source/UICore/MixerPanel.h`

- [ ] **Step 1: Add `getMasterStripRightEdge()` and `getMasterStripCenterY()`**

```cpp
int getMasterStripRightEdge() const
{
    for (auto* strip : stripPtrs_)
        if (strip && strip->getTrack().isMaster())
            return strip->getBounds().getRight();
    return getWidth();
}

int getMasterStripCenterY() const
{
    for (auto* strip : stripPtrs_)
        if (strip && strip->getTrack().isMaster())
            return strip->getBounds().getCentreY();
    return getHeight() / 2;
}
```

- [ ] **Step 2: Update `toggleSidePanel()` to notify SlimeSidePanel**

Modify the existing `toggleSidePanel()` method so its callback routes through to SlimeSidePanel. The actual toggling should be done by SlimeSidePanel itself — MainComponent bridges the call.

No change to MixerPanel's toggleSidePanel signature — the wiring in MainComponent translates the callback.

---

### Task 4: Remove FloatingPanelChrome references for FX chain

**Files:**
- Verify: All references to `sidePanelChrome_` in MainComponent.cpp are replaced

- [ ] **Step 1: Remove old `sidePanelChrome_.reset()` call**

In `MainComponent.cpp` line 5645, replace `sidePanelChrome_.reset();` with `slimeSidePanel_.reset();`

- [ ] **Step 2: Verify all references**

Search for remaining `sidePanelChrome_` references in MainComponent.cpp. Each must be handled:
- All `sidePanelChrome_->isVisible()` checks → `slimeSidePanel_->isOpen()`
- All `sidePanelChrome_->isMaximized()` checks → `false` (SlimeSidePanel doesn't maximize)
- All `sidePanelChrome_->closePanel()` → `slimeSidePanel_->setAnimationState(0.0f)`
- All `sidePanelChrome_->restorePanel()` → `slimeSidePanel_->setAnimationState(1.0f)`
- All `sidePanelChrome_->getBounds()` → `slimeSidePanel_->getBounds()`

- [ ] **Step 3: Remove unused FloatingPanelChrome include**

In `MainComponent.h` or `MainComponent.cpp`, remove `#include "../FloatingWindowCore/FloatingPanelChrome.h"` if it's no longer used (check if FloatingPanelChrome is still used for other panels like browser).

---

### Task 5: Update MixerPanel side panel state wiring

**Files:**
- Modify: `Source/UICore/MixerPanel.h`

- [ ] **Step 1: Update `toggleSidePanel()` callback chain**

The existing `onToggleSidePanel` callback already routes to MainComponent. MainComponent now routes to SlimeSidePanel. Ensure the callback signature matches:

```cpp
void toggleSidePanel()
{
    sidePanelOpen_ = !sidePanelOpen_;
    if (onToggleSidePanel) onToggleSidePanel(sidePanelOpen_);
}
```

This stays the same. MainComponent's handler changes to drive SlimeSidePanel.

---

### Task 6: Build Verification

- [ ] **Step 1: Build the project**

Run the build command for the project to verify compilation succeeds with zero errors.

- [ ] **Step 2: Verify no FloatingPanelChrome references remain for FX chain**

Search for `sidePanelChrome_` in `MainComponent.cpp` — there should be zero remaining references.

- [ ] **Step 3: Clean up any unused `FloatingPanelChrome.h` includes**

If FloatingPanelChrome is no longer used at all, remove its include. (It may still be used by other panels though — check first.)
