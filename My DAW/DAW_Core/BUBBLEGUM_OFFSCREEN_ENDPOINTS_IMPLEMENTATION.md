# Bubblegum Offscreen Endpoint Multi-Target Picker Implementation

## Overview

This implementation adds a premium multi-target picker for Bubblegum offscreen endpoints, allowing users to select from multiple offscreen send targets instead of being forced to navigate to only the first one.

## Components Created

### 1. **BubblegumOffscreenDetectionCore.h**
**Location:** `Source\Bubblegum\BubblegumOffscreenDetectionCore.h`

**Purpose:** Core logic for detecting which Bubblegum send targets are offscreen (scrolled out of view) and grouping them by direction (left/right).

**Key Features:**
- Detects offscreen targets based on viewport bounds
- Groups targets by direction (left/right)
- Sorts targets by track number for logical ordering
- Generates preview text for hover tooltips
- Example: "3 sends →" or "← Bass Bus"

**API:**
```cpp
struct OffscreenTarget {
    TrackID trackId;
    juce::String trackName;
    int trackNumber;
    Direction direction;
    float sendLevel;
};

struct OffscreenGroup {
    Direction direction;
    std::vector<OffscreenTarget> targets;
};

std::vector<OffscreenGroup> detectOffscreenTargets(
    viewportBounds, targetPositions, targetNames, 
    targetNumbers, sendLevels);
```

### 2. **BubblegumOffscreenTargetPopup.h**
**Location:** `Source\UICore\BubblegumOffscreenTargetPopup.h`

**Purpose:** Premium Bubblegum-styled popup component that displays multiple offscreen targets in a selectable list.

**Visual Design:**
- Dark gradient background with pink Bubblegum accents
- Rounded corners (8px) with multi-layer pink glow borders
- Each row shows:
  - Track number (if available)
  - Bubblegum dot indicator (intensity varies with send level)
  - Track name
- Hover highlight on rows (pink tint + outline)
- Direction arrow indicator at top
- Drop shadow for depth
- Constrained size: 180-300px width, max 240px height

**Behavior:**
- Click any row to select that target
- Hover shows highlight
- Automatically closes after selection
- Closes on focus loss or clicking outside

### 3. **BubblegumOffscreenEndpointComponent.h**
**Location:** `Source\UICore\BubblegumOffscreenEndpointComponent.h`

**Purpose:** Renders endpoint indicators at the left/right edges of the viewport when targets are offscreen. Handles hover previews and click interactions.

**Visual Design:**
- Circular endpoint buttons (28px diameter) at left/right edges
- Pink gradient fill with multi-layer glow rings
- Direction arrow (◄ or ►) in white
- Count badge (red circle) showing number of targets if > 1
- Hover state: brighter glow, tooltip with preview text
- Edge margins: 12px from viewport edge

**Behavior:**
- **Single offscreen target:** Click directly scrolls to that track
- **Multiple offscreen targets:** Click opens the target picker popup
- **Hover:** Shows preview tooltip
  - Single target: "← Bass Bus" or "FX Verb →"
  - Multiple: "← 3 sends" or "4 sends →"
- Popup anchored next to the endpoint (left-side endpoints open to right, vice versa)
- Selection triggers smooth auto-scroll to chosen track

### 4. **MixerPanel Updates**

**Added Methods:**
```cpp
void setViewport(juce::Viewport* viewport);  // Set parent viewport reference
void scrollToTrack(const TrackID& trackId);  // Smooth auto-scroll to track
```

**scrollToTrack Implementation:**
- Centers the target strip in the viewport
- Smooth animated scroll (300ms, eased)
- Clamps to valid scroll range
- Debug logging for tracking behavior

**Added Member:**
```cpp
juce::Viewport* parentViewport_ = nullptr;
```

### 5. **BubblegumV2System Updates**

**Added Core:**
```cpp
BubblegumOffscreenDetectionCore offscreenDetector;
```

## Integration Points

### MainComponent Integration (Required)

1. **Set viewport reference:**
```cpp
mixerPanel_->setViewport(&mixerViewport_);
```

2. **Create and add endpoint component:**
```cpp
// In MainComponent private members:
std::unique_ptr<DAW::BubblegumOffscreenEndpointComponent> offscreenEndpoints_;

// In constructor:
offscreenEndpoints_ = std::make_unique<DAW::BubblegumOffscreenEndpointComponent>();
offscreenEndpoints_->bind(&appCore_.getBubblegumV2System(), mixerPanel_.get());
offscreenEndpoints_->onScrollToTrack = [this](const TrackID& trackId) {
    mixerPanel_->scrollToTrack(trackId);
};
addAndMakeVisible(offscreenEndpoints_.get());
```

3. **Position endpoint component:**
```cpp
// In resized():
// Position over the mixer viewport area
offscreenEndpoints_->setBounds(mixerViewport_.getBounds());
offscreenEndpoints_->setAlwaysOnTop(true);
```

