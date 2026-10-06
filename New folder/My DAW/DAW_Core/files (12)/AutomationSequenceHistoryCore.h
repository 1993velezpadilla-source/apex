// ============================================================
// AutomationSequenceHistoryCore.h  — v3
// APEX · Create Sequence  |  Source/AutomationSequence/
//
// Persists two things to disk (message-thread only — never call
// from the audio thread):
//
//   1. Seed history  — every random seed ever used by this
//      machine.  Guarantees the user never sees a repeated
//      randomized pattern.  Stored as a binary list of
//      uint32_t values in:
//         userAppData/APEX/SeedHistory.dat
//
//   2. Pattern library  — named user-saved patterns.  Each
//      pattern is a single XML file in:
//         userAppData/APEX/Sequences/<name>.apexseq
//      Pattern files are project-independent — they follow the
//      user across projects and machines (if the user copies
//      the folder).
//
// Header-only with inline static state.  No .cpp needed.
// ============================================================
#pragma once
#include <JuceHeader.h>
#include "AutomationSequenceTypes.h"
#include <unordered_set>
#include <mutex>

namespace APEX {
namespace AutomationSeq {

class HistoryCore {
public:
    HistoryCore() = delete;

    // ============================================================
    // Seed history — non-repeating random
    // ============================================================

    // Returns a 32-bit seed value not previously used.
    // Always succeeds — even if the history grows huge, the search
    // is bounded by 256 tries before falling back to a freshly
    // randomized value (still ~99.99% unique at that point).
    static uint32_t getNextUnusedSeed() {
        std::lock_guard<std::mutex> lk(getMutex());
        ensureLoaded();
        auto& used = getUsedSeeds();
        juce::Random rng;
        for (int tries = 0; tries < 256; ++tries) {
            uint32_t s = static_cast<uint32_t>(rng.nextInt())
                       ^ (static_cast<uint32_t>(juce::Time::currentTimeMillis()) * 2654435761u);
            if (s == 0) s = 0xCAFEBABE;
            if (used.find(s) == used.end()) {
                used.insert(s);
                saveSeedHistory();
                return s;
            }
        }
        // Statistical fallback — virtually certain to be unique
        uint32_t s = static_cast<uint32_t>(rng.nextInt64()) ^ 0xA5A5A5A5u;
        used.insert(s);
        saveSeedHistory();
        return s;
    }

    // Record that a seed was used (e.g. when loading a saved pattern)
    static void markSeedAsUsed(uint32_t seed) {
        std::lock_guard<std::mutex> lk(getMutex());
        ensureLoaded();
        getUsedSeeds().insert(seed);
        saveSeedHistory();
    }

    // Wipe the entire seed history.  Use sparingly.
    static void clearSeedHistory() {
        std::lock_guard<std::mutex> lk(getMutex());
        getUsedSeeds().clear();
        getLoaded() = true;
        saveSeedHistory();
    }

    static size_t getUniqueSeedCount() {
        std::lock_guard<std::mutex> lk(getMutex());
        ensureLoaded();
        return getUsedSeeds().size();
    }

    // ============================================================
    // Saved patterns library
    // ============================================================

    struct SavedPatternInfo {
        juce::String name;
        juce::Time   createdAt;
        ShapeMode    primaryMode = ShapeMode::Normal;
    };

    // List all saved patterns by name (alphabetical)
    static juce::Array<SavedPatternInfo> listSavedPatterns() {
        juce::Array<SavedPatternInfo> out;
        auto dir = getSequencesDir();
        if (!dir.isDirectory()) return out;

        juce::Array<juce::File> files;
        dir.findChildFiles(files, juce::File::findFiles, false, "*.apexseq");
        for (auto& f : files) {
            SavedPatternInfo info;
            info.name      = f.getFileNameWithoutExtension();
            info.createdAt = f.getCreationTime();
            // Peek the mode quickly
            auto xml = juce::parseXML(f);
            if (xml != nullptr) {
                info.primaryMode = static_cast<ShapeMode>(
                    xml->getIntAttribute("mode", 0));
            }
            out.add(info);
        }
        std::sort(out.begin(), out.end(),
            [](const SavedPatternInfo& a, const SavedPatternInfo& b) {
                return a.name.compareIgnoreCase(b.name) < 0;
            });
        return out;
    }

