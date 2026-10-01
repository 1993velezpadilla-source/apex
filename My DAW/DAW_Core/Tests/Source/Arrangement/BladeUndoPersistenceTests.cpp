#include <JuceHeader.h>
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ArrangementClipStateCore.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ArrangementUndoCore.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ClipSplitCore.h"
#include "../../../Builds/VisualStudio2026/ArrangementEditor/ArrangementViewCore.h"
#include "../../../Source/ClipCore/Clip.h"
#include "../../../Source/AudioEngineCore/AudioFileManager.h"
#include "../../../Source/AutomationCore/AutomationManagerCore.h"
#include "../../../Source/PluginHostCore/ClipRegionPluginCore.h"
#include "../../../Source/PluginScanCore/PluginScanFormatsCore.h"
#include "../../../Source/C4Core/C4NativePluginFormat.h"
#include "../../../Source/G10Core/G10NativePluginFormat.h"
#include <cmath>
#include <cstring>

using namespace ArrangementEditor;

class BladeUndoPersistenceTests final : public juce::UnitTest
{
public:
    BladeUndoPersistenceTests() : juce::UnitTest ("blade-undo-persistence.v1", "APEX.Arrangement") {}

    void runTest() override
    {
        beginTest ("split then undo restores original model then redo restores split");
        {
            ArrangementClipStateCore state;
            ArrangementUndoCore undo;

            ArrangementClipModel original;
            original.id = juce::Uuid();
            original.startTime = 0.0;
            original.length = 2.0;
            original.sourceOffset = 0.0;
            original.sourceStartSample = 0;
            original.sourceEndSample = 88200;
            original.sourceSampleRate = 44100.0;
            original.timePitch.pitchSemitones = 0.0;
            original.timePitch.stretchRatio = 1.0;

            state.addClip (original);
            expectEquals (state.allClips().size(), (size_t) 1);

            auto splitResult = ClipSplitCore::splitClip (original, 1.0);
            expect (splitResult.valid);

            auto origVisual = original;
            auto leftVisual = splitResult.left;
            auto rightVisual = splitResult.right;

            auto redoFn = [&state, origUuid = original.id, left = leftVisual, right = rightVisual]()
            {
                state.removeClip (origUuid);
                state.addClip (left);
                state.addClip (right);
            };
            auto undoFn = [&state, origUuid = original.id, leftUuid = leftVisual.id,
                           rightUuid = rightVisual.id, orig = origVisual]()
            {
                state.removeClip (leftUuid);
                state.removeClip (rightUuid);
                state.addClip (orig);
            };

            undo.executeCommand (std::make_unique<LambdaArrangementCommand>(
                "Split Clip", std::move(redoFn), std::move(undoFn), false));

            expectEquals (state.allClips().size(), (size_t) 2);
            expect (state.findClip (leftVisual.id) != nullptr);
            expect (state.findClip (rightVisual.id) != nullptr);

            undo.undo();
            expectEquals (state.allClips().size(), (size_t) 1);
            expect (state.findClip (original.id) != nullptr);
            expect (state.findClip (leftVisual.id) == nullptr);

            undo.redo();
            expectEquals (state.allClips().size(), (size_t) 2);
            expect (state.findClip (leftVisual.id) != nullptr);
            expect (state.findClip (rightVisual.id) != nullptr);
        }

        beginTest ("AudioClip getState and restoreState round-trip preserves engine properties");
        {
            DAW::ClipManager clipMgr;
            auto* clip = clipMgr.createAudioClip ("Test Clip", juce::File());

            const auto origStart = (DAW::SamplePosition) 44100;
            const auto origLen = (DAW::SamplePosition) 176400;
            const auto origSrcOffset = (DAW::SamplePosition) 22050;
            const auto origFadeIn = (DAW::SamplePosition) 1000;
            const auto origFadeOut = (DAW::SamplePosition) 2000;

            clip->setStartPosition (origStart);
            clip->setLength (origLen);
            clip->setSourceOffset (origSrcOffset);
            clip->setSourceStartSample (5000);
            clip->setSourceEndSample (42000);
            clip->setFadeInLength (origFadeIn);
            clip->setFadeOutLength (origFadeOut);
            clip->setPitchTargetFromUI (3.5f);
            clip->setTimeStretch (1.25f);
            clip->setTimePitchMode (1);

            auto valueTreeState = clip->getState();
            expect (valueTreeState.isValid(), "should produce valid ValueTree");

            DAW::AudioClip restored ("restored", "Restored");
            restored.restoreState (valueTreeState);

            expectEquals (restored.getStartPosition(), origStart);
            expectEquals (restored.getLength(), origLen);
            expectEquals (restored.getSourceOffset(), origSrcOffset);
            expectEquals (restored.getFadeInLength(), origFadeIn);
            expectEquals (restored.getFadeOutLength(), origFadeOut);
            expectWithinAbsoluteError (restored.getPitch(), 3.5f, 0.01f);
            expectWithinAbsoluteError (restored.getTimeStretch(), 1.25f, 0.01f);
            expectEquals (restored.getTimePitchMode(), 1);
            expectEquals (restored.getSourceStartSample(), (int64_t) 5000);
            expectEquals (restored.getSourceEndSample(), (int64_t) 42000);
        }
    }
};

