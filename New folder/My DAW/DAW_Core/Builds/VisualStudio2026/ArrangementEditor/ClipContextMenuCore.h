// ===========================================================================
// ClipContextMenuCore.h
// Context menu completo para clips de arrangement.
//
// ITEMS:
//   Pitch / Time  → submenú con modo + knobs (abre PropertiesWindow)
//   Presets       → submenú con 7 presets + 5 safe defaults por tipo
//   Bounce        → render a nuevo buffer (baked audio)
//   Freeze        → freeze temporal para ahorrar CPU
//   Unfreeze      → restaurar procesamiento en vivo
//   ─────────────
//   (estado actual mostrado en items disabled)
//
// FLUJO:
//   1. User right-clicks clip en arrangement.
//   2. ArrangementViewCore llama ClipContextMenuCore::show(clip, pos, callbacks).
//   3. Callbacks hacen el trabajo real (undo, DSP, UI update).
//
// THREAD SAFETY:
//   Toda la lógica en message thread. Callbacks deben ser message-thread safe.
// ===========================================================================
#pragma once
#include "ArrangementClipModel.h"
#include "TimePitchPresetsCore.h"
#include "TimePitchUICore.h"
#include "TimePitchUndoActions.h"
#include "ClipFreezeState.h"
#include "../../../Source/AutomationCore/AutomationLaneCore.h"
#include "../../../Source/AutomationCore/AutomationQuickCreateCore.h"
#include "ClipAutomationPanel.h"
#include <JuceHeader.h>
#include <functional>

namespace ArrangementEditor
{

// ---------------------------------------------------------------------------
// Callbacks que ArrangementViewCore debe proveer
// ---------------------------------------------------------------------------
struct ClipContextCallbacks
{
    // Standard DAW clip operations
    std::function<void(ArrangementClipModel*)>    cutClip;
    std::function<void(ArrangementClipModel*)>    copyClip;
    std::function<void(ArrangementClipModel*)>    duplicateClip;
    std::function<void(ArrangementClipModel*)>    deleteClip;
    std::function<void(ArrangementClipModel*)>    renameClip;
    std::function<void(ArrangementClipModel*)>    muteClip;
    std::function<void(ArrangementClipModel*)>    loopClip;
    std::function<bool()>                         canPaste;
    std::function<void(double /*time*/, int /*track*/)> pasteClip;

    // Pitch/Time: open the properties panel for the clip
    std::function<void(ArrangementClipModel*)>    openTimePitchPanel;

    // Apply preset: (clip, presetName) — caller creates undo action
    std::function<void(ArrangementClipModel*, const std::string&)> applyPreset;

    // Bounce: render clip to baked audio (async)
    std::function<void(ArrangementClipModel*)>    bounceClip;

    // Freeze / Unfreeze
    std::function<void(ArrangementClipModel*)>    freezeClip;
    std::function<void(ArrangementClipModel*)>    unfreezeClip;

    // Reset to identity (no pitch/stretch)
    std::function<void(ArrangementClipModel*)>    resetTimePitch;

    // Automation quick-create for the clicked clip/track.
    std::function<void(ArrangementClipModel*, const juce::String&)> createAutomationForClip;

    // Open the full "Automate This Clip" floating panel.
    std::function<void(ArrangementClipModel*)> openAutomateClipPanel;

    // Open Vocal Tune for the clicked clip.
    std::function<void(ArrangementClipModel*)> openVocalTune;

    // Toggle bypass for Vocal Tune on the clicked clip.
    std::function<void(ArrangementClipModel*)> toggleVocalTuneBypass;

    // Returns true if Vocal Tune is currently bypassed for the clip.
    std::function<bool(ArrangementClipModel*)> isVocalTuneBypassed;

    // Toggle normalize on a clip (calls normalizePeak / revert).
    std::function<void(ArrangementClipModel*)> normalizeClip;

    // Returns true if clip is currently normalized.
    std::function<bool(ArrangementClipModel*)> isClipNormalized;

