// JUCE-independent, C++17 model-agnostic Pichy action safety gauntlet.
// g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic PichyActionPreflightStandalone.cpp -o pichy_test && ./pichy_test
#include "../../Source/PichyCore/PichyActionPreflightCore.h"
#include <cassert>
#include <limits>

using namespace apex::pichy;

static PichyCapabilitySnapshot world()
{
    PichyCapabilitySnapshot state;
    state.captureId = 19;
    PichyTrackCapability track;
    track.trackId = "lead-uuid";
    PichyPluginCapability eq;
    eq.instanceId = "plugin-uuid";
    eq.pluginIdentifier = "apex.parametric";
    eq.processorAccessible = true;
    eq.slotIndex = 2;
    PichyParameterCapability freq;
    freq.parameterId = "band0.frequency";
    freq.name = "Frequency";
    freq.stableId = true;
    freq.automatable = true;
    freq.normalizedValue = 0.3f;
    eq.parameters.push_back(freq);
    track.plugins.push_back(eq);
    state.tracks.push_back(track);
    return state;
}
static PichyParameterChangeIntent intent()
{
    PichyParameterChangeIntent in;
    in.trackId = "lead-uuid";
    in.pluginInstanceId = "plugin-uuid";
    in.parameterId = "band0.frequency";
    in.expectedNormalized = 0.3f;
    in.desiredNormalized = 0.5f;
    in.observedCaptureId = 19;
    in.source = PichyActionSource::Voice;
    return in;
}
int main()
{
    using Status = PichyActionPreflightStatus;
    auto s = world();
    auto req = intent();
    auto out = PichyActionPreflightCore::evaluate(s, req);
    assert(out.status == Status::RequiresUserApproval);
    assert(!out.maySubmitToExecutor());
    assert(!PichyActionPreflightCore::canRevalidateForExecution(out, s));

    req.authorization = PichyActionAuthorization::ExplicitUserApproval;
    out = PichyActionPreflightCore::evaluate(s, req);
    assert(out.status == Status::ReadyForTrustedExecutor);
    assert(out.maySubmitToExecutor());
    assert(PichyActionPreflightCore::canRevalidateForExecution(out, s));

    // Reordering does NOT cause stale target confusion.
    s.tracks[0].plugins[0].slotIndex = 17;
    assert(PichyActionPreflightCore::canRevalidateForExecution(out, s));

    // Plugin removal is a safe no-op rather than touching the new slot owner.
    s.tracks[0].plugins.clear();
    assert(!PichyActionPreflightCore::canRevalidateForExecution(out, s));
    s = world();

    // Detect concurrent/manual/automation edits between preview and execution.
    s.tracks[0].plugins[0].parameters[0].normalizedValue = 0.45f;
    assert(!PichyActionPreflightCore::canRevalidateForExecution(out, s));
    assert(PichyActionPreflightCore::evaluate(s, req).status == Status::ChangedSincePreview);
    s = world();

    req.authorization = PichyActionAuthorization::PreviewOnly;
    assert(PichyActionPreflightCore::evaluate(s, req).status == Status::RequiresUserApproval);

    req = intent();
    req.observedCaptureId = 100;
    assert(PichyActionPreflightCore::evaluate(s, req).status == Status::StaleObservation);
    req = intent();
    req.parameterId = "no-such-param";
    assert(PichyActionPreflightCore::evaluate(s, req).status == Status::UnknownTarget);
    req = intent();
    req.pluginInstanceId = "no-such-instance";
    assert(PichyActionPreflightCore::evaluate(s, req).status == Status::UnknownTarget);

    req = intent();
    req.desiredNormalized = std::numeric_limits<float>::quiet_NaN();
    assert(PichyActionPreflightCore::evaluate(s, req).status == Status::InvalidRequest);
    req = intent();
    req.desiredNormalized = 5.0f;
    assert(PichyActionPreflightCore::evaluate(s, req).status == Status::InvalidRequest);
    req = intent();
    req.desiredNormalized = 0.3f;
    assert(PichyActionPreflightCore::evaluate(s, req).status == Status::NoChangeRequested);

    s.tracks[0].plugins[0].parameters[0].stableId = false;
    req = intent();
    assert(PichyActionPreflightCore::evaluate(s, req).status == Status::UnstableParameterIdentity);
    s = world();
    s.tracks[0].plugins[0].parameters[0].automatable = false;
    assert(PichyActionPreflightCore::evaluate(s, req).status == Status::NotAutomatable);
    s = world();
    s.tracks[0].plugins[0].processorAccessible = false;
    s.tracks[0].plugins[0].parameters.clear();
    assert(PichyActionPreflightCore::evaluate(s, req).status == Status::UnavailableProcessor);

    s = world();
    s.tracks[0].plugins.push_back(s.tracks[0].plugins[0]);
    assert(PichyActionPreflightCore::evaluate(s, req).status == Status::InvalidSnapshot);

    // Proactivity NEVER supplies authorization.
    s = world();
    req = intent();
    req.source = PichyActionSource::Proactive;
    assert(PichyActionPreflightCore::evaluate(s, req).status == Status::RequiresUserApproval);
}
