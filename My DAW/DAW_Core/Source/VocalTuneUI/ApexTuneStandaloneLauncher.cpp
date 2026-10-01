// =============================================================================
//  ApexTuneStandaloneLauncher.cpp
//  See header.
//
//  Drop-in: Source/VocalTuneUI/ApexTuneStandaloneLauncher.cpp
// =============================================================================

#include "ApexTuneStandaloneLauncher.h"
#include "ApexTuneEditorComponent.h"
#include "ApexTuneColors.h"
#include "../VocalTuneCore/ApexPitchDetectionCore.h"
#include "../VocalTuneCore/ApexNoteSegmentationCore.h"
#include "../VocalTuneCore/ApexSibilantDetectorCore.h"
#include "../VocalTuneCore/ApexTuneRenderCore.h"

namespace apex { namespace vocaltune {

namespace
{
    // -------------------------------------------------------------------------
    // Host component: owns all the data that needs to outlive the launcher
    // function. The DialogWindow takes ownership of this component, and when
    // the user closes the window, this destructs, freeing audio + state.
    // -------------------------------------------------------------------------
    class SmokeTestHost : public juce::Component
    {
    public:
        SmokeTestHost (juce::File           sourceFile,
                       std::vector<float>   mono,
                       double               sampleRate,
                       ApexTuneAnalysis     analysis,
                       std::vector<ApexTuneNote> notes,
                       std::vector<ApexSibilantRegion> sibilants)
            : sourceFile_ (std::move (sourceFile))
            , mono_       (std::move (mono))
            , sampleRate_ (sampleRate)
            , analysis_   (std::move (analysis))
            , sibilants_  (std::move (sibilants))
        {
            state_.clipId      = juce::Uuid().toString();
            state_.sourceFile  = sourceFile_;
            state_.notes       = std::move (notes);
            state_.analyzed    = true;
            state_.scaleRoot   = "C";
            state_.scaleType   = "Chromatic";

            editor_ = std::make_unique<ApexTuneEditorComponent>();
            editor_->setClipState  (&state_);
            editor_->setAnalysis   (&analysis_);
            editor_->setSourceAudio (mono_.data(), (int) mono_.size(), sampleRate_);

            editor_->onStateModified = [this]
            {
                DBG ("[ApexTune SmokeTest] state modified, renderVersion="
                     + juce::String (state_.renderVersion));
            };

            editor_->onRenderRequested = [this]
            {
                DBG ("[ApexTune SmokeTest] render requested...");

                auto res = ApexTuneRenderCore::renderClipMono (
                    mono_.data(), (int) mono_.size(), sampleRate_,
                    analysis_, state_.notes, sibilants_);

                if (res.success)
                {
                    DBG ("[ApexTune SmokeTest] render OK: "
                         + juce::String ((int) res.audio.size()) + " samples @ "
                         + juce::String (res.sampleRate) + " Hz");
                }
                else
                {
                    DBG ("[ApexTune SmokeTest] render FAIL: " + res.errorMessage);
                }
            };

            editor_->onBypassChanged = [] (bool b)
            {
                DBG ("[ApexTune SmokeTest] bypass = " + juce::String ((int) b));
            };

            addAndMakeVisible (*editor_);
            setSize (960, 480);
        }

        void resized() override
        {
            if (getWidth() <= 0 || getHeight() <= 0) return;
            if (editor_ != nullptr) editor_->setBounds (getLocalBounds());
        }

    private:
        juce::File                          sourceFile_;
        std::vector<float>                  mono_;
        double                              sampleRate_;
        ApexTuneAnalysis                    analysis_;
        std::vector<ApexSibilantRegion>     sibilants_;
        ApexTuneClipState                   state_;
        std::unique_ptr<ApexTuneEditorComponent> editor_;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmokeTestHost)
    };

    // -------------------------------------------------------------------------
    // Load a file -> mono float vector. Returns empty on failure.
    // -------------------------------------------------------------------------
    std::vector<float> loadMonoFromFile (const juce::File& file, double& outSr)
    {
        outSr = 0.0;
        if (! file.existsAsFile()) return {};

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();

        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        if (reader == nullptr) return {};

        const int    numSamples = (int) reader->lengthInSamples;
        const int    numChans   = (int) reader->numChannels;
        outSr                   = reader->sampleRate;
        if (numSamples <= 0 || numChans <= 0) return {};

        juce::AudioBuffer<float> buf (numChans, numSamples);
        reader->read (&buf, 0, numSamples, 0, true, numChans > 1);

        std::vector<float> mono ((size_t) numSamples, 0.0f);
        if (numChans == 1)
        {
            const float* src = buf.getReadPointer (0);
            for (int i = 0; i < numSamples; ++i) mono[(size_t) i] = src[i];
        }
        else
        {
            const float invN = 1.0f / (float) numChans;
            for (int i = 0; i < numSamples; ++i)
            {
                float sum = 0.0f;
                for (int c = 0; c < numChans; ++c) sum += buf.getReadPointer (c)[i];
                mono[(size_t) i] = sum * invN;
            }
        }
        return mono;
    }

    // -------------------------------------------------------------------------
    // Build the editor window from a loaded clip. Must run on the message thread.
    // -------------------------------------------------------------------------
    void buildAndShowDialog (const juce::File& file)
    {
        double sr = 0.0;
        auto mono = loadMonoFromFile (file, sr);
        if (mono.empty() || sr <= 0.0)
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::WarningIcon,
                "APEX Vocal Tune",
                "Could not load audio: " + file.getFileName());
            return;
        }

        // Analysis pipeline (blocking on message thread -- smoke test only).
        auto analysis  = ApexPitchDetectionCore::analyzeOffline
                             (mono.data(), (int) mono.size(), sr);
        auto notes     = ApexNoteSegmentationCore::segment (analysis);
        auto sibilants = ApexSibilantDetectorCore::detect (analysis);
        ApexSibilantDetectorCore::markOverlappingNotes (notes, sibilants);

        // Wrap everything in the host component.
        auto host = std::make_unique<SmokeTestHost> (
            file, std::move (mono), sr,
            std::move (analysis), std::move (notes), std::move (sibilants));

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned             (host.release());
        opts.dialogTitle                  = "APEX Vocal Tune - " + file.getFileName();
        opts.dialogBackgroundColour       = Col::VocalTune::background();
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar            = true;
        opts.resizable                    = true;
        opts.launchAsync();
    }
}

// -----------------------------------------------------------------------------
void ApexTuneStandaloneLauncher::launch()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Pick a vocal clip",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff;*.flac");

    chooser->launchAsync (
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles,
        [chooser] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (! file.existsAsFile()) return;
            buildAndShowDialog (file);
        });
}

}} // namespace apex::vocaltune