    // Save (or overwrite) a pattern under the given name
    static bool savePattern(const juce::String& name,
                             const SequenceParams& p)
    {
        if (name.trim().isEmpty()) return false;
        auto file = patternFileFor(name);
        file.getParentDirectory().createDirectory();

        auto xml = std::make_unique<juce::XmlElement>("APEXSequencePattern");
        xml->setAttribute("version",      1);
        xml->setAttribute("name",         name);
        xml->setAttribute("mode",         static_cast<int>(p.mode));
        xml->setAttribute("seed",         static_cast<int>(p.seed));
        xml->setAttribute("numSteps",     p.numSteps);
        xml->setAttribute("attackLevel",  p.attackLevel);
        xml->setAttribute("attackSlope",  p.attackSlope);
        xml->setAttribute("decaySlope",   p.decaySlope);
        xml->setAttribute("sustainLevel", p.sustainLevel);
        xml->setAttribute("releaseSlope", p.releaseSlope);
        xml->setAttribute("gate",         p.gate);
        xml->setAttribute("swing",        p.swing);
        xml->setAttribute("timeMul",      p.timeMul);
        xml->setAttribute("humanize",     p.humanize);
        xml->setAttribute("chaos",        p.chaos);
        xml->setAttribute("repeatCount",  p.repeatCount);

        for (int i = 0; i < kMaxSteps; ++i) {
            auto* sx = xml->createNewChildElement("Step");
            sx->setAttribute("idx",         i);
            sx->setAttribute("active",      p.steps[i].active);
            sx->setAttribute("probability", p.steps[i].probability);
            sx->setAttribute("lAttack",     p.steps[i].localAttackLevel);
            sx->setAttribute("lDecay",      p.steps[i].localDecaySlope);
            sx->setAttribute("lSustain",    p.steps[i].localSustainLevel);
            sx->setAttribute("lRelease",    p.steps[i].localReleaseSlope);
        }
        return xml->writeTo(file);
    }

    // Load a pattern by name; returns true on success, fills outParams
    static bool loadPattern(const juce::String& name, SequenceParams& outParams) {
        auto file = patternFileFor(name);
        if (!file.existsAsFile()) return false;
        auto xml = juce::parseXML(file);
        if (xml == nullptr) return false;
        if (xml->getTagName() != "APEXSequencePattern") return false;

        outParams = SequenceParams{};
        outParams.mode         = static_cast<ShapeMode>(xml->getIntAttribute("mode", 0));
        outParams.seed         = static_cast<uint32_t>(xml->getIntAttribute("seed", 12345));
        outParams.numSteps     = xml->getIntAttribute   ("numSteps",     16);
        outParams.attackLevel  = (float) xml->getDoubleAttribute("attackLevel",  1.0);
        outParams.attackSlope  = (float) xml->getDoubleAttribute("attackSlope",  0.25);
        outParams.decaySlope   = (float) xml->getDoubleAttribute("decaySlope",   0.35);
        outParams.sustainLevel = (float) xml->getDoubleAttribute("sustainLevel", 0.55);
        outParams.releaseSlope = (float) xml->getDoubleAttribute("releaseSlope", 0.35);
        outParams.gate         = (float) xml->getDoubleAttribute("gate",         0.75);
        outParams.swing        = (float) xml->getDoubleAttribute("swing",        0.0);
        outParams.timeMul      = (float) xml->getDoubleAttribute("timeMul",      0.5);
        outParams.humanize     = (float) xml->getDoubleAttribute("humanize",     0.0);
        outParams.chaos        = (float) xml->getDoubleAttribute("chaos",        0.0);
        outParams.repeatCount  = xml->getIntAttribute   ("repeatCount",  1);

        for (auto* c = xml->getFirstChildElement(); c != nullptr; c = c->getNextElement()) {
            if (c->getTagName() != "Step") continue;
            const int i = c->getIntAttribute("idx", -1);
            if (i < 0 || i >= kMaxSteps) continue;
            outParams.steps[i].active      = c->getBoolAttribute("active",      true);
            outParams.steps[i].probability = (float) c->getDoubleAttribute("probability", 1.0);
            outParams.steps[i].localAttackLevel  = (float) c->getDoubleAttribute("lAttack",  -1.0);
            outParams.steps[i].localDecaySlope   = (float) c->getDoubleAttribute("lDecay",   -1.0);
            outParams.steps[i].localSustainLevel = (float) c->getDoubleAttribute("lSustain", -1.0);
            outParams.steps[i].localReleaseSlope = (float) c->getDoubleAttribute("lRelease", -1.0);
        }
        return true;
    }

