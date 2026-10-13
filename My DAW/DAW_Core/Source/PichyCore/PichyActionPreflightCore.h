#pragma once
#include "PichyCapabilityRegistryCore.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace apex::pichy {

/**
 * PichyActionPreflightCore is a deterministic, model-agnostic safety barrier.
 * It DOES NOT modify APEX state. It verifies that a natural-language request
 * was resolved to an existing, stable, host-exposed, automatable parameter.
 *
 * ExplicitAuthorization is obtained from the APP'S trusted user-interaction
 * layer (voice confirmation/button), NEVER directly from an LLM output.
 */
enum class PichyActionAuthorization : std::uint8_t
{
    None = 0,
    PreviewOnly,
    ExplicitUserApproval
};

enum class PichyActionSource : std::uint8_t
{
    Text = 0,
    Voice,
    Proactive
};

struct PichyParameterChangeIntent
{
    std::string trackId;
    std::string pluginInstanceId;
    std::string parameterId;
    float expectedNormalized = 0.0f;
    float desiredNormalized = 0.0f;
    std::uint64_t observedCaptureId = 0; // id of the exact snapshot reviewed
    PichyActionSource source = PichyActionSource::Text;
    PichyActionAuthorization authorization = PichyActionAuthorization::None;
};

enum class PichyActionPreflightStatus : std::uint8_t
{
    ReadyForTrustedExecutor, // approved plan, still MUST recheck live host state
    RequiresUserApproval,
    InvalidSnapshot,
    StaleObservation,
    InvalidRequest,
    UnknownTarget,
    UnstableParameterIdentity,
    NotAutomatable,
    UnavailableProcessor,
    ChangedSincePreview,
    NoChangeRequested
};

struct PichyActionPreflightResult
{
    PichyActionPreflightStatus status = PichyActionPreflightStatus::InvalidRequest;
    std::string trackId;
    std::string pluginInstanceId;
    std::string parameterId;
    float verifiedPriorNormalized = 0.0f;
    float desiredNormalized = 0.0f;

    bool maySubmitToExecutor() const noexcept
    {
        return status == PichyActionPreflightStatus::ReadyForTrustedExecutor;
    }
};

class PichyActionPreflightCore final
{
public:
    static constexpr float kValueTolerance = 0.0001f;

    static PichyActionPreflightResult evaluate(
        const PichyCapabilitySnapshot& observed,
        const PichyParameterChangeIntent& intent)
    {
        PichyActionPreflightResult answer;
        // Keep the target visible in a rejected response, but never return
        // raw pointers or mutable references to host objects.
        answer.trackId = intent.trackId;
        answer.pluginInstanceId = intent.pluginInstanceId;
        answer.parameterId = intent.parameterId;
        answer.desiredNormalized = intent.desiredNormalized;

        if (PichyCapabilityRegistryCore::validate(observed) !=
            PichySnapshotStatus::Ok)
        {
            answer.status = PichyActionPreflightStatus::InvalidSnapshot;
            return answer;
        }
        if (intent.observedCaptureId == 0 ||
            intent.observedCaptureId != observed.captureId)
        {
            answer.status = PichyActionPreflightStatus::StaleObservation;
            return answer;
        }
        if (!PichyCapabilityRegistryCore::validIdentity(intent.trackId) ||
            !PichyCapabilityRegistryCore::validIdentity(intent.pluginInstanceId) ||
            !PichyCapabilityRegistryCore::validIdentity(intent.parameterId) ||
            !std::isfinite(intent.expectedNormalized) ||
            !std::isfinite(intent.desiredNormalized) ||
            intent.expectedNormalized < 0.0f || intent.expectedNormalized > 1.0f ||
            intent.desiredNormalized < 0.0f || intent.desiredNormalized > 1.0f)
        {
            answer.status = PichyActionPreflightStatus::InvalidRequest;
            return answer;
        }

        const auto* plugin = PichyCapabilityRegistryCore::findPlugin(
            observed, intent.trackId, intent.pluginInstanceId);
        if (plugin == nullptr)
        {
            answer.status = PichyActionPreflightStatus::UnknownTarget;
            return answer;
        }
        if (!plugin->processorAccessible)
        {
            answer.status = PichyActionPreflightStatus::UnavailableProcessor;
            return answer;
        }
        const auto* parameter = PichyCapabilityRegistryCore::findParameter(
            observed, intent.trackId, intent.pluginInstanceId, intent.parameterId);
        if (parameter == nullptr)
        {
            answer.status = PichyActionPreflightStatus::UnknownTarget;
            return answer;
        }
        if (!parameter->stableId)
        {
            answer.status = PichyActionPreflightStatus::UnstableParameterIdentity;
            return answer;
        }
        if (!parameter->automatable)
        {
            answer.status = PichyActionPreflightStatus::NotAutomatable;
            return answer;
        }
        if (std::abs(parameter->normalizedValue - intent.expectedNormalized) >
            kValueTolerance)
        {
            answer.status = PichyActionPreflightStatus::ChangedSincePreview;
            return answer;
        }

        answer.verifiedPriorNormalized = parameter->normalizedValue;
        if (std::abs(parameter->normalizedValue - intent.desiredNormalized) <
            kValueTolerance)
        {
            answer.status = PichyActionPreflightStatus::NoChangeRequested;
            return answer;
        }
        answer.status =
            intent.authorization == PichyActionAuthorization::ExplicitUserApproval
                ? PichyActionPreflightStatus::ReadyForTrustedExecutor
                : PichyActionPreflightStatus::RequiresUserApproval;
        return answer;
    }

    /**
     * Before executing an approved change, the host must capture fresh
     * metadata and recheck the same instance/parameter/current value on the
     * JUCE message thread. Capture IDs are observation IDs, not revisions,
     * so comparing JUST those IDs does not defend against a changed plugin.
     */
    static bool canRevalidateForExecution(
        const PichyActionPreflightResult& approved,
        const PichyCapabilitySnapshot& live) 
    {
        if (!approved.maySubmitToExecutor() ||
            PichyCapabilityRegistryCore::validate(live) != PichySnapshotStatus::Ok)
            return false;

        const auto* plugin = PichyCapabilityRegistryCore::findPlugin(
            live, approved.trackId, approved.pluginInstanceId);
        if (plugin == nullptr || !plugin->processorAccessible)
            return false;

        const auto* parameter = PichyCapabilityRegistryCore::findParameter(
            live, approved.trackId, approved.pluginInstanceId, approved.parameterId);
        return parameter != nullptr &&
            PichyCapabilityRegistryCore::wouldPermitFutureWrite(*parameter) &&
            std::abs(parameter->normalizedValue -
                     approved.verifiedPriorNormalized) <= kValueTolerance;
    }
};

} // namespace apex::pichy
