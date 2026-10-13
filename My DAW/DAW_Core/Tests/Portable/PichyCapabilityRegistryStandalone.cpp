// Standalone contract test: no JUCE, audio hardware, model, network, or API key.
// g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic PichyCapabilityRegistryStandalone.cpp -o test && ./test
#include "../../Source/PichyCore/PichyCapabilityRegistryCore.h"
#include <cassert>
#include <limits>
#include <string>
#include <utility>

using apex::pichy::PichyCapabilityRegistryCore;
using apex::pichy::PichyCapabilitySnapshot;
using apex::pichy::PichyParameterCapability;
using apex::pichy::PichyPluginCapability;
using apex::pichy::PichySnapshotStatus;
using apex::pichy::PichyTrackCapability;

static PichyCapabilitySnapshot makeSession()
{
    PichyCapabilitySnapshot world;
    world.captureId = 73;

    PichyTrackCapability lead;
    lead.trackId = "track-vocal-uuid";
    lead.displayName = "Vocal Principal";
    lead.armed = true;
    lead.monitoring = true;
    lead.inputTrimDb = -6.0f;

    PichyPluginCapability native;
    native.instanceId = "native-plugin-instance-uuid";
    native.displayName = "APEX EQ";
    native.formatName = "APEX Native";
    native.pluginIdentifier = "apex.native.eq";
    native.slotIndex = 3;
    native.processorAccessible = true;

    PichyParameterCapability gain;
    gain.parameterId = "band0.gain";
    gain.name = "Band 1 Gain";
    gain.label = "dB";
    gain.normalizedValue = 0.43f;
    gain.automatable = true;
    gain.stableId = true;
    native.parameters.push_back(gain);
    lead.plugins.push_back(native);

    PichyPluginCapability thirdParty;
    thirdParty.instanceId = "vst-instance-uuid";
    thirdParty.displayName = "External VST3";
    thirdParty.formatName = "VST3";
    thirdParty.pluginIdentifier = "vst3.external.example";
    thirdParty.slotIndex = 5;
    thirdParty.processorAccessible = true;
    PichyParameterCapability legacy;
    legacy.parameterId = "param_23";
    legacy.name = "Host Index Only";
    legacy.normalizedValue = 0.75f;
    legacy.automatable = true;
    legacy.stableId = false; // Can inspect; cannot auto-write by default.
    thirdParty.parameters.push_back(legacy);
    lead.plugins.push_back(thirdParty);
    world.tracks.push_back(lead);

    PichyTrackCapability bus;
    bus.trackId = "bus-1";
    bus.displayName = "Vocal Bus";
    bus.inputTrimDb = 0.0f;
    world.tracks.push_back(bus);

    apex::pichy::PichyAvailablePluginCapability available;
    available.pluginIdentifier = "apex.native.eq";
    available.displayName = "APEX EQ";
    available.formatName = "APEX Native";
    available.manufacturer = "APEX";
    available.uniqueId = 155;
    world.availablePlugins.push_back(available);
    available.pluginIdentifier = "vst3.external.example";
    available.displayName = "External VST3";
    available.formatName = "VST3";
    available.uniqueId = 711;
    world.availablePlugins.push_back(available);
    return world;
}

