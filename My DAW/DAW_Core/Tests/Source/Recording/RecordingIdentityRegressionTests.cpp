#include <JuceHeader.h>
#include "../../../Source/ClipCore/Clip.h"
#include "../../../Source/CommandCore/RecordCommands.h"
#include <set>

class RecordingIdentityRegressionTests final : public juce::UnitTest
{
public:
    RecordingIdentityRegressionTests()
        : juce::UnitTest ("recording.reload-clip-identity.v1", "APEX.Recording") {}

    void runTest() override
    {
        beginTest ("recording after restore cannot reuse the imported beat ID");

        DAW::ClipManager clips;

        const auto idProbe = DAW::IDGenerator::generateClipID();
        const auto probeSuffix = idProbe.startsWith ("CLIP_")
            ? idProbe.substring (5) : juce::String();
        const auto persistedBeatId = probeSuffix.isNotEmpty()
                                         && probeSuffix.containsOnly ("0123456789")
            ? "CLIP_" + juce::String (probeSuffix.getLargeIntValue() + 1)
            : juce::String ("CLIP_1");

        juce::ValueTree restoredClips ("Clips");
        juce::ValueTree beatState ("AudioClip");
        beatState.setProperty ("id", persistedBeatId, nullptr);
        beatState.setProperty ("name", "Imported Beat", nullptr);
        beatState.setProperty ("type", (int) DAW::ClipType::Audio, nullptr);
        beatState.setProperty ("trackID", "TRK_1", nullptr);
        beatState.setProperty ("startPosition", (juce::int64) 0, nullptr);
        beatState.setProperty ("length", (juce::int64) 48000, nullptr);
        beatState.setProperty ("sourceFile", "beat.wav", nullptr);
        restoredClips.appendChild (beatState, nullptr);
        clips.restoreState (restoredClips);

        auto* importedBeat = dynamic_cast<DAW::AudioClip*> (clips.getClip (persistedBeatId));
        expect (importedBeat != nullptr, "legacy beat must restore as " + persistedBeatId);
        if (importedBeat == nullptr)
            return;

        juce::ValueTree takeState ("AudioClip");
        takeState.setProperty ("name", "Track 3 Take", nullptr);
        takeState.setProperty ("type", (int) DAW::ClipType::Audio, nullptr);
        takeState.setProperty ("trackID", "TRK_3", nullptr);
        takeState.setProperty ("startPosition", (juce::int64) 0, nullptr);
        takeState.setProperty ("length", (juce::int64) 24000, nullptr);
        takeState.setProperty ("sourceFile", "take.wav", nullptr);

        auto* recordedTake = dynamic_cast<DAW::AudioClip*> (
            clips.recreateClipFromState (takeState));
        expect (recordedTake != nullptr, "recorded take must be created");
        if (recordedTake == nullptr)
            return;

        expect (recordedTake->getID().startsWith ("CLIP_"));
        expect (recordedTake->getID() != importedBeat->getID(),
                "recorded take must not reuse the restored beat ID");

        std::set<std::string> ids;
        for (auto* clip : clips.getAllClips())
            ids.insert (clip->getID().toStdString());
        expectEquals ((int) ids.size(), clips.getAllClips().size(),
                      "all live clip IDs must be unique");

        const auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("apex_recording_identity_" + juce::Uuid().toString());
        expect (tempDir.createDirectory(), "temporary fixture directory must be created");
        const auto beatFile = tempDir.getChildFile ("beat.wav");
        const auto takeFile = tempDir.getChildFile ("take.wav");

        const auto writeFixture = [] (const juce::File& file, int frames, float value)
        {
            std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
            if (stream == nullptr)
                return false;
            juce::WavAudioFormat format;
            auto writer = std::unique_ptr<juce::AudioFormatWriter> (
                format.createWriterFor (stream,
                    juce::AudioFormatWriterOptions()
                        .withSampleRate (48000.0)
                        .withNumChannels (1)
                        .withBitsPerSample (16)));
            if (writer == nullptr)
                return false;
            juce::AudioBuffer<float> buffer (1, frames);
            for (int i = 0; i < frames; ++i)
                buffer.setSample (0, i, value);
            return writer->writeFromAudioSampleBuffer (buffer, 0, frames);
        };

        expect (writeFixture (beatFile, 128, 0.25f));
        expect (writeFixture (takeFile, 256, -0.25f));
        importedBeat->setSourceFile (beatFile);
        recordedTake->setSourceFile (takeFile);

        DAW::AudioFileManager audioFiles;
        expect (audioFiles.loadForClip (importedBeat->getID(), beatFile, false).success);
        expect (audioFiles.loadForClip (recordedTake->getID(), takeFile, false).success);
        expectEquals (audioFiles.getSourceNumSamples (importedBeat->getID()),
                      (DAW::SamplePosition) 128);
        expectEquals (audioFiles.getSourceNumSamples (recordedTake->getID()),
                      (DAW::SamplePosition) 256,
                      "recorded audio must not replace the beat cache entry");

        DAW::ClipManager roundTrip;
        roundTrip.restoreState (clips.getState());
        auto* roundTripBeat = dynamic_cast<DAW::AudioClip*> (roundTrip.getClip (importedBeat->getID()));
        auto* roundTripTake = dynamic_cast<DAW::AudioClip*> (roundTrip.getClip (recordedTake->getID()));
        expect (roundTripBeat != nullptr && roundTripTake != nullptr,
                "both distinct IDs must survive save-state restoration");
        if (roundTripBeat != nullptr)
            expectEquals (roundTripBeat->getSourceFile().getFullPathName(),
                          beatFile.getFullPathName());
        if (roundTripTake != nullptr)
            expectEquals (roundTripTake->getSourceFile().getFullPathName(),
                          takeFile.getFullPathName());

        DAW::RecordAudioTakeCommand::Take commandTake;
        commandTake.state = recordedTake->getState();
        commandTake.sourceFile = recordedTake->getSourceFile();
        commandTake.currentId = recordedTake->getID();

        const auto recordedId = recordedTake->getID();
        DAW::RecordAudioTakeCommand command (clips, &audioFiles, { commandTake });
        command.execute();
        command.undo();

        expect (clips.getClip (recordedId) == nullptr,
                "undo must remove only the recorded take");
        auto* survivingBeat = clips.getClip (persistedBeatId);
        expect (survivingBeat != nullptr, "undo must preserve the imported beat");
        if (survivingBeat != nullptr)
            expectEquals (survivingBeat->getTrackID(), juce::String ("TRK_1"));

        beginTest ("duplicate persisted IDs are diagnosed before publication");

        const DAW::ClipID duplicateId ("CLIP_DUPLICATE");
        struct DuplicatePublicationListener final : DAW::ClipManager::Listener
        {
            void clipAdded (DAW::Clip* clip) override
            {
                if (clip != nullptr && clip->getID() == observedId)
                    ++publicationCount;
            }

            void clipsRestored() override { ++batchPublicationCount; }

            DAW::ClipID observedId;
            int publicationCount = 0;
            int batchPublicationCount = 0;
        } duplicateListener;

        duplicateListener.observedId = duplicateId;
        DAW::ClipManager duplicateRestore;
        duplicateRestore.addListener (&duplicateListener);
        juce::ValueTree duplicateRoot ("Clips");
        auto firstState = beatState.createCopy();
        auto secondState = takeState.createCopy();
        secondState.setProperty ("id", duplicateId, nullptr);
        firstState.setProperty ("id", duplicateId, nullptr);
        duplicateRoot.appendChild (firstState, nullptr);
        duplicateRoot.appendChild (secondState, nullptr);
        duplicateRestore.restoreState (duplicateRoot);
        duplicateRestore.removeListener (&duplicateListener);

        expectEquals (duplicateRestore.getAllClips().size(), 1,
                      "a duplicate persisted ID must not reach arrangement/cache publication");
        expectEquals (duplicateListener.publicationCount, 0,
                      "restore must suppress per-clip publication churn");
        expectEquals (duplicateListener.batchPublicationCount, 1,
                      "the validated restored generation must be published exactly once");
        auto* retained = duplicateRestore.getClip (duplicateId);
        expect (retained != nullptr);
        if (retained != nullptr)
            expectEquals (retained->getTrackID(), juce::String ("TRK_1"),
                          "the first valid persisted owner is retained");

        tempDir.deleteRecursively();
    }
};

static RecordingIdentityRegressionTests recordingIdentityRegressionTests;
