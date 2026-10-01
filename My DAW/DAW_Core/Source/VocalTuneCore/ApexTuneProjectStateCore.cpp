// =============================================================================
//  ApexTuneProjectStateCore.cpp
//  See header. ValueTree <-> ApexTuneClipState with defensive loading.
//
//  Drop-in: Source/VocalTuneCore/ApexTuneProjectStateCore.cpp
// =============================================================================

#include "ApexTuneProjectStateCore.h"

namespace apex { namespace vocaltune {

// -----------------------------------------------------------------------------
// Exposed root identifiers.
// -----------------------------------------------------------------------------
const juce::Identifier ApexTuneProjectStateCore::idRoot  { "ApexTuneClipState" };
const juce::Identifier ApexTuneProjectStateCore::idNotes { "Notes" };
const juce::Identifier ApexTuneProjectStateCore::idNote  { "Note" };

// -----------------------------------------------------------------------------
// Internal property identifiers (file-local).
// -----------------------------------------------------------------------------
namespace
{
    // Root properties
    const juce::Identifier propSchemaVersion { "schemaVersion" };
    const juce::Identifier propClipId        { "clipId" };
    const juce::Identifier propSourceFile    { "sourceFile" };
    const juce::Identifier propScaleRoot     { "scaleRoot" };
    const juce::Identifier propScaleType     { "scaleType" };
    const juce::Identifier propAnalyzed      { "analyzed" };
    const juce::Identifier propAnalysisVer   { "analysisVersion" };
    const juce::Identifier propRenderVer     { "renderVersion" };
    const juce::Identifier propBypassed      { "bypassed" };

    // Note properties
    const juce::Identifier propNoteId        { "noteId" };
    const juce::Identifier propStartSample   { "startSample" };
    const juce::Identifier propEndSample     { "endSample" };
    const juce::Identifier propDetectedMidi  { "detectedMidi" };
    const juce::Identifier propTargetMidi    { "targetMidi" };
    const juce::Identifier propCentsOffset   { "centsOffset" };
    const juce::Identifier propCorrAmt       { "correctionAmount" };
    const juce::Identifier propDriftAmt      { "driftAmount" };
    const juce::Identifier propModAmt        { "modulationAmount" };
    const juce::Identifier propFormantShift  { "formantShift" };
    const juce::Identifier propGainDb        { "gainDb" };
    const juce::Identifier propVoiced        { "voiced" };
    const juce::Identifier propSibilantProt  { "sibilantProtected" };

    // -------------------------------------------------------------------------
    // Defensive property readers. Always return a usable value.
    // -------------------------------------------------------------------------
    juce::String readString (const juce::ValueTree& t,
                             const juce::Identifier& id,
                             const juce::String& fallback)
    {
        if (! t.hasProperty (id)) return fallback;
        return t.getProperty (id).toString();
    }

    int readInt (const juce::ValueTree& t, const juce::Identifier& id, int fallback)
    {
        if (! t.hasProperty (id)) return fallback;
        return (int) t.getProperty (id);
    }

    juce::int64 readInt64 (const juce::ValueTree& t,
                           const juce::Identifier& id, juce::int64 fallback)
    {
        if (! t.hasProperty (id)) return fallback;
        return (juce::int64) t.getProperty (id);
    }

    float readFloat (const juce::ValueTree& t,
                     const juce::Identifier& id, float fallback)
    {
        if (! t.hasProperty (id)) return fallback;
        return (float) (double) t.getProperty (id);
    }

    bool readBool (const juce::ValueTree& t,
                   const juce::Identifier& id, bool fallback)
    {
        if (! t.hasProperty (id)) return fallback;
        return (bool) t.getProperty (id);
    }

    // -------------------------------------------------------------------------
    // Convert one note to/from a ValueTree.
    // -------------------------------------------------------------------------
    juce::ValueTree noteToTree (const ApexTuneNote& n)
    {
        juce::ValueTree t (ApexTuneProjectStateCore::idNote);
        t.setProperty (propNoteId,       n.noteId,                                  nullptr);
        t.setProperty (propStartSample,  juce::var ((juce::int64) n.startSample),   nullptr);
        t.setProperty (propEndSample,    juce::var ((juce::int64) n.endSample),     nullptr);
        t.setProperty (propDetectedMidi, (double) n.detectedMidi,                   nullptr);
        t.setProperty (propTargetMidi,   (double) n.targetMidi,                     nullptr);
        t.setProperty (propCentsOffset,  (double) n.centsOffset,                    nullptr);
        t.setProperty (propCorrAmt,      (double) n.correctionAmount,               nullptr);
        t.setProperty (propDriftAmt,     (double) n.driftAmount,                    nullptr);
        t.setProperty (propModAmt,       (double) n.modulationAmount,               nullptr);
        t.setProperty (propFormantShift, (double) n.formantShift,                   nullptr);
        t.setProperty (propGainDb,       (double) n.gainDb,                         nullptr);
        t.setProperty (propVoiced,       n.voiced,                                  nullptr);
        t.setProperty (propSibilantProt, n.sibilantProtected,                       nullptr);
        return t;
    }