static BladeUndoPersistenceTests bladeUndoPersistenceTests;

namespace
{
    ArrangementClipModel makeTransactionalClip()
    {
        ArrangementClipModel clip;
        clip.id = juce::Uuid();
        clip.trackIndex = 0;
        clip.startTime = 0.0;
        clip.length = 2.0;
        clip.sourceOffset = 0.0;
        clip.sourceStartSample = 0;
        clip.sourceEndSample = 88200;
        clip.sourceSampleRate = 44100.0;
        clip.timePitch.pitchSemitones = 0.0;
        clip.timePitch.stretchRatio = 1.0;
        return clip;
    }

    DAW::ClipID wireEngineClip(ArrangementEditor::ArrangementViewCore& view,
                               DAW::ClipManager& clipManager,
                               const ArrangementClipModel& model)
    {
        view.m_suppressEngineClipCreation = true;
        view.m_clipState.addClip(model);
        view.m_suppressEngineClipCreation = false;

        auto* engineClip = clipManager.createAudioClip("Transactional Split", juce::File());
        if (engineClip == nullptr)
            return {};

        engineClip->setStartPosition(0);
        engineClip->setLength(88200);
        engineClip->setSourceOffset(0);
        engineClip->setSourceStartSample(0);
        engineClip->setSourceEndSample(88200);
        view.m_uuidToEngineId[model.id] = engineClip->getID();
        return engineClip->getID();
    }

    juce::Uuid findRightVisualId(const ArrangementEditor::ArrangementViewCore& view,
                                 const juce::Uuid& originalId)
    {
        for (const auto& clip : view.m_clipState.allClips())
            if (clip.id != originalId)
                return clip.id;
        return {};
    }

    bool pluginSnapshotsEquivalent(const DAW::ClipRegionPluginCore::ClipStateSnapshot& a,
                                   const DAW::ClipRegionPluginCore::ClipStateSnapshot& b)
    {
        if (a.size() != b.size())
            return false;

        for (size_t i = 0; i < a.size(); ++i)
        {
            if (a[i].description.name != b[i].description.name
                || a[i].description.pluginFormatName != b[i].description.pluginFormatName
                || a[i].description.fileOrIdentifier != b[i].description.fileOrIdentifier
                || a[i].bypassed != b[i].bypassed
                || a[i].state.getSize() != b[i].state.getSize())
                return false;

            if (a[i].state.getSize() > 0
                && std::memcmp(a[i].state.getData(), b[i].state.getData(), a[i].state.getSize()) != 0)
                return false;
        }
        return true;
    }

    const DAW::AutomationLaneCore* findAutomationLane(
        const DAW::AutomationManagerCore& manager,
        const juce::String& parameterId)
    {
        for (const auto& lane : manager.getLanes())
            if (lane.parameterId == parameterId)
                return &lane;
        return nullptr;
    }

    const DAW::AutomationPoint* findAutomationPoint(
        const DAW::AutomationLaneCore& lane,
        int64_t sample)
    {
        for (const auto& point : lane.points)
            if (point.timeSamples == sample)
                return &point;
        return nullptr;
    }
}

class SplitTransactionStableIdTests final : public juce::UnitTest
{
public:
    SplitTransactionStableIdTests()
        : juce::UnitTest("split.transaction.stable-id.v1", "APEX.Arrangement") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        beginTest("plugin-free split restores exact original state and redoes with stable IDs");

