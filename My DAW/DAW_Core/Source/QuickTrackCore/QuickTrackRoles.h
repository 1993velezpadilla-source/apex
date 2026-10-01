// ===========================================================================
// QuickTrackRoles.h
// APEX Quick Track Builder — data-driven role catalog.
//
// A role descriptor contains every piece of information needed to create,
// name, color and route a Quick Track without hard-wiring per-role logic:
//
//   * canonical role ID          — stable persistence key (never a TrackID)
//   * display name              — shown in the builder AND used as the
//                                  track-name base ("Coro / Hook" → track
//                                  "Coro / Hook", "Coro / Hook 2", ...)
//   * category / section id     — builder grouping
//   * kind                      — NormalTrack | Bus | Return
//   * routing target role id    — optional "routes into" bus role id
//   * preferred parent role id  — optional bus-parent role id (e.g. family
//                                  buses prefer to feed the Vocal Bus when
//                                  it exists in the batch/project)
//
// Adding a new role later = adding one descriptor row. No if-chains.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include <vector>

namespace DAW {

enum class QuickTrackKind
{
    NormalTrack, // recording / audio track, default Mono 1 hardware input
    Bus,         // real routing destination (RoutingNodeType::Bus)
    Return       // FX return bus (Reverb / Delay / Parallel Comp)
};

struct QuickTrackRole
{
    const char* roleId;             // stable canonical id, e.g. "coro_hook"
    const char* displayName;        // builder label + track-name base
    const char* section;            // builder section id, e.g. "vocals"
    QuickTrackKind kind;
    const char* routesToRoleId;     // "" = direct to master (or preferred bus parent)
    const char* preferredParentRoleId; // "" = none (feed master); for family buses: "vocal_bus"
};

class QuickTrackRoleCatalog
{
public:
    static const std::vector<QuickTrackRole>& getAll();
    static const QuickTrackRole* findById(const juce::String& roleId);
    static const QuickTrackRole* findByDisplayName(const juce::String& displayName);
    static bool isBusKind(const QuickTrackRole& role) noexcept
    {
        return role.kind == QuickTrackKind::Bus || role.kind == QuickTrackKind::Return;
    }
    /** Section ids in display order. */
    static const std::vector<juce::String>& getSectionOrder();
    /** Human-readable section heading for a section id. */
    static juce::String sectionTitle(const juce::String& sectionId);

private:
    static const std::vector<QuickTrackRole> buildCatalog();
};

// ── Catalog definition (data-driven; add roles here) ─────────────────────────
inline const std::vector<QuickTrackRole>& QuickTrackRoleCatalog::getAll()
{
    static const std::vector<QuickTrackRole> catalog = buildCatalog();
    return catalog;
}

inline const std::vector<QuickTrackRole> QuickTrackRoleCatalog::buildCatalog()
{
    return
    {
        // ── BASIC ────────────────────────────────────────────────────────
        { "untitled",             "Untitled",              "basic",  QuickTrackKind::NormalTrack, "", "" },
        { "instrumental_beat",    "Instrumental / Beat",   "basic",  QuickTrackKind::NormalTrack, "", "" },
        { "reference",            "Reference",             "basic",  QuickTrackKind::NormalTrack, "", "" },
        { "scratch_demo",         "Scratch / Demo",        "basic",  QuickTrackKind::NormalTrack, "", "" },

        // ── SONG PARTS ───────────────────────────────────────────────────
        { "intro",                "Intro",                 "song_parts", QuickTrackKind::NormalTrack, "", "" },
        { "verse",                "Verso / Verse",         "song_parts", QuickTrackKind::NormalTrack, "", "" },
        { "pre_chorus",           "Pre-Coro / Pre-Chorus", "song_parts", QuickTrackKind::NormalTrack, "", "" },
        { "coro_hook",            "Coro / Hook",           "song_parts", QuickTrackKind::NormalTrack, "", "" },
        { "bridge",               "Bridge / Puente",       "song_parts", QuickTrackKind::NormalTrack, "", "" },
        { "outro",                "Outro",                 "song_parts", QuickTrackKind::NormalTrack, "", "" },

        // ── VOCALS ───────────────────────────────────────────────────────
        { "lead_vocal",           "Lead Vocal",            "vocals", QuickTrackKind::NormalTrack, "vocal_bus", "" },
        { "doubles",              "Doubles",               "vocals", QuickTrackKind::NormalTrack, "doubles_bus", "" },
        { "adlibs",               "Adlibs / Highlights",   "vocals", QuickTrackKind::NormalTrack, "adlibs_bus", "" },
        { "harmonies",            "Harmonies",             "vocals", QuickTrackKind::NormalTrack, "harmonies_bus", "" },
        { "bg_vocals",            "Background Vocals",     "vocals", QuickTrackKind::NormalTrack, "bg_vocals_bus", "" },
        { "gang_vocals",          "Gang Vocals",           "vocals", QuickTrackKind::NormalTrack, "", "" },
        { "whisper",              "Whisper",               "vocals", QuickTrackKind::NormalTrack, "", "" },

        // ── VOCAL BUSES ──────────────────────────────────────────────────
        { "vocal_bus",            "Vocal Bus",             "vocal_buses", QuickTrackKind::Bus, "", "" },
        { "doubles_bus",          "Doubles Bus",           "vocal_buses", QuickTrackKind::Bus, "", "vocal_bus" },
        { "adlibs_bus",           "Adlibs Bus",            "vocal_buses", QuickTrackKind::Bus, "", "vocal_bus" },
        { "harmonies_bus",        "Harmonies Bus",         "vocal_buses", QuickTrackKind::Bus, "", "vocal_bus" },
        { "bg_vocals_bus",        "Background Vocals Bus", "vocal_buses", QuickTrackKind::Bus, "", "vocal_bus" },

        // ── OTHER BUSES ──────────────────────────────────────────────────
        { "instrumental_bus",     "Instrumental Bus",      "other_buses", QuickTrackKind::Bus, "", "" },
        { "drum_bus",             "Drum Bus",              "other_buses", QuickTrackKind::Bus, "", "" },
        { "fx_bus",               "FX Bus",                "other_buses", QuickTrackKind::Bus, "", "" },

        // ── FX / RETURNS ─────────────────────────────────────────────────
        { "reverb",               "Reverb",                "fx_returns", QuickTrackKind::Return, "", "" },
        { "delay",                "Delay",                 "fx_returns", QuickTrackKind::Return, "", "" },
        { "parallel_comp",        "Parallel Comp",         "fx_returns", QuickTrackKind::Return, "", "" }
    };
}

inline const QuickTrackRole* QuickTrackRoleCatalog::findById(const juce::String& roleId)
{
    for (const auto& role : getAll())
        if (roleId == role.roleId)
            return &role;
    return nullptr;
}

inline const QuickTrackRole* QuickTrackRoleCatalog::findByDisplayName(const juce::String& displayName)
{
    for (const auto& role : getAll())
        if (displayName == role.displayName)
            return &role;
    return nullptr;
}

inline const std::vector<juce::String>& QuickTrackRoleCatalog::getSectionOrder()
{
    static const std::vector<juce::String> order =
    {
        "basic", "song_parts", "vocals", "vocal_buses", "other_buses", "fx_returns"
    };
    return order;
}

inline juce::String QuickTrackRoleCatalog::sectionTitle(const juce::String& sectionId)
{
    if (sectionId == "basic")        return "BASIC";
    if (sectionId == "song_parts")   return "SONG PARTS";
    if (sectionId == "vocals")       return "VOCALS";
    if (sectionId == "vocal_buses")  return "VOCAL BUSES";
    if (sectionId == "other_buses")  return "OTHER BUSES";
    if (sectionId == "fx_returns")   return "FX / RETURNS";
    return sectionId;
}

} // namespace DAW
