#include "PluginChainCore.h"

#include "../PluginSandboxCore/SandboxedPluginProxyCore.h"

namespace DAW {

namespace
{
PluginSandboxPreparation sandboxPreparationFromInsertOptions(
    const PluginInsertOptions& options)
{
    PluginSandboxPreparation preparation;
    const auto& layout = options.restoredLayoutTree;
    if (layout.isValid() && layout.hasType("Layout"))
    {
        // The worker transport currently has an 8-channel physical bound.
        // A persisted layout is a hint, not permission to widen that protocol.
        const auto inputChannels = juce::jlimit(
            1, 8, static_cast<int>(layout.getProperty("mainInputChannels", 0)));
        const auto outputChannels = juce::jlimit(
            1, 8, static_cast<int>(layout.getProperty("mainOutputChannels", 0)));
        if (static_cast<int>(layout.getProperty("mainInputChannels", 0)) > 0)
            preparation.mainInputChannels = static_cast<std::uint32_t>(inputChannels);
        if (static_cast<int>(layout.getProperty("mainOutputChannels", 0)) > 0)
            preparation.mainOutputChannels = static_cast<std::uint32_t>(outputChannels);
    }
    return preparation;
}
}

int PluginChainCore::appendSandboxedPlugin(const juce::PluginDescription& description,
                                           juce::String& errorMsg,
                                           const PluginSandboxPreparation& preparationIn,
                                           bool publishAndNotify)
{
    if (sampleRate_ <= 0.0 || blockSize_ <= 0)
    {
        errorMsg = "Audio engine is not prepared yet.";
        return -1;
    }

    if (description.fileOrIdentifier.isEmpty()
        || description.pluginFormatName != "VST3")
    {
        errorMsg = "Sandboxed slots currently accept exact VST3 components only.";
        return -1;
    }

    if (description.isInstrument)
    {
        errorMsg = "Sandboxed instrument/MIDI plugins are not supported yet; choose InProcess explicitly.";
        return -1;
    }

    PluginSandboxPreparation preparation = preparationIn;
    preparation.sampleRate = sampleRate_;
    // The chain's prepared block size is the MAXIMUM host callback size. The
    // sandbox quantum Q (blockSamples) keeps the caller-provided value; a
    // larger host callback is reblocked into exact Q-sample worker quanta.
    if (preparation.maximumHostBlockSamples == 0)
        preparation.maximumHostBlockSamples = static_cast<std::uint32_t>(blockSize_);
    if (preparation.mainInputChannels == 0)
        preparation.mainInputChannels = 2;
    if (preparation.mainOutputChannels == 0)
        preparation.mainOutputChannels = 2;

    auto proxy = std::make_unique<SandboxedPluginProxyCore>(description);

    // Phase E2B: canonical APEX automation delivery for sandboxed slots uses
    // the frozen E2A sidecar transport. E2A is opt-in and must be enabled
    // BEFORE the proxy's first prepare (frozen E2A contract). The default
    // stays disabled for every other E2A consumer.
    {
        juce::String automationEnableError;
        proxy->enableAutomationTransport(automationEnableError);
    }

    const auto result = proxy->prepare(preparation);
    if (result != SandboxedPluginProxyCore::PrepareResult::Prepared)
    {
        errorMsg = proxy->processDiagnostics().error.isNotEmpty()
            ? proxy->processDiagnostics().error
            : juce::String("Sandboxed plugin failed to prepare (result=")
                + juce::String(static_cast<int>(result)) + ")";
        PluginScanAuditLogCore::appendLine(
            "plugin_ui_flow.log",
            "PluginChainCore::appendSandboxedPlugin failure"
                " plugin=\"" + description.name + "\""
                " error=\"" + errorMsg + "\"");
        return -1;
    }

    auto wrapped = makeSharedSandboxedPlugin(std::move(proxy));

    // Mirror the in-process appendPlugin flow: prepare the slot through the
    // chain's normal lifecycle so the published snapshot observes a prepared,
    // processable slot. The proxy is already prepared; prepareSandboxed skips
    // the worker restart for an identical rate/block configuration.
    wrapped->prepare(sampleRate_, blockSize_, getEnabledAuxInputBusesForSlot((int) slots_.size()));
    if (! wrapped->isPrepared())
    {
        errorMsg = "Sandboxed plugin failed during chain prepare and was not inserted.";
        return -1;
    }

    // Sidechain and parent-side editor features remain unsupported for this
    // sandbox path. E2B automation context is configured after insertion below.
    // The proxy is the sole bypass authority, so this slot publishes a null
    // chain bypass core.
    const int slotIndex = static_cast<int>(slots_.size());
    slots_.push_back(std::move(wrapped));
    bypassCores_.push_back(nullptr);
    prepareSidechainScratchCapacity(blockSize_);

    // The sandbox worker is already prepared at this point, so configure the
    // slot only now: this builds the E2B ordinal bindings and seeds them from
    // the actual worker values. configureSlotAutomation intentionally returns
    // after the slot context for sandboxed slots because no parent-side
    // AudioProcessor exists to register with the APEX parameter registry.
    configureSlotAutomation(slots_[static_cast<std::size_t>(slotIndex)].get(),
                            slotIndex);

    if (publishAndNotify)
    {
        publishSnapshot();
        notifyChanged();
    }
    PluginScanAuditLogCore::appendLine(
        "plugin_ui_flow.log",
        "PluginChainCore::appendSandboxedPlugin success"
            " plugin=\"" + description.name + "\""
            + " slotIndex=" + juce::String((int) slots_.size() - 1)
            + " slotCount=" + juce::String((int) slots_.size()));
    return (int) slots_.size() - 1;
}

bool PluginChainCore::loadSandboxedPlugin(int slotIndex,
                                          const juce::PluginDescription& description,
                                          const PluginInsertOptions& options,
                                          juce::String& errorMsg)
{
    if (slotIndex < 0)
    {
        errorMsg = "Invalid plugin slot index for sandbox replacement.";
        return false;
    }

    if (! getEnabledAuxInputBusesForSlot(slotIndex).isEmpty())
    {
        errorMsg = "Sandboxed sidechain plugins are not supported while an active sidechain is assigned.";
        return false;
    }

    // Build the replacement through the existing appendSandboxedPlugin path.
    // It constructs only the parent proxy and starts the worker; it never asks
    // the parent format manager to create an AudioPluginInstance. Publication
    // is suppressed until the candidate occupies the requested slot.
    const int originalSlotCount = static_cast<int>(slots_.size());
    const int candidateIndex = appendSandboxedPlugin(
        description, errorMsg, sandboxPreparationFromInsertOptions(options), false);
    if (candidateIndex < 0)
        return false;

    jassert(candidateIndex == originalSlotCount);
    if (slots_.empty() || bypassCores_.empty())
    {
        errorMsg = "Sandbox replacement candidate was not retained by the chain.";
        return false;
    }

    // Detach the temporary end-slot bookkeeping before rebinding the same
    // PluginInstanceCore to its requested stable slot index.  The sandbox
    // worker/proxy remains the same object throughout this move.
    removeSlotAutomation(candidateIndex);
    auto candidate = std::move(slots_.back());
    slots_.pop_back();
    bypassCores_.pop_back();

    if (slotIndex < static_cast<int>(slots_.size()))
    {
        if (slots_[static_cast<std::size_t>(slotIndex)] != nullptr)
        {
            removeSlotAutomation(slotIndex);
            slots_[static_cast<std::size_t>(slotIndex)]->closeEditor();
            retiredSlots_.push_back(std::move(slots_[static_cast<std::size_t>(slotIndex)]));
            retireBypassCore(std::move(bypassCores_[static_cast<std::size_t>(slotIndex)]));
        }

        slots_[static_cast<std::size_t>(slotIndex)] = std::move(candidate);
        bypassCores_[static_cast<std::size_t>(slotIndex)] = nullptr;
    }
    else
    {
        while (static_cast<int>(slots_.size()) < slotIndex)
        {
            slots_.push_back(nullptr);
            auto bypassCore = std::make_unique<BypassCrossfadeCore>();
            bypassCore->prepare(sampleRate_);
            bypassCores_.push_back(std::move(bypassCore));
        }
        slots_.push_back(std::move(candidate));
        bypassCores_.push_back(nullptr);
    }

    configureSlotAutomation(slots_[static_cast<std::size_t>(slotIndex)].get(), slotIndex);
    prepareSidechainScratchCapacity(blockSize_);
    refreshAutomationContexts();

    if (options.publishAndNotify)
    {
        publishSnapshot();
        notifyChanged();
    }

    PluginScanAuditLogCore::appendLine(
        "plugin_ui_flow.log",
        "PluginChainCore::loadSandboxedPlugin success"
            " slotIndex=" + juce::String(slotIndex)
            + " plugin=\"" + description.name + "\""
            + " slotCount=" + juce::String(static_cast<int>(slots_.size())));
    return true;
}

std::shared_ptr<PluginInstanceCore> PluginChainCore::makeSharedSandboxedPlugin(
    std::unique_ptr<SandboxedPluginProxyCore> proxy)
{
    return std::shared_ptr<PluginInstanceCore>(
        new PluginInstanceCore(std::move(proxy)),
        [](PluginInstanceCore* p)
        {
            auto* mm = juce::MessageManager::getInstanceWithoutCreating();
            if (mm != nullptr && mm->isThisTheMessageThread())
            {
                delete p;
            }
            else if (mm != nullptr && retirePluginForMessageThreadDeletion(p))
            {
                // Queued — drainRetiredPlugins() deletes on the message thread.
            }
            else
            {
                // Static teardown or retire queue exhausted: leak with a
                // diagnostic instead of risking realtime destruction.
                jassertfalse;
            }
        });
}

void PluginChainCore::pollSandboxHealth()
{
    for (auto& slot : slots_)
    {
        // Keep the owning instance alive for the entire control-plane
        // recovery-completion handshake. The proxy retains no raw callback
        // into PluginInstanceCore, so chain removal cannot leave a dangling
        // recovery target.
        auto slotOwner = slot;
        if (slotOwner == nullptr || ! slotOwner->isSandboxed())
            continue;
        if (auto* proxy = slotOwner->getSandboxProxy())
        {
            proxy->pollHealthAndRecover();
            if (proxy->recoveryCompletionPending())
                slotOwner->completeSandboxRecovery();
        }
    }
}

} // namespace DAW