        DAW::ClipManager clipManager;
        DAW::AudioFileManager audioFiles;
        ArrangementEditor::ArrangementViewCore view;
        view.setAudioEngineBridge(&clipManager, &audioFiles, nullptr, 44100.0);
        view.m_undo.clear();

        const auto original = makeTransactionalClip();
        const auto originalEngineId = wireEngineClip(view, clipManager, original);
        auto* originalEngine = clipManager.getClip(originalEngineId);
        expect(originalEngine != nullptr);
        if (originalEngine == nullptr)
            return;
        const auto originalEngineState = originalEngine->getState();

        view.performSplit(original, 1.0);
        const auto rightVisualId = findRightVisualId(view, original.id);
        expect(!rightVisualId.isNull(), "split must publish a right visual fragment");
        expect(view.m_clipState.findClip(original.id) != nullptr,
               "left visual fragment must retain the original ID");

        const auto rightIt = view.m_uuidToEngineId.find(rightVisualId);
        expect(rightIt != view.m_uuidToEngineId.end(), "right reverse map must be published");
        if (rightIt == view.m_uuidToEngineId.end())
            return;
        const auto rightEngineId = rightIt->second;
        expect(rightEngineId.isNotEmpty() && rightEngineId != originalEngineId,
               "right engine fragment must receive a generated stable ID");

        expect(view.m_undo.undo(), "split undo must execute");
        expectEquals(view.m_clipState.allClips().size(), (size_t)1);
        expect(view.m_clipState.findClip(original.id) != nullptr);
        expect(view.m_clipState.findClip(rightVisualId) == nullptr);
        expect(clipManager.getClip(originalEngineId) != nullptr,
               "undo must restore the original engine ID");
        expect(clipManager.getClip(rightEngineId) == nullptr,
               "undo must remove the right engine fragment");
        expect(originalEngineState.isEquivalentTo(clipManager.getClip(originalEngineId)->getState()),
               "undo must restore the exact original engine state");

        expect(view.m_undo.redo(), "split redo must execute");
        expect(view.m_clipState.findClip(original.id) != nullptr);
        expect(view.m_uuidToEngineId[rightVisualId] == rightEngineId,
               "redo must recreate the same right reverse-map identity");
        expect(clipManager.getClip(rightEngineId) != nullptr,
               "redo must recreate the same right engine ID");

        view.m_undo.clear();
    }
};

static SplitTransactionStableIdTests splitTransactionStableIdTests;

class SplitTransactionAutomationTests final : public juce::UnitTest
{
public:
    SplitTransactionAutomationTests()
        : juce::UnitTest("split.transaction.automation.v1", "APEX.Arrangement") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        beginTest("crossing automation partitions and restores exactly on undo");

        DAW::ClipManager clipManager;
        DAW::AudioFileManager audioFiles;
        DAW::AutomationManagerCore automation;
        ArrangementEditor::ArrangementViewCore view;
        view.setAudioEngineBridge(&clipManager, &audioFiles, nullptr, 44100.0);
        view.setAutomationManager(&automation);
        view.m_undo.clear();

        const auto original = makeTransactionalClip();
        const auto originalEngineId = wireEngineClip(view, clipManager, original);
        const auto parameterId = DAW::AutomationLaneCore::makeClipGainParameterId(originalEngineId);
        auto& lane = automation.getOrCreateLane("transaction-track", parameterId);
        lane.addPoint(0, 0.10f);
        lane.addPoint(22050, 0.30f);
        lane.addPoint(44100, 0.70f);
        lane.addPoint(88200, 0.20f);
        lane.points[1].curveToNext = DAW::AutomationCurveType::Smooth;
        lane.points[1].tensionToNext = 0.45f;
        lane.points[2].curveToNext = DAW::AutomationCurveType::DoubleCurve;
        lane.points[2].tensionToNext = -0.25f;
        automation.publishSnapshot();
        const auto originalAutomation = automation.captureClipAutomationState(originalEngineId);

        view.performSplit(original, 1.0);
        const auto rightVisualId = findRightVisualId(view, original.id);
        const auto rightEngineId = view.m_uuidToEngineId[rightVisualId];
        const auto rightParameterId = DAW::AutomationLaneCore::makeClipGainParameterId(rightEngineId);
        const auto* leftLane = findAutomationLane(automation, parameterId);
        const auto* rightLane = findAutomationLane(automation, rightParameterId);