    // Context: where the right-click happened (for paste)
    double clickTime  = 0.0;
    int    clickTrack = 0;
};

// ---------------------------------------------------------------------------
// ClipContextMenuCore
// ---------------------------------------------------------------------------
class ClipContextMenuCore
{
public:
    // -----------------------------------------------------------------------
    // show — builds and shows the PopupMenu for the given clip.
    // pos: screen position for the menu.
    // freezeState: nullptr if clip has no freeze state yet.
    // -----------------------------------------------------------------------
    // showOnClip — right-click on an existing clip
    static void show(ArrangementClipModel*       clip,
                     juce::Point<int>             screenPos,
                     const ClipContextCallbacks&  cb,
                     const ClipFreezeState*       freezeState = nullptr)
    {
        if (!clip) return;

        juce::PopupMenu menu;

        // ── Standard clip edit operations ─────────────────────────────────
        menu.addItem(kIdCut,       "Cut",       cb.cutClip       != nullptr);
        menu.addItem(kIdCopy,      "Copy",      cb.copyClip      != nullptr);
        menu.addItem(kIdDuplicate, "Duplicate", cb.duplicateClip != nullptr);
        menu.addSeparator();
        menu.addItem(kIdPaste,     "Paste",
                     cb.pasteClip != nullptr && cb.canPaste && cb.canPaste());
        menu.addSeparator();
        menu.addItem(kIdMute,  clip->muted ? "Unmute" : "Mute", cb.muteClip != nullptr);
        menu.addItem(kIdRename, juce::String(juce::CharPointer_UTF8("Rename\xe2\x80\xa6")), cb.renameClip != nullptr);
        menu.addSeparator();
        menu.addItem(kIdDelete, "Delete", cb.deleteClip != nullptr);
        menu.addSeparator();

        // ── Pitch / Time ──────────────────────────────────────────────────
        menu.addItem(kIdPitchTimePanel, juce::String(juce::CharPointer_UTF8("Pitch / Time\xe2\x80\xa6")),
                     cb.openTimePitchPanel != nullptr);

        // ── Presets submenu ────────────────────────────────────────────────
        menu.addSubMenu("Presets", buildPresetsMenu());
        menu.addSubMenu("Safe Defaults", buildSafeDefaultsMenu());
        menu.addItem(kIdReset, "Reset Pitch / Time", cb.resetTimePitch != nullptr);
        menu.addSeparator();

        // ── Bounce / Freeze ────────────────────────────────────────────────
        const bool isFrozen = freezeState && freezeState->frozen;
        menu.addItem(kIdBounce,   "Bounce",   cb.bounceClip   != nullptr && !isFrozen);
        menu.addItem(kIdFreeze,   "Freeze",   cb.freezeClip   != nullptr && !isFrozen);
        menu.addItem(kIdUnfreeze, "Unfreeze", cb.unfreezeClip != nullptr && isFrozen);
        {
            const bool normalized = cb.isClipNormalized ? cb.isClipNormalized(clip) : false;
            juce::PopupMenu::Item normItem;
            normItem.itemID    = kIdNormalize;
            normItem.text      = normalized ? "Revert Normalize" : "Normalize";
            normItem.isEnabled = cb.normalizeClip != nullptr;
            normItem.isTicked  = normalized;
            menu.addItem(normItem);
        }
        menu.addSeparator();

        // ── Automation ────────────────────────────────────────────────────
        menu.addItem(kIdAutomateThisClip,
                     juce::String(juce::CharPointer_UTF8("\xf0\x9f\x8e\x9b  Automate This Clip\xe2\x80\xa6")),
                     cb.openAutomateClipPanel != nullptr);
        menu.addItem(kIdVocalTune,
                     "Vocal Tune",
                     cb.openVocalTune != nullptr);
        {
            const bool bypassed = cb.isVocalTuneBypassed ? cb.isVocalTuneBypassed(clip) : false;
            juce::PopupMenu::Item bypassItem;
            bypassItem.itemID    = kIdVocalTuneBypass;
            bypassItem.text      = "Bypass Vocal Tune";
            bypassItem.isEnabled = cb.toggleVocalTuneBypass != nullptr;
            bypassItem.isTicked  = bypassed;
            menu.addItem(bypassItem);
        }

        // ── Show ──────────────────────────────────────────
        menu.showMenuAsync(
            juce::PopupMenu::Options()
                .withTargetScreenArea(juce::Rectangle<int>(screenPos.x,
                                                           screenPos.y, 1, 1)),
            [clip, cb](int result)
            {
                handleResult(result, clip, cb);
            });
    }

