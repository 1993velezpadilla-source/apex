// =============================================================================
//  ApexTuneProjectStateCore.h
//  Serialize / deserialize ApexTuneClipState to/from juce::ValueTree (XML).
//
//  Drop-in: Source/VocalTuneCore/ApexTuneProjectStateCore.h
//  Depends on: ApexTuneTypes.h, JUCE.
//  Touches:    nothing else in the APEX codebase. APEX's project save/load
//              will call toValueTree() to embed clip state, and
//              fromValueTree() to restore it.
//
//  Schema versioning:
//   currentSchemaVersion is bumped on incompatible changes. fromValueTree
//   handles older versions transparently (V1 has no migrations yet).
//
//  XML shape produced by toValueTree():
//
//    <ApexTuneClipState schemaVersion="1" clipId="..." sourceFile="..."
//                       scaleRoot="C" scaleType="Chromatic"
//                       analyzed="1" analysisVersion="3" renderVersion="7">
//      <Notes>
//        <Note noteId="n_0" startSample="0" endSample="48000"
//              detectedMidi="69.21" targetMidi="69" centsOffset="-21"
//              correctionAmount="1" driftAmount="1" modulationAmount="1"
//              formantShift="0" gainDb="0"
//              voiced="1" sibilantProtected="0"/>
//        ...
//      </Notes>
//    </ApexTuneClipState>
//
//  Defensive loading:
//   - Missing properties fall back to ApexTuneClipState / ApexTuneNote defaults.
//   - Missing <Notes> child = empty notes vector.
//   - Source file path that no longer exists on disk is preserved verbatim
//     in state.sourceFile; the UI/render layer can check existsAsFile() and
//     show a "missing" status. We do NOT crash or skip the state.
// =============================================================================

#pragma once

#include "ApexTuneTypes.h"

namespace apex { namespace vocaltune {

class ApexTuneProjectStateCore
{
public:
    // Bump on incompatible changes. Add a migration block in fromValueTree.
    static constexpr int currentSchemaVersion = 1;

    // ---- Primary ValueTree interface ---------------------------------------
    static juce::ValueTree toValueTree   (const ApexTuneClipState& state);
    static bool            fromValueTree (const juce::ValueTree& tree,
                                          ApexTuneClipState& outState);

    // ---- XML convenience ---------------------------------------------------
    static juce::String    toXmlString   (const ApexTuneClipState& state);
    static bool            fromXmlString (const juce::String& xml,
                                          ApexTuneClipState& outState);

    // ---- Helpers -----------------------------------------------------------
    // True if state.sourceFile points at an existing readable file.
    // Use this in the UI to show a "missing source" badge.
    static bool sourceFileExists (const ApexTuneClipState& state);

    // ---- Identifiers (exposed so APEX project XML can locate them) --------
    static const juce::Identifier idRoot;
    static const juce::Identifier idNotes;
    static const juce::Identifier idNote;
};

}} // namespace apex::vocaltune