        expect(leftLane != nullptr, "left automation must retain the original ClipID");
        expect(rightLane != nullptr, "right automation must use the right ClipID");
        if (leftLane != nullptr && rightLane != nullptr)
        {
            expect(findAutomationPoint(*leftLane, 44100) != nullptr,
                   "left boundary point must be preserved");
            expect(findAutomationPoint(*rightLane, 44100) != nullptr,
                   "right boundary point must be preserved");
            expect(findAutomationPoint(*leftLane, 22050)->curveToNext == DAW::AutomationCurveType::Smooth);
            expectWithinAbsoluteError(findAutomationPoint(*leftLane, 22050)->tensionToNext, 0.45f, 1.0e-6f);
            expect(findAutomationPoint(*leftLane, 44100)->curveToNext == DAW::AutomationCurveType::DoubleCurve);
            expectWithinAbsoluteError(findAutomationPoint(*leftLane, 44100)->tensionToNext, -0.25f, 1.0e-6f);
        }

        expect(view.m_undo.undo(), "automation split undo must execute");
        expect(automation.captureClipAutomationState(originalEngineId).isEquivalentTo(originalAutomation),
               "undo must restore the exact original automation snapshot");
        expect(findAutomationLane(automation, rightParameterId) == nullptr,
               "undo must remove right-fragment automation");

        view.m_undo.clear();
    }
};

static SplitTransactionAutomationTests splitTransactionAutomationTests;

class SplitTransactionPluginTests final : public juce::UnitTest
{
public:
    SplitTransactionPluginTests()
        : juce::UnitTest("split.transaction.plugin.v1", "APEX.Arrangement") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        beginTest("Plugin A clones independently, Plugin B disappears on undo, and plugin-free undo is exact");

        DAW::ClipManager clipManager;
        DAW::AudioFileManager audioFiles;
        DAW::AutomationManagerCore automation;
        DAW::ClipRegionPluginCore plugins;
        DAW::PluginScanFormatsCore formats;
        ArrangementEditor::ArrangementViewCore view;
        plugins.prepare(44100.0, 512);
        view.setAudioEngineBridge(&clipManager, &audioFiles, nullptr, 44100.0,
                                  &plugins, &formats.getManager());
        view.setAutomationManager(&automation);
        view.m_undo.clear();

        const auto original = makeTransactionalClip();
        const auto originalEngineId = wireEngineClip(view, clipManager, original);
        const auto pluginA = APEX::C4::C4NativePluginFormat::createC4Description();
        const auto pluginB = APEX::G10::G10NativePluginFormat::createG10Description();
        const auto loadedA = plugins.loadForClip(originalEngineId, pluginA, formats.getManager());
        expect(loadedA.success, "Plugin A must load for the original clip");
        if (!loadedA.success)
            return;

        const auto originalPluginState = plugins.captureClipState(originalEngineId);
        view.performSplit(original, 1.0);
        const auto rightVisualId = findRightVisualId(view, original.id);
        const auto rightEngineId = view.m_uuidToEngineId[rightVisualId];
        const auto leftEntries = plugins.getEntriesForClip(originalEngineId);
        const auto rightEntries = plugins.getEntriesForClip(rightEngineId);
        expectEquals(leftEntries.size(), (size_t)1, "left must contain exactly Plugin A");
        expectEquals(rightEntries.size(), (size_t)1, "right must contain exactly Plugin A");

        if (leftEntries.size() == 1 && rightEntries.size() == 1)
        {
            auto* leftInstance = plugins.findEntryById(originalEngineId, leftEntries.front().instanceId);
            auto* rightInstance = plugins.findEntryById(rightEngineId, rightEntries.front().instanceId);
            expect(leftInstance != nullptr && rightInstance != nullptr);
            if (leftInstance != nullptr && rightInstance != nullptr)
                expect(leftInstance->instance.get() != rightInstance->instance.get(),
                       "left and right must own independent plugin instances");

            plugins.setBypassed(rightEngineId, rightEntries.front().instanceId, true);
            expect(!plugins.getEntriesForClip(originalEngineId).front().bypassed,
                   "right bypass changes must not mutate left runtime state");
            expect(plugins.getEntriesForClip(rightEngineId).front().bypassed,
                   "right bypass change must remain local to right");
        }

