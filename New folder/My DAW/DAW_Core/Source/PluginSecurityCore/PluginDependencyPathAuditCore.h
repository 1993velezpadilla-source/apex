#pragma once
#include <JuceHeader.h>

namespace DAW {

class PluginDependencyPathAuditCore
{
public:
    static bool isLikelyTempPath(const juce::String& pathOrText)
    {
        auto s = pathOrText.toLowerCase();
        return s.contains("\\appdata\\local\\temp\\")
            || s.contains("/appdata/local/temp/")
            || s.contains("\\temp\\")
            || s.contains("/temp/");
    }

    static juce::String extractBlockedDllCandidate(const juce::String& text)
    {
        auto tokens = juce::StringArray::fromTokens(text, " \n\r\t\"'()[]", "");
        for (auto& token : tokens)
        {
            auto lower = token.toLowerCase();
            if (lower.contains(".dll") || lower.contains(".vst3"))
                return token;
        }
        return {};
    }
};

} // namespace DAW
