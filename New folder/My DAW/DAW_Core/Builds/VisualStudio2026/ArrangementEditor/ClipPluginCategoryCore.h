// ===========================================================================
// ClipPluginCategoryCore.h
// Defines plugin categories relevant to clip-level processing.
// Extend this as new plugin types are supported by the engine.
// ===========================================================================
#pragma once
#include <string>
#include <vector>

namespace ClipPlugins
{
    enum class PluginCategory
    {
        PitchCorrection,     // Melodyne, Auto-Tune, ReaTune
        PitchShift,          // Waves Tune, Pitch Monster, manual pitch only
        TimeStretch,         // Elastique, zplane, Rubber Band as plugin
        SpectralEditor,      // iZotope RX, SpectraLayers
        VoiceProcessing,     // Nectar, VocALign, Revoice Pro
        HarmonicExciter,     // plugins that reshape harmonics
        Unknown              // detected but category unresolved
    };

    inline std::string categoryDisplayName(PluginCategory cat)
    {
        switch (cat)
        {
        case PluginCategory::PitchCorrection:  return "Pitch Correction";
        case PluginCategory::PitchShift:       return "Pitch Shift";
        case PluginCategory::TimeStretch:      return "Time Stretch";
        case PluginCategory::SpectralEditor:   return "Spectral Editor";
        case PluginCategory::VoiceProcessing:  return "Voice Processing";
        case PluginCategory::HarmonicExciter:  return "Harmonic / Exciter";
        default:                               return "Other";
        }
    }

    struct PluginEntry
    {
        std::string   searchKey;      // lowercase substring for cache scan
        std::string   displayName;    // shown in the UI
        std::string   vendor;
        PluginCategory category;
        bool          supportsARA2;   // Melodyne, SpectraLayers, etc.
        bool          supportsClipFX; // can be applied clip-level as insert
        std::string   homepage;       // for the info button
    };