        const auto loadedB = plugins.loadForClip(rightEngineId, pluginB, formats.getManager());
        expect(loadedB.success, "Plugin B must load only on the right fragment");

        expect(view.m_undo.undo(), "plugin split undo must execute");
        expect(plugins.getEntriesForClip(rightEngineId).empty(),
               "undo must remove right Plugin A and Plugin B state");
        expect(pluginSnapshotsEquivalent(plugins.captureClipState(originalEngineId), originalPluginState),
               "undo must restore Plugin A exactly");

        expect(view.m_undo.redo(), "plugin split redo must execute");
        expect(view.m_uuidToEngineId[rightVisualId] == rightEngineId,
               "redo must retain the right stable plugin owner ID");
        expectEquals(plugins.getEntriesForClip(originalEngineId).size(), (size_t)1);
        expectEquals(plugins.getEntriesForClip(rightEngineId).size(), (size_t)1);

        view.m_undo.clear();
    }
};

static SplitTransactionPluginTests splitTransactionPluginTests;

class SplitTransactionRollbackTests final : public juce::UnitTest
{
public:
    SplitTransactionRollbackTests()
        : juce::UnitTest("split.transaction.rollback.v1", "APEX.Arrangement") {}

    void runTest() override
    {
        juce::ScopedJuceInitialiser_GUI gui;
        beginTest("failure injection restores engine, model, plugin, automation, and maps");

        DAW::ClipManager clipManager;
        DAW::AudioFileManager audioFiles;
        DAW::AutomationManagerCore automation;
        DAW::ClipRegionPluginCore plugins;
        DAW::PluginScanFormatsCore formats;
        ArrangementEditor::ArrangementViewCore view;
        plugins.prepare(44100.0, 512);
        view.setAudioEngineBridge(&clipManager, &audioFiles, nullptr, 44100.0,
                                  &plugins, &formats.getManager());
        view.setAutomationManager(&automation);
        view.m_undo.clear();

        const auto original = makeTransactionalClip();
        const auto originalEngineId = wireEngineClip(view, clipManager, original);
        const auto pluginA = APEX::C4::C4NativePluginFormat::createC4Description();
        expect(plugins.loadForClip(originalEngineId, pluginA, formats.getManager()).success,
               "rollback fixture Plugin A must load");
        const auto originalPluginState = plugins.captureClipState(originalEngineId);

        const auto parameterId = DAW::AutomationLaneCore::makeClipGainParameterId(originalEngineId);
        automation.addPoint("transaction-track", parameterId, 0, 0.25f);
        automation.addPoint("transaction-track", parameterId, 44100, 0.75f);
        const auto originalAutomation = automation.captureClipAutomationState(originalEngineId);
        const auto originalEngineState = clipManager.getClip(originalEngineId)->getState();

        view.splitTransactionFailureInjector = [](const juce::String& stage)
        {
            return stage == "engine";
        };
        view.performSplit(original, 1.0);
        view.splitTransactionFailureInjector = nullptr;

        expectEquals(view.m_clipState.allClips().size(), (size_t)1,
                     "failed split must not publish a partial model");
        expect(view.m_clipState.findClip(original.id) != nullptr);
        expectEquals(clipManager.getAllClips().size(), 1,
                     "failed split must not leave an orphan engine clip");
        expect(clipManager.getClip(originalEngineId) != nullptr);
        expect(originalEngineState.isEquivalentTo(clipManager.getClip(originalEngineId)->getState()),
               "failed split must restore the original engine state");
        expect(pluginSnapshotsEquivalent(plugins.captureClipState(originalEngineId), originalPluginState),
               "failed split must restore the original plugin state");
        expect(automation.captureClipAutomationState(originalEngineId).isEquivalentTo(originalAutomation),
               "failed split must restore the original automation");
        expectEquals(view.m_uuidToEngineId.size(), (size_t)1,
                     "failed split must leave one coherent reverse map");
        expect(view.m_uuidToEngineId[original.id] == originalEngineId);
        expect(!view.m_undo.canUndo(), "failed split must not enter history");

        view.m_undo.clear();
    }
};

static SplitTransactionRollbackTests splitTransactionRollbackTests;