4. **Update offscreen detection (30Hz timer or when cables repaint):**
```cpp
if (bgV2->isActive()) {
    // Collect target positions, names, numbers, send levels
    std::map<TrackID, float> targetPositions;
    std::map<TrackID, juce::String> targetNames;
    std::map<TrackID, int> targetNumbers;
    std::map<TrackID, float> sendLevels;
    
    auto viewportBounds = mixerViewport_.getViewArea().toFloat();
    auto& strips = mixerPanel_->getStrips();
    
    for (auto* strip : strips) {
        if (strip->getBubblegumRole() == MixerStrip::BgRole::Target) {
            auto trackId = strip->getTrack().getID();
            auto globalPos = strip->localPointToGlobal(
                juce::Point<int>(strip->getWidth() / 2, 0));
            auto localPos = mixerPanel_->getLocalPoint(nullptr, globalPos);
            
            targetPositions[trackId] = (float)localPos.x;
            targetNames[trackId] = strip->getTrack().getName();
            targetNumbers[trackId] = /* track number from track manager */;
            sendLevels[trackId] = bgV2->getSendLevelTo(trackId);
        }
    }
    
    offscreenEndpoints_->updateOffscreenTargets(
        viewportBounds, targetPositions, targetNames, 
        targetNumbers, sendLevels);
}
```

## User Experience Flow

### Single Offscreen Target
1. User has Bubblegum active with one send to an offscreen track
2. Endpoint appears at edge (left or right) with arrow
3. Hover shows tooltip: "← Reverb Bus"
4. Click endpoint
5. Mixer smoothly scrolls to center that track (300ms animation)

### Multiple Offscreen Targets
1. User has Bubblegum active with 3 sends to offscreen tracks (all on right)
2. Right endpoint appears with arrow + red badge showing "3"
3. Hover shows tooltip: "3 sends →"
4. Click endpoint
5. Premium pink-accented popup opens next to endpoint
6. Popup shows:
   ```
   Track 5 — Bass Bus
   Track 7 — Reverb
   Track 12 — Delay
   ```
7. User hovers over "Reverb" → row highlights in pink
8. User clicks "Reverb"
9. Popup closes
10. Mixer smoothly scrolls to center Track 7

## Visual Design Principles

All components follow Bubblegum design language:
- **Color:** Pink (#FF7DB8) as primary accent
- **Style:** Premium, glossy, modern
- **Borders:** Multi-layer glows, soft rounded corners
- **Typography:** Bold fonts, clear hierarchy
- **Shadows:** Subtle drop shadows for depth
- **Animation:** Smooth, professional (300ms standard)
- **Feedback:** Clear hover states, visual confirmation

## Performance Considerations

- Offscreen detection runs at 30Hz (same as Bubblegum animation timer)
- Only active when Bubblegum mode is on
- Popup uses immediate-mode rendering (no complex state)
- Scroll animation handled by JUCE's animator (hardware accelerated)
- Minimal allocations (vectors reused, maps passed by reference)

## Future Enhancements

Possible improvements:
1. **Grouped by distance:** Sort targets by proximity to viewport
2. **Send level visualization:** Show send level bars in popup rows
3. **Keyboard navigation:** Arrow keys to navigate popup, Enter to select
4. **Preview cables:** Highlight cable when hovering popup row
5. **Batch navigation:** "Next/Previous offscreen target" shortcuts
6. **Smart positioning:** Avoid popup going offscreen
7. **Transition effects:** Fade in/out animations for popup

## Testing Checklist

- [ ] Single offscreen target (left) → click → auto-scroll
- [ ] Single offscreen target (right) → click → auto-scroll
- [ ] Multiple targets (left) → click → popup → select → auto-scroll
- [ ] Multiple targets (right) → click → popup → select → auto-scroll
- [ ] Hover shows correct preview text
- [ ] Count badge displays correct number
- [ ] Popup closes on selection
- [ ] Popup closes on clicking outside
- [ ] Popup closes when Bubblegum deactivated
- [ ] Smooth scroll animation (no jumps)
- [ ] Endpoints disappear when targets come onscreen
- [ ] Track numbers display correctly in popup
- [ ] Send level affects dot brightness
- [ ] Visual polish (gradients, glows, shadows) looks premium

## File Summary

**New Files:**
1. `Source\Bubblegum\BubblegumOffscreenDetectionCore.h` (core logic)
2. `Source\UICore\BubblegumOffscreenTargetPopup.h` (popup component)
3. `Source\UICore\BubblegumOffscreenEndpointComponent.h` (endpoint overlay)

**Modified Files:**
1. `Source\Bubblegum\BubblegumV2System.h` (added offscreenDetector core + include)
2. `Source\UICore\MixerPanel.h` (added setViewport, scrollToTrack, parentViewport_)

**Integration Required:**
- `Source\MainComponent.h` / `.cpp` (wire up endpoint component, viewport, timer updates)

---

**Status:** Core implementation complete. Ready for MainComponent integration and testing.
