// ===========================================================================
// QuickTrackTemplate.h
// APEX Quick Track Builder — reusable SESSION-STRUCTURE RECIPES.
//
// A template is NOT a project file and NEVER stores runtime identities:
// no TrackID, no RoutingNodeID, no connection IDs, no pointers.
// It stores role IDs + counts + order + per-role color mode
// (AUTO | explicit ARGB) + the canonical routing intent (derived from the
// role descriptors' routesToRoleId / preferredParentRoleId fields).
//
// Storage: application-global user data (%APPDATA%/APEX/QuickTrackTemplates),
// versioned XML (ValueTree), atomic temp→validate→replace saves, so a crash
// can never corrupt the whole template set and one corrupt file never
// prevents loading the others.
// ===========================================================================
#pragma once
#include <JuceHeader.h>
#include <vector>
#include "../QuickTrackCore/QuickTrackRoles.h"

namespace DAW {

struct QuickTrackTemplateRole
{
    juce::String roleId;        // catalog role id (stable)
    int count = 0;              // quantity queued for creation
    int order = 0;              // display order within the template
    bool autoColor = true;      // true = project decides; false = explicitColor
    juce::String explicitColor; // ARGB hex when autoColor == false
};

struct QuickTrackTemplate
{
    static constexpr int kCurrentVersion = 1;

    juce::String name;
    int version = kCurrentVersion;
    std::vector<QuickTrackTemplateRole> roles; // ordered by .order

    int totalTrackCount() const noexcept
    {
        int total = 0;
        for (const auto& r : roles)
            total += r.count;
        return total;
    }

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree tree("QuickTrackTemplate");
        tree.setProperty("version", version, nullptr);
        tree.setProperty("name", name, nullptr);
        for (const auto& role : roles)
        {
            juce::ValueTree roleTree("Role");
            roleTree.setProperty("roleId", role.roleId, nullptr);
            roleTree.setProperty("count", role.count, nullptr);
            roleTree.setProperty("order", role.order, nullptr);
            roleTree.setProperty("colorMode", role.autoColor ? "auto" : "explicit", nullptr);
            if (!role.autoColor && role.explicitColor.isNotEmpty())
                roleTree.setProperty("explicitColor", role.explicitColor, nullptr);
            tree.appendChild(roleTree, nullptr);
        }
        return tree;
    }

    bool fromValueTree(const juce::ValueTree& tree)
    {
        roles.clear();
        name.clear();
        if (!tree.isValid() || !tree.hasType("QuickTrackTemplate"))
            return false;

        version = (int) tree.getProperty("version", 1);
        name = tree.getProperty("name").toString();

        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            const auto child = tree.getChild(i);
            if (!child.hasType("Role"))
                continue;

            const auto roleId = child.getProperty("roleId").toString();
            if (QuickTrackRoleCatalog::findById(roleId) == nullptr)
                continue; // unknown/future role — skip, never corrupt the load

            QuickTrackTemplateRole role;
            role.roleId = roleId;
            role.count = juce::jmax(0, (int) child.getProperty("count", 0));
            role.order = (int) child.getProperty("order", i);
            role.autoColor = child.getProperty("colorMode", "auto").toString() != "explicit";
            if (!role.autoColor)
                role.explicitColor = child.getProperty("explicitColor").toString();
            roles.push_back(std::move(role));
        }

        std::stable_sort(roles.begin(), roles.end(),
                         [](const QuickTrackTemplateRole& a, const QuickTrackTemplateRole& b)
                         { return a.order < b.order; });
        return true;
    }
};

// ===========================================================================
// QuickTrackTemplateStore — application-global, durable, atomic.
// ===========================================================================
class QuickTrackTemplateStore
{
public:
    /** baseDir override exists for tests only; production uses the default
     *  deterministic APEX user-data location. */
    explicit QuickTrackTemplateStore(juce::File baseDir = juce::File())
        : baseDir_(baseDir.isDirectory() ? baseDir : defaultBaseDir())
    {
    }

    static juce::File defaultBaseDir()
    {
        // %APPDATA%/APEX/QuickTrackTemplates — app-global, survives
        // New Project / project close / APEX restart.
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("APEX")
            .getChildFile("QuickTrackTemplates");
    }

    juce::File getDirectory() const noexcept { return baseDir_; }

