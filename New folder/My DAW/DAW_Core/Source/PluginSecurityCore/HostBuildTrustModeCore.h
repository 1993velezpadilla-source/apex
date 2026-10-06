#pragma once
#include <JuceHeader.h>

namespace DAW {

enum class HostBuildTrustMode
{
    DebugUnsignedLocal,
    ReleaseUnsigned,
    ReleaseSignedOrTrusted,
    InstalledTrustedBuild
};

class HostBuildTrustModeCore
{
public:
    static HostBuildTrustMode detectCurrentMode()
    {
        auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        auto path = exe.getFullPathName().toLowerCase();
        auto isProgramFiles = path.contains("\\program files\\");
        auto isDownloads = path.contains("\\downloads\\");
#if JUCE_DEBUG
        if (isDownloads || path.contains("\\debug\\"))
            return HostBuildTrustMode::DebugUnsignedLocal;
        return HostBuildTrustMode::ReleaseUnsigned;
#else
        if (isProgramFiles)
            return HostBuildTrustMode::InstalledTrustedBuild;
        return HostBuildTrustMode::ReleaseSignedOrTrusted;
#endif
    }

    static juce::String toString(HostBuildTrustMode mode)
    {
        switch (mode)
        {
            case HostBuildTrustMode::DebugUnsignedLocal: return "debug_unsigned_local";
            case HostBuildTrustMode::ReleaseUnsigned: return "release_unsigned";
            case HostBuildTrustMode::ReleaseSignedOrTrusted: return "release_signed_or_trusted";
            case HostBuildTrustMode::InstalledTrustedBuild: return "installed_trusted_build";
        }
        return "unknown";
    }
};

} // namespace DAW