    inline const std::vector<PluginEntry>& getRegistry()
    {
        static std::vector<PluginEntry> reg = {
        // ===================================================================
        // ARA2 PLUGINS (Surgical Clip Editing - Only Affect Selected Clip)
        // ===================================================================

        // Celemony Melodyne
        { "melodyne",                 "Melodyne 5 Studio",           "Celemony", PluginCategory::PitchCorrection,  true,  false, "https://www.celemony.com" },
        { "melodyne essential",       "Melodyne 5 Essential",        "Celemony", PluginCategory::PitchCorrection,  true,  false, "https://www.celemony.com" },
        { "melodyne assistant",       "Melodyne 5 Assistant",        "Celemony", PluginCategory::PitchCorrection,  true,  false, "https://www.celemony.com" },
        { "melodyne editor",          "Melodyne 5 Editor",           "Celemony", PluginCategory::PitchCorrection,  true,  false, "https://www.celemony.com" },

        // Steinberg SpectraLayers
        { "spectralayers",            "SpectraLayers Pro",           "Steinberg", PluginCategory::SpectralEditor,  true,  false, "https://www.steinberg.net" },
        { "spectralayers one",        "SpectraLayers One",           "Steinberg", PluginCategory::SpectralEditor,  true,  false, "https://www.steinberg.net" },
        { "spectralayers elements",   "SpectraLayers Elements",      "Steinberg", PluginCategory::SpectralEditor,  true,  false, "https://www.steinberg.net" },

        // Synchro Arts (Time & Pitch)
        { "revoice pro",              "Revoice Pro 5",               "Synchro Arts", PluginCategory::VoiceProcessing, true, false, "https://www.synchroarts.com" },
        { "revoice",                  "Revoice Pro 4",               "Synchro Arts", PluginCategory::VoiceProcessing, true, false, "https://www.synchroarts.com" },
        { "vocalign project",         "VocALign Project 5",          "Synchro Arts", PluginCategory::VoiceProcessing, true, false, "https://www.synchroarts.com" },
        { "vocalign ultra",           "VocALign Ultra",              "Synchro Arts", PluginCategory::VoiceProcessing, true, false, "https://www.synchroarts.com" },
        { "vocalign",                 "VocALign Pro 4",              "Synchro Arts", PluginCategory::VoiceProcessing, true, false, "https://www.synchroarts.com" },
        { "repitch",                  "RePitch",                     "Synchro Arts", PluginCategory::PitchCorrection, true, false, "https://www.synchroarts.com" },

        // PreSonus Studio One ARA2
        { "ampire",                   "Ampire (ARA2)",               "PreSonus",  PluginCategory::HarmonicExciter,  true, false, "https://www.presonus.com" },
        { "chord track",              "Chord Track (ARA2)",          "PreSonus",  PluginCategory::PitchCorrection,  true, false, "https://www.presonus.com" },

        // iZotope RX (ARA2 mode available in some versions)
        { "rx 10",                    "iZotope RX 10 (ARA2)",        "iZotope",   PluginCategory::SpectralEditor,   true, true,  "https://www.izotope.com" },
        { "rx 9 advanced",            "iZotope RX 9 Advanced",       "iZotope",   PluginCategory::SpectralEditor,   true, true,  "https://www.izotope.com" },

        // ===================================================================
        // STANDARD CLIP FX (Insert Mode - Process Entire Clip)
        // ===================================================================

        // Antares Auto-Tune
        { "auto-tune pro",            "Auto-Tune Pro",               "Antares",   PluginCategory::PitchCorrection,  false, true,  "https://www.antarestech.com" },
        { "auto-tune artist",         "Auto-Tune Artist",            "Antares",   PluginCategory::PitchCorrection,  false, true,  "https://www.antarestech.com" },
        { "auto-tune access",         "Auto-Tune Access",            "Antares",   PluginCategory::PitchCorrection,  false, true,  "https://www.antarestech.com" },
        { "auto-tune efx",            "Auto-Tune EFX+",              "Antares",   PluginCategory::PitchCorrection,  false, true,  "https://www.antarestech.com" },
        { "auto-tune unlimited",      "Auto-Tune Unlimited",         "Antares",   PluginCategory::PitchCorrection,  false, true,  "https://www.antarestech.com" },

        // Waves
        { "waves tune",               "Waves Tune",                  "Waves",     PluginCategory::PitchShift,       false, true,  "https://www.waves.com" },
        { "waves tune real-time",     "Waves Tune Real-Time",        "Waves",     PluginCategory::PitchCorrection,  false, true,  "https://www.waves.com" },
        { "waves tune lt",            "Waves Tune LT",               "Waves",     PluginCategory::PitchShift,       false, true,  "https://www.waves.com" },

        // Devious Machines
        { "pitch monster",            "Pitch Monster",               "Devious Machines", PluginCategory::PitchShift, false, true,  "https://deviousmachines.com" },

        // iZotope
        { "nectar",                   "Nectar 4",                    "iZotope",   PluginCategory::VoiceProcessing,  false, true,  "https://www.izotope.com" },
        { "nectar 3",                 "Nectar 3",                    "iZotope",   PluginCategory::VoiceProcessing,  false, true,  "https://www.izotope.com" },
        { "rx ",                      "iZotope RX (Standard)",       "iZotope",   PluginCategory::SpectralEditor,   false, true,  "https://www.izotope.com" },

        // REAPER Built-in
        { "reatune",                  "ReaTune",                     "Cockos",    PluginCategory::PitchCorrection,  false, true,  "" },
        { "reapitch",                 "ReaPitch",                    "Cockos",    PluginCategory::PitchShift,       false, true,  "" },

        // Image-Line
        { "pitcher",                  "Pitcher",                     "Image-Line", PluginCategory::PitchCorrection, false, true,  "https://www.image-line.com" },
        { "newtone",                  "NewTone",                     "Image-Line", PluginCategory::PitchCorrection, false, true,  "https://www.image-line.com" },

        // Other Popular
        { "gsnap",                    "GSnap",                       "GVST",      PluginCategory::PitchCorrection,  false, true,  "" },
        { "autotalent",               "AutoTalent",                  "Tom Baran", PluginCategory::PitchCorrection,  false, true,  "" },
        { "graillon",                 "Graillon 2",                  "Auburn Sounds", PluginCategory::PitchShift,   false, true,  "https://www.auburnsounds.com" },
        };
        return reg;
    }

} // namespace ClipPlugins