    static juce::String sanitizeFileName(const juce::String& name)
    {
        juce::String s = name.trim();
        juce::String out;
        for (auto c : s)
            out += juce::CharacterFunctions::isLetterOrDigit(c) || c == ' ' || c == '-'
                       || c == '_' || c == '(' || c == ')'
                   ? c
                   : (juce::juce_wchar) '_';
        return out.trim();
    }

    juce::File getTemplateFile(const juce::String& name) const
    {
        const auto safe = sanitizeFileName(name);
        return baseDir_.getChildFile((safe.isEmpty() ? juce::String("template") : safe) + ".qttpl");
    }

    bool templateExists(const juce::String& name) const
    {
        return getTemplateFile(name).existsAsFile();
    }

    enum class SaveResult { Saved, AlreadyExists, Failed };

    /** Saves atomically. Never overwrites unless overwriteExisting == true. */
    SaveResult saveTemplate(const QuickTrackTemplate& t, bool overwriteExisting, juce::String& errorOut)
    {
        errorOut.clear();
        if (t.name.trim().isEmpty())
        {
            errorOut = "Template name is empty";
            return SaveResult::Failed;
        }

        baseDir_.createDirectory();
        const auto target = getTemplateFile(t.name);
        if (target.existsAsFile() && !overwriteExisting)
        {
            errorOut = "A template named \"" + t.name + "\" already exists";
            return SaveResult::AlreadyExists;
        }

        // 1) Serialize + validate BEFORE touching the target.
        auto tree = t.toValueTree();
        tree.setProperty("name", t.name.trim(), nullptr);
        auto xml = tree.createXml();
        if (xml == nullptr)
        {
            errorOut = "Template could not be serialized";
            return SaveResult::Failed;
        }

        // 2) Write temp → parse back → atomic replace.
        const auto tempFile = target.getSiblingFile("." + target.getFileName() + ".tmp");
        if (!xml->writeTo(tempFile))
        {
            tempFile.deleteFile();
            errorOut = "Template write failed";
            return SaveResult::Failed;
        }

        QuickTrackTemplate verify;
        {
            auto parsedXml = juce::XmlDocument::parse(tempFile);
            if (parsedXml == nullptr)
            {
                tempFile.deleteFile();
                errorOut = "Template validation failed";
                return SaveResult::Failed;
            }
            auto parsedTree = juce::ValueTree::fromXml(*parsedXml);
            if (!verify.fromValueTree(parsedTree) || verify.name.trim().isEmpty())
            {
                tempFile.deleteFile();
                errorOut = "Template validation failed";
                return SaveResult::Failed;
            }
        }

        if (target.existsAsFile() && !target.deleteFile())
        {
            tempFile.deleteFile();
            errorOut = "Could not replace existing template";
            return SaveResult::Failed;
        }
        if (!tempFile.moveFileTo(target))
        {
            tempFile.deleteFile();
            errorOut = "Could not finalize template";
            return SaveResult::Failed;
        }
        return SaveResult::Saved;
    }

    bool loadTemplate(const juce::String& name, QuickTrackTemplate& out) const
    {
        const auto file = getTemplateFile(name);
        if (!file.existsAsFile())
            return false;
        auto xml = juce::XmlDocument::parse(file);
        if (xml == nullptr)
            return false;
        auto tree = juce::ValueTree::fromXml(*xml);
        if (!out.fromValueTree(tree))
            return false;
        return true;
    }

    std::vector<QuickTrackTemplate> listTemplates() const
    {
        std::vector<QuickTrackTemplate> result;
        if (!baseDir_.isDirectory())
            return result;

        juce::Array<juce::File> files = baseDir_.findChildFiles(juce::File::findFiles, false, "*.qttpl");
        files.sort();
        for (const auto& file : files)
        {
            QuickTrackTemplate t;
            auto xml = juce::XmlDocument::parse(file);
            if (xml == nullptr)
                continue; // corrupt/partial file — skip, never destroy others
            auto tree = juce::ValueTree::fromXml(*xml);
            if (t.fromValueTree(tree) && t.name.trim().isNotEmpty())
                result.push_back(std::move(t));
        }
        return result;
    }

    bool deleteTemplate(const juce::String& name, juce::String& errorOut)
    {
        errorOut.clear();
        const auto file = getTemplateFile(name);
        if (!file.existsAsFile())
        {
            errorOut = "Template \"" + name + "\" not found";
            return false;
        }
        return file.deleteFile();
    }

private:
    juce::File baseDir_;
};

} // namespace DAW