int main()
{
    using Status = PichySnapshotStatus;
    using Core = PichyCapabilityRegistryCore;
    auto session = makeSession();
    assert(Core::validate(session) == Status::Ok);
    assert(session.captureId == 73);
    assert(session.availablePlugins.size() == 2);
    assert(session.availablePlugins[0].formatName == "APEX Native");
    assert(session.availablePlugins[1].formatName == "VST3");
    const auto* eq = Core::findPlugin(session, "track-vocal-uuid", "native-plugin-instance-uuid");
    assert(eq && eq->slotIndex == 3);
    const auto* gain = Core::findParameter(session, "track-vocal-uuid",
                                          "native-plugin-instance-uuid", "band0.gain");
    assert(gain && Core::wouldPermitFutureWrite(*gain));
    const auto* legacy = Core::findParameter(session, "track-vocal-uuid",
                                            "vst-instance-uuid", "param_23");
    assert(legacy && !Core::wouldPermitFutureWrite(*legacy));
    assert(Core::findParameter(session, "bus-1", "vst-instance-uuid", "param_23") == nullptr);
    assert(Core::findParameter(session, "track-vocal-uuid",
                               "vst-instance-uuid", "missing") == nullptr);

    // Reordering a slot does not change the plugin's persistent identity.
    session.tracks[0].plugins[0].slotIndex = 77;
    assert(Core::validate(session) == Status::Ok);
    assert(Core::findParameter(session, "track-vocal-uuid",
                               "native-plugin-instance-uuid", "band0.gain") != nullptr);

    auto bad = makeSession();
    bad.availablePlugins.push_back(bad.availablePlugins.front());
    assert(Core::validate(bad) == Status::DuplicateIdentity);

    bad = makeSession();
    bad.availablePlugins.front().formatName = "";
    assert(Core::validate(bad) == Status::InvalidIdentity);

    bad = makeSession();
    bad.availablePlugins.resize(Core::kMaxAvailablePlugins + 1);
    assert(Core::validate(bad) == Status::LimitsExceeded);

    bad = makeSession();
    bad.tracks.push_back(bad.tracks.front());
    assert(Core::validate(bad) == Status::DuplicateIdentity);

    bad = makeSession();
    bad.tracks[1].plugins.push_back(bad.tracks[0].plugins.front());
    assert(Core::validate(bad) == Status::DuplicateIdentity);

    bad = makeSession();
    bad.tracks[0].plugins[1].slotIndex = 3;
    assert(Core::validate(bad) == Status::DuplicateIdentity);

    bad = makeSession();
    bad.tracks[0].plugins[0].parameters.push_back(bad.tracks[0].plugins[0].parameters[0]);
    assert(Core::validate(bad) == Status::DuplicateIdentity);

    bad = makeSession();
    bad.tracks[0].plugins[0].parameters[0].normalizedValue =
        std::numeric_limits<float>::quiet_NaN();
    assert(Core::validate(bad) == Status::InvalidParameter);

    bad = makeSession();
    bad.tracks[0].plugins[0].parameters[0].normalizedValue = 1.01f;
    assert(Core::validate(bad) == Status::InvalidParameter);

    bad = makeSession();
    bad.tracks[0].inputTrimDb = -121.0f;
    assert(Core::validate(bad) == Status::InvalidParameter);

    bad = makeSession();
    bad.tracks[0].plugins[0].instanceId = "bad\nplugin";
    assert(Core::validate(bad) == Status::InvalidIdentity);

    bad = makeSession();
    bad.tracks[0].plugins[0].parameters[0].parameterId.clear();
    assert(Core::validate(bad) == Status::InvalidIdentity);

    bad = makeSession();
    bad.tracks[0].plugins[0].processorAccessible = false;
    assert(Core::validate(bad) == Status::InvalidParameter);

    bad = makeSession();
    bad.tracks[0].plugins[0].parameters.resize(Core::kMaxParamsPerPlugin + 1);
    assert(Core::validate(bad) == Status::LimitsExceeded);

    // A sandbox proxy may be present with NO parent-visible parameters.
    bad = makeSession();
    bad.tracks[0].plugins[0].sandboxed = true;
    bad.tracks[0].plugins[0].processorAccessible = false;
    bad.tracks[0].plugins[0].parameters.clear();
    assert(Core::validate(bad) == Status::Ok);

    PichyParameterCapability blocked;
    blocked.parameterId = "cutoff";
    blocked.automatable = false;
    blocked.stableId = true;
    assert(!Core::wouldPermitFutureWrite(blocked));
    blocked.automatable = true;
    blocked.normalizedValue = 0.5f;
    assert(Core::wouldPermitFutureWrite(blocked));
    blocked.normalizedValue = -0.1f;
    assert(!Core::wouldPermitFutureWrite(blocked));
}
