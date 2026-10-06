// =============================================================================
//  ApexTuneSessionCore.h
//  Owns the data + editor that live for the duration of one open editing
//  session. Constructed when the user opens the editor on a clip; destroyed
//  when they close the window.
//
//  Drop-in: Source/VocalTuneCore/ApexTuneSessionCore.h
//  Depends on: JUCE, ApexTuneTypes.h, the UI editor header.
//  Touches:    nothing else in the APEX codebase.
//
//  Why this exists:
//   The editor doesn't own its data (state, audio, analysis). Without a
//   holder, all of that has to be passed around carefully. ApexTuneSessionCore
//   bundles them so the IntegrationCore can hand a single std::unique_ptr to
//   the adapter's showEditorWindow().
//
//  Lifecycle:
//   - IntegrationCore constructs Session on the message thread after analysis
//     completes.
//   - Session is wrapped in a "host" Component that gets handed to
//     IEngineAdapter::showEditorWindow().
//   - When the host Component is destroyed (window closed), Session
//     destructs, releasing the audio buffer + analysis + editor.
//   - On destruction, Session calls the onSessionClosed callback so the
//     IntegrationCore can copy the final state back into its registry.
// =============================================================================

#pragma once

#include "ApexTuneTypes.h"
#include "ApexSibilantDetectorCore.h"
#include "../VocalTuneUI/ApexTuneEditorComponent.h"

namespace apex { namespace vocaltune {

class ApexTuneSessionCore : private juce::Timer
{
public:
    // Constructed with everything the editor needs.
    ApexTuneSessionCore (juce::String                   clipId,
                         juce::File                     sourceFile,
                         std::vector<float>             monoAudio,
                         double                         sampleRate,
                         ApexTuneAnalysis               analysis,
                         std::vector<ApexSibilantRegion> sibilants,
                         ApexTuneClipState              initialState);

    ~ApexTuneSessionCore();

    // ---- Read-only accessors (for IntegrationCore callbacks) --------------
    const juce::String&      getClipId()    const noexcept { return clipId_; }
    const ApexTuneClipState& getState()     const noexcept { return state_; }
    ApexTuneClipState&       getMutableState()    noexcept { return state_; }

    const std::vector<float>&              getMono()      const noexcept { return *mono_; }
    double                                 getSampleRate() const noexcept { return sampleRate_; }
    const ApexTuneAnalysis&                getAnalysis()  const noexcept { return *analysis_; }
    const std::vector<ApexSibilantRegion>& getSibilants() const noexcept { return *sibilants_; }

    // Shared-ownership accessors -- workers capture these to safely outlive
    // the session if the user closes the editor mid-render.
    std::shared_ptr<const std::vector<float>>              getMonoPtr()      const { return mono_; }
    std::shared_ptr<const ApexTuneAnalysis>                getAnalysisPtr()  const { return analysis_; }
    std::shared_ptr<const std::vector<ApexSibilantRegion>> getSibilantsPtr() const { return sibilants_; }

    // ---- The editor component (owned) --------------------------------------
    ApexTuneEditorComponent& getEditor() noexcept { return *editor_; }
    void requestDeferredRender();

    // ---- Callbacks (set by IntegrationCore) -------------------------------
    std::function<void (ApexTuneSessionCore&)>  onStateModified;
    std::function<void (ApexTuneSessionCore&)>  onRenderRequested;
    std::function<void (ApexTuneSessionCore&, bool)> onBypassChanged;
    std::function<void (ApexTuneSessionCore&)>  onSessionClosed;

private:
    juce::String                          clipId_;
    juce::File                            sourceFile_;
    std::shared_ptr<std::vector<float>>             mono_;
    std::shared_ptr<ApexTuneAnalysis>               analysis_;
    std::shared_ptr<std::vector<ApexSibilantRegion>> sibilants_;
    double                                sampleRate_;
    ApexTuneClipState                     state_;
    std::unique_ptr<ApexTuneEditorComponent> editor_;

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApexTuneSessionCore)
};

}} // namespace apex::vocaltune