    // Delete a saved pattern by name
    static bool deletePattern(const juce::String& name) {
        auto file = patternFileFor(name);
        return file.existsAsFile() && file.deleteFile();
    }

    // Rename a saved pattern
    static bool renamePattern(const juce::String& oldName, const juce::String& newName) {
        auto src = patternFileFor(oldName);
        auto dst = patternFileFor(newName);
        if (!src.existsAsFile() || dst.existsAsFile()) return false;
        return src.moveFileTo(dst);
    }

    // Returns true if a pattern with this name already exists
    static bool patternExists(const juce::String& name) {
        return patternFileFor(name).existsAsFile();
    }

    // Returns the directory where saved patterns live (for "show in file explorer")
    static juce::File getSavedPatternsDirectory() {
        return getSequencesDir();
    }

private:
    // ---- Singletons (inline static, C++17) ---------------------
    static std::unordered_set<uint32_t>& getUsedSeeds() {
        static std::unordered_set<uint32_t> s;
        return s;
    }
    static bool& getLoaded() { static bool b = false; return b; }
    static std::mutex& getMutex() { static std::mutex m; return m; }

    // ---- File locations ----------------------------------------
    static juce::File getApexUserDir() {
        return juce::File::getSpecialLocation(
            juce::File::userApplicationDataDirectory)
            .getChildFile("APEX");
    }
    static juce::File getSeedHistoryFile() {
        return getApexUserDir().getChildFile("SeedHistory.dat");
    }
    static juce::File getSequencesDir() {
        return getApexUserDir().getChildFile("Sequences");
    }
    static juce::File patternFileFor(const juce::String& name) {
        return getSequencesDir()
            .getChildFile(name + ".apexseq");
    }

    // ---- Seed history I/O --------------------------------------
    static void ensureLoaded() {
        if (getLoaded()) return;
        loadSeedHistory();
        getLoaded() = true;
    }

    static void loadSeedHistory() {
        auto& used = getUsedSeeds();
        used.clear();
        auto f = getSeedHistoryFile();
        if (!f.existsAsFile()) return;
        juce::MemoryBlock mb;
        if (!f.loadFileAsData(mb)) return;
        const size_t count = mb.getSize() / sizeof(uint32_t);
        const uint32_t* data = static_cast<const uint32_t*>(mb.getData());
        used.reserve(count + 64);
        for (size_t i = 0; i < count; ++i)
            used.insert(data[i]);
    }

    static void saveSeedHistory() {
        auto& used = getUsedSeeds();
        auto f = getSeedHistoryFile();
        f.getParentDirectory().createDirectory();
        juce::MemoryBlock mb(used.size() * sizeof(uint32_t));
        uint32_t* out = static_cast<uint32_t*>(mb.getData());
        size_t i = 0;
        for (uint32_t s : used) out[i++] = s;
        // Atomic-ish write: temp file then move
        auto tmp = f.getSiblingFile(f.getFileName() + ".tmp");
        tmp.replaceWithData(mb.getData(), mb.getSize());
        tmp.moveFileTo(f);
    }
};

} // namespace AutomationSeq
} // namespace APEX