    bool noteFromTree (const juce::ValueTree& t, ApexTuneNote& out)
    {
        if (! t.hasType (ApexTuneProjectStateCore::idNote)) return false;

        const ApexTuneNote defaults {};

        out.noteId            = readString (t, propNoteId,       defaults.noteId);
        out.startSample       = readInt64  (t, propStartSample,  defaults.startSample);
        out.endSample         = readInt64  (t, propEndSample,    defaults.endSample);
        out.detectedMidi      = readFloat  (t, propDetectedMidi, defaults.detectedMidi);
        out.targetMidi        = readFloat  (t, propTargetMidi,   defaults.targetMidi);
        out.centsOffset       = readFloat  (t, propCentsOffset,  defaults.centsOffset);
        out.correctionAmount  = readFloat  (t, propCorrAmt,      defaults.correctionAmount);
        out.driftAmount       = readFloat  (t, propDriftAmt,     defaults.driftAmount);
        out.modulationAmount  = readFloat  (t, propModAmt,       defaults.modulationAmount);
        out.formantShift      = readFloat  (t, propFormantShift, defaults.formantShift);
        out.gainDb            = readFloat  (t, propGainDb,       defaults.gainDb);
        out.voiced            = readBool   (t, propVoiced,       defaults.voiced);
        out.sibilantProtected = readBool   (t, propSibilantProt, defaults.sibilantProtected);

        // Sanity: endSample must be >= startSample. If not, mark unvoiced/zero-length.
        if (out.endSample < out.startSample) out.endSample = out.startSample;

        return true;
    }
}

// -----------------------------------------------------------------------------
juce::ValueTree ApexTuneProjectStateCore::toValueTree (const ApexTuneClipState& state)
{
    juce::ValueTree tree (idRoot);

    tree.setProperty (propSchemaVersion, currentSchemaVersion,                  nullptr);
    tree.setProperty (propClipId,        state.clipId,                          nullptr);
    tree.setProperty (propSourceFile,    state.sourceFile.getFullPathName(),    nullptr);
    tree.setProperty (propScaleRoot,     state.scaleRoot,                       nullptr);
    tree.setProperty (propScaleType,     state.scaleType,                       nullptr);
    tree.setProperty (propAnalyzed,      state.analyzed,                        nullptr);
    tree.setProperty (propAnalysisVer,   state.analysisVersion,                 nullptr);
    tree.setProperty (propRenderVer,     state.renderVersion,                   nullptr);
    tree.setProperty (propBypassed,      state.bypassed,                        nullptr);

    juce::ValueTree notes (idNotes);
    for (const auto& n : state.notes)
        notes.appendChild (noteToTree (n), nullptr);

    tree.appendChild (notes, nullptr);
    return tree;
}

// -----------------------------------------------------------------------------
bool ApexTuneProjectStateCore::fromValueTree (const juce::ValueTree& tree,
                                              ApexTuneClipState& out)
{
    if (! tree.isValid() || ! tree.hasType (idRoot)) return false;

    // ---- Schema version check / migration ----------------------------------
    const int schema = readInt (tree, propSchemaVersion, 1);
    if (schema > currentSchemaVersion)
    {
        // Future schema -- attempt best-effort load. Anything unknown is ignored
        // by the defensive readers below. We do NOT bail out, because that
        // would silently lose the user's edits.
    }
    // (When we add V2, insert per-property migration logic here.)

    // ---- Root properties ---------------------------------------------------
    const ApexTuneClipState defaults {};

    out = {};   // start from defaults

    out.clipId          = readString (tree, propClipId,      defaults.clipId);

    const juce::String path = readString (tree, propSourceFile, juce::String());
    out.sourceFile      = path.isEmpty() ? juce::File() : juce::File (path);

    out.scaleRoot       = readString (tree, propScaleRoot,   defaults.scaleRoot);
    out.scaleType       = readString (tree, propScaleType,   defaults.scaleType);
    out.analyzed        = readBool   (tree, propAnalyzed,    defaults.analyzed);
    out.analysisVersion = readInt    (tree, propAnalysisVer, defaults.analysisVersion);
    out.renderVersion   = readInt    (tree, propRenderVer,   defaults.renderVersion);
    out.bypassed        = readBool   (tree, propBypassed,    defaults.bypassed);

    // ---- Notes -------------------------------------------------------------
    const auto notesTree = tree.getChildWithName (idNotes);
    if (notesTree.isValid())
    {
        out.notes.reserve ((size_t) notesTree.getNumChildren());
        for (int i = 0; i < notesTree.getNumChildren(); ++i)
        {
            ApexTuneNote n;
            if (noteFromTree (notesTree.getChild (i), n))
                out.notes.push_back (n);
        }
    }

    return true;
}

// -----------------------------------------------------------------------------
juce::String ApexTuneProjectStateCore::toXmlString (const ApexTuneClipState& state)
{
    const auto tree = toValueTree (state);
    if (auto xml = tree.createXml())
        return xml->toString();
    return {};
}

// -----------------------------------------------------------------------------
bool ApexTuneProjectStateCore::fromXmlString (const juce::String& xmlString,
                                              ApexTuneClipState& outState)
{
    if (xmlString.isEmpty()) return false;

    auto xml = juce::XmlDocument::parse (xmlString);
    if (xml == nullptr) return false;

    const auto tree = juce::ValueTree::fromXml (*xml);
    return fromValueTree (tree, outState);
}

// -----------------------------------------------------------------------------
bool ApexTuneProjectStateCore::sourceFileExists (const ApexTuneClipState& state)
{
    return state.sourceFile != juce::File()
        && state.sourceFile.existsAsFile();
}

}} // namespace apex::vocaltune