    // showOnEmptyTrack — right-click on empty arrangement space (no clip)
    static void showOnEmptyTrack(juce::Point<int>            screenPos,
                                 const ClipContextCallbacks& cb)
    {
        juce::PopupMenu menu;
        const bool hasPaste = cb.pasteClip != nullptr && cb.canPaste && cb.canPaste();
        menu.addItem(kIdPaste, "Paste", hasPaste);

        menu.showMenuAsync(
            juce::PopupMenu::Options()
                .withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
            [cb](int result)
            {
                if (result == kIdPaste && cb.pasteClip)
                    cb.pasteClip(cb.clickTime, cb.clickTrack);
            });
    }

private:
    // Menu item IDs
    enum IDs : int
    {
        kIdCut              = 1,
        kIdCopy             = 2,
        kIdDuplicate        = 3,
        kIdPaste            = 4,
        kIdDelete           = 5,
        kIdMute             = 6,
        kIdRename           = 7,
        kIdPitchTimePanel   = 10,
        kIdBounce           = 11,
        kIdFreeze           = 12,
        kIdUnfreeze         = 13,
        kIdReset            = 14,
        kIdAutomateThisClip = 20,
        kIdVocalTune        = 21,
        kIdVocalTuneBypass  = 22,
        kIdNormalize        = 23,
        kIdAutomationBase   = 300,
        // Presets: 100-106
        kIdPresetBase       = 100,
        // Safe Defaults: 200-204
        kIdSafeDefaultBase  = 200,
    };

    static juce::String buildStatusString(const ArrangementClipModel& clip,
                                           const ClipFreezeState* fs)
    {
        const auto& tp = clip.timePitch;
        juce::String s = TimePitchUICore::modeName(tp.mode);

        if (std::abs(tp.pitchSemitones) > 0.05)
            s += "  " + juce::String(tp.pitchSemitones > 0 ? "+" : "")
               + juce::String(tp.pitchSemitones, 1) + "st";

        if (std::abs(tp.stretchRatio - 1.0) > 0.01)
            s += "  " + juce::String((int)std::round(tp.stretchRatio * 100)) + "%";

        if (fs && fs->frozen) s += "  \xf0\x9f\xa7\x8a Frozen";

        return s;
    }

    static juce::PopupMenu buildPresetsMenu()
    {
        juce::PopupMenu sub;
        const auto names = TimePitchPresetsCore::allNames();
        for (int i = 0; i < (int)names.size(); ++i)
            sub.addItem(kIdPresetBase + i, names[i]);
        return sub;
    }

    static juce::PopupMenu buildSafeDefaultsMenu()
    {
        juce::PopupMenu sub;
        sub.addItem(kIdSafeDefaultBase + 0, "Vocals  \xe2\x86\x92  Vocal Clean");
        sub.addItem(kIdSafeDefaultBase + 1, "Drums   \xe2\x86\x92  Percussion");
        sub.addItem(kIdSafeDefaultBase + 2, "Loops   \xe2\x86\x92  Stretch");
        sub.addItem(kIdSafeDefaultBase + 3, "FX      \xe2\x86\x92  Texture");
        sub.addItem(kIdSafeDefaultBase + 4, "Chipmunk / Demon  \xe2\x86\x92  Resample");
        return sub;
    }

    // Map safe default index → preset name
    static const char* safeDefaultPresetName(int idx)
    {
        switch (idx)
        {
            case 0: return "Vocal Clean";
            case 1: return "Percussion"; // special: sets mode only
            case 2: return "Stretch";    // special: sets mode only
            case 3: return "Texture Cloud";
            case 4: return "Resample";   // special
            default: return nullptr;
        }
    }

