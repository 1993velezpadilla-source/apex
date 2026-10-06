#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * ModifierTranslator — converts shortcut modifier text between Windows and Apple styles.
 *
 * Windows style: Ctrl, Alt, Shift, Win
 * Apple style:   Command (⌘), Option (⌥), Shift, Control (⌃)
 *
 * Mapping:
 *   Ctrl    ↔  Command (⌘)
 *   Alt     ↔  Option (⌥)
 *   Win     ↔  Control (⌃)   [rare, for completeness]
 *   Cmd     ↔  Ctrl          [Logic-style entries stored with Cmd]
 *   Option  ↔  Alt           [Logic-style entries stored with Option]
 */
class ModifierTranslator
{
public:
    enum class PlatformStyle { Windows, Apple };

    /** Translate a shortcut display string to the target platform style. */
    static juce::String translate(const juce::String& shortcut, PlatformStyle target)
    {
        if (shortcut.isEmpty()) return shortcut;

        juce::String result = shortcut;

        if (target == PlatformStyle::Apple)
        {
            // Windows → Apple
            result = result.replace("Ctrl+",    juce::CharPointer_UTF8("\xe2\x8c\x98"))
                           .replace("Alt+",     juce::CharPointer_UTF8("\xe2\x8c\xa5"))
                           .replace("Win+",     juce::CharPointer_UTF8("\xe2\x8c\x83"))
                           .replace("Shift+",   juce::CharPointer_UTF8("\xe2\x87\xa7"));
            // Already-Apple tokens: keep as-is (Cmd -> Command, Option -> Option)
            result = result.replace("Cmd+",     juce::CharPointer_UTF8("\xe2\x8c\x98"))
                           .replace("Option+",  juce::CharPointer_UTF8("\xe2\x8c\xa5"));
            // Clean up "Delete" for Apple feel
            result = result.replace("Delete",   juce::CharPointer_UTF8("\xE2\x8C\xAB"));
            result = result.replace("Return",   juce::CharPointer_UTF8("\xe2\x86\xa9"));
        }
        else // Windows
        {
            // Apple → Windows
            result = result.replace(juce::CharPointer_UTF8("\xe2\x8c\x98"),        "Ctrl+")
                           .replace(juce::CharPointer_UTF8("\xe2\x8c\xa5"),        "Alt+")
                           .replace(juce::CharPointer_UTF8("\xe2\x8c\x83"),        "Win+")
                           .replace(juce::CharPointer_UTF8("\xe2\x87\xa7"),        "Shift+")
                           .replace(juce::CharPointer_UTF8("\xE2\x8C\xAB"),        "Delete")
                           .replace(juce::CharPointer_UTF8("\xe2\x86\xa9"),        "Enter");
            result = result.replace("Cmd+",     "Ctrl+")
                           .replace("Command+", "Ctrl+")
                           .replace("Option+",  "Alt+");
        }

        return result;
    }

    /** Batch-translate all shortcut strings in a list of entries. */
    static std::vector<std::pair<juce::String, juce::String>>
    translateEntries(const std::vector<std::pair<juce::String, juce::String>>& entries,
                     PlatformStyle target)
    {
        std::vector<std::pair<juce::String, juce::String>> out;
        out.reserve(entries.size());
        for (auto& [cmd, key] : entries)
            out.push_back({ cmd, translate(key, target) });
        return out;
    }
};

} // namespace DAW