    static void handleResult(int result,
                              ArrangementClipModel* clip,
                              const ClipContextCallbacks& cb)
    {
        if (!clip) return;

        if (result == kIdCut)
        {
            if (cb.cutClip) cb.cutClip(clip);
        }
        else if (result == kIdCopy)
        {
            if (cb.copyClip) cb.copyClip(clip);
        }
        else if (result == kIdDuplicate)
        {
            if (cb.duplicateClip) cb.duplicateClip(clip);
        }
        else if (result == kIdPaste)
        {
            if (cb.pasteClip) cb.pasteClip(cb.clickTime, cb.clickTrack);
        }
        else if (result == kIdDelete)
        {
            if (cb.deleteClip) cb.deleteClip(clip);
        }
        else if (result == kIdMute)
        {
            if (cb.muteClip) cb.muteClip(clip);
        }
        else if (result == kIdRename)
        {
            if (cb.renameClip) cb.renameClip(clip);
        }
        else if (result == kIdPitchTimePanel)
        {
            if (cb.openTimePitchPanel) cb.openTimePitchPanel(clip);
        }
        else if (result == kIdBounce)
        {
            if (cb.bounceClip) cb.bounceClip(clip);
        }
        else if (result == kIdFreeze)
        {
            if (cb.freezeClip) cb.freezeClip(clip);
        }
        else if (result == kIdUnfreeze)
        {
            if (cb.unfreezeClip) cb.unfreezeClip(clip);
        }
        else if (result == kIdReset)
        {
            if (cb.resetTimePitch) cb.resetTimePitch(clip);
        }
        else if (result == kIdAutomateThisClip)
        {
            if (cb.openAutomateClipPanel) cb.openAutomateClipPanel(clip);
        }
        else if (result == kIdVocalTune)
        {
            if (cb.openVocalTune) cb.openVocalTune(clip);
        }
        else if (result == kIdVocalTuneBypass)
        {
            if (cb.toggleVocalTuneBypass) cb.toggleVocalTuneBypass(clip);
        }
        else if (result == kIdNormalize)
        {
            if (cb.normalizeClip) cb.normalizeClip(clip);
        }
        else if (result >= kIdPresetBase && result < kIdPresetBase + 7)
        {
            const int idx = result - kIdPresetBase;
            const auto names = TimePitchPresetsCore::allNames();
            if (idx < (int)names.size() && cb.applyPreset)
                cb.applyPreset(clip, names[idx]);
        }
        else if (result >= kIdSafeDefaultBase && result < kIdSafeDefaultBase + 5)
        {
            const int idx = result - kIdSafeDefaultBase;
            applySafeDefault(idx, clip, cb);
        }
    }

    static void applySafeDefault(int idx, ArrangementClipModel* clip,
                                  const ClipContextCallbacks& cb)
    {
        // Safe defaults: apply mode + sensible parameters
        switch (idx)
        {
            case 0: // Vocals → Vocal Clean preset
                if (cb.applyPreset) cb.applyPreset(clip, "Vocal Clean");
                break;
            case 1: // Drums → Percussion mode, identity
            {
                const auto old = clip->timePitch;
                TimePitchState s;
                s.mode           = TimePitchMode::Percussion;
                s.pitchSemitones = 0.0;
                s.stretchRatio   = 1.0;
                clip->timePitch  = s;
                (void)old; // caller should wrap in undo action
                break;
            }
            case 2: // Loops → Stretch mode, identity
            {
                TimePitchState s;
                s.mode           = TimePitchMode::Stretch;
                s.pitchSemitones = 0.0;
                s.stretchRatio   = 1.0;
                clip->timePitch  = s;
                break;
            }
            case 3: // FX → Texture Cloud preset
                if (cb.applyPreset) cb.applyPreset(clip, "Texture Cloud");
                break;
            case 4: // Chipmunk/Demon → Resample, identity (user sets pitch)
            {
                TimePitchState s;
                s.mode           = TimePitchMode::Resample;
                s.pitchSemitones = 0.0;
                s.stretchRatio   = 1.0;
                clip->timePitch  = s;
                break;
            }
            default: break;
        }
    }
};

} // namespace ArrangementEditor
