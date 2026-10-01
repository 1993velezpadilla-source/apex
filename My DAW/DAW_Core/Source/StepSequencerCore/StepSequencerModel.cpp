#include "StepSequencerModel.h"

namespace DAW {

StepSequencerModel::StepSequencerModel() {
    patterns_.add(PatternData());
    publishSnapshot(); // Publish initial empty snapshot for audio thread.
}

StepSequencerModel::~StepSequencerModel() = default;

int StepSequencerModel::addPattern(const juce::String& name) {
    const juce::ScopedLock sl(lock_);
    PatternData p;
    p.name = name.isNotEmpty() ? name : "Pattern " + juce::String(patterns_.size() + 1);
    patterns_.add(std::move(p));
    currentPatternIndex_ = patterns_.size() - 1;
    sendChangeMessage();
    publishSnapshot();
    return currentPatternIndex_;
}

void StepSequencerModel::removePattern(int index) {
    const juce::ScopedLock sl(lock_);
    if (patterns_.size() <= 1) return;
    patterns_.remove(index);
    currentPatternIndex_ = juce::jlimit(0, patterns_.size() - 1, currentPatternIndex_);
    sendChangeMessage();
    publishSnapshot();
}

void StepSequencerModel::setCurrentPattern(int index) {
    currentPatternIndex_ = juce::jlimit(0, patterns_.size() - 1, index);
    sendChangeMessage();
    publishSnapshot();
}

int StepSequencerModel::getCurrentPatternIndex() const noexcept { return currentPatternIndex_; }
int StepSequencerModel::getPatternCount() const noexcept { return patterns_.size(); }

PatternData& StepSequencerModel::getPattern(int index) { return patterns_.getReference(index); }
// UB fix (C2): const juce::Array::operator[] returns BY VALUE — the previous
// version returned a reference bound to that temporary (dangling, C4172).
const PatternData& StepSequencerModel::getPattern(int index) const { return patterns_.getReference(index); }

void StepSequencerModel::clonePattern(int sourceIndex) {
    const juce::ScopedLock sl(lock_);
    if (!juce::isPositiveAndBelow(sourceIndex, patterns_.size())) return;
    PatternData clone = patterns_[sourceIndex];
    clone.name += " (copy)";
    patterns_.add(std::move(clone));
    currentPatternIndex_ = patterns_.size() - 1;
    sendChangeMessage();
    publishSnapshot();
}

StablePatternId StepSequencerModel::clonePatternWithId(int sourceIndex) {
    const juce::ScopedLock sl(lock_);
    if (!juce::isPositiveAndBelow(sourceIndex, patterns_.size()))
        return StablePatternId();
    PatternData clone = patterns_[sourceIndex];
    clone.id = StablePatternId(); // Generate new UUID
    clone.name += " (unique)";
    const auto newId = clone.id;
    patterns_.add(std::move(clone));
    currentPatternIndex_ = patterns_.size() - 1;
    sendChangeMessage();
    publishSnapshot();
    return newId;
}

int StepSequencerModel::addChannel(const juce::String& name) {
    const juce::ScopedLock sl(lock_);
    auto& p = patterns_.getReference(currentPatternIndex_);
    LaneData ln;
    ln.name = name;
    ln.steps.resize(p.getTotalSteps());
    p.lanes.add(std::move(ln));
    sendChangeMessage();
    publishSnapshot();
    return p.lanes.size() - 1;
}

void StepSequencerModel::removeChannel(int index) {
    const juce::ScopedLock sl(lock_);
    patterns_.getReference(currentPatternIndex_).lanes.remove(index);
    sendChangeMessage();
    publishSnapshot();
}

void StepSequencerModel::moveChannel(int from, int to) {
    const juce::ScopedLock sl(lock_);
    patterns_.getReference(currentPatternIndex_).lanes.move(from, to);
    sendChangeMessage();
    publishSnapshot();
}

juce::Array<LaneData>& StepSequencerModel::getChannels() {
    return patterns_.getReference(currentPatternIndex_).lanes;
}

LaneData& StepSequencerModel::getChannel(int index) {
    return patterns_.getReference(currentPatternIndex_).lanes.getReference(index);
}

int StepSequencerModel::addLane(const juce::String& name) {
    return addChannel(name);
}

void StepSequencerModel::removeLane(int index) {
    removeChannel(index);
}

void StepSequencerModel::moveLane(int fromIndex, int toIndex) {
    moveChannel(fromIndex, toIndex);
}

LaneData& StepSequencerModel::getLane(int index) {
    return getChannel(index);
}

const LaneData& StepSequencerModel::getLane(int index) const {
    return patterns_[currentPatternIndex_].lanes.getReference(index);
}

int StepSequencerModel::getLaneCount() const noexcept {
    return patterns_[currentPatternIndex_].lanes.size();
}

void StepSequencerModel::toggleStep(int ch, int step) {
    const juce::ScopedLock sl(lock_);
    auto& steps = patterns_.getReference(currentPatternIndex_)
                      .lanes.getReference(ch).steps;
    if (juce::isPositiveAndBelow(step, steps.size()))
        steps.getReference(step).active = !steps.getReference(step).active;
    sendChangeMessage();
    publishSnapshot();
}

void StepSequencerModel::setStepVelocity(int ch, int step, float vel) {
    const juce::ScopedLock sl(lock_);
    auto& steps = patterns_.getReference(currentPatternIndex_)
                      .lanes.getReference(ch).steps;
    if (juce::isPositiveAndBelow(step, steps.size()))
        steps.getReference(step).velocity = vel;
    sendChangeMessage();
    publishSnapshot();
}

void StepSequencerModel::setStepProbability(int ch, int step, float prob) {
    const juce::ScopedLock sl(lock_);
    auto& steps = patterns_.getReference(currentPatternIndex_)
                      .lanes.getReference(ch).steps;
    if (juce::isPositiveAndBelow(step, steps.size()))
        steps.getReference(step).probability = prob;
    sendChangeMessage();
    publishSnapshot();
}

void StepSequencerModel::setStepPitchOffset(int ch, int step, int8_t offset) {
    const juce::ScopedLock sl(lock_);
    auto& steps = patterns_.getReference(currentPatternIndex_)
                      .lanes.getReference(ch).steps;
    if (juce::isPositiveAndBelow(step, steps.size()))
        steps.getReference(step).pitchOffset = offset;
    sendChangeMessage();
    publishSnapshot();
}

void StepSequencerModel::addStepAtPitch(int ch, int step, int8_t pitchOffset) {
    const juce::ScopedLock sl(lock_);
    auto& steps = patterns_.getReference(currentPatternIndex_)
                      .lanes.getReference(ch).steps;
    if (!juce::isPositiveAndBelow(step, steps.size())) return;
    auto& ev = steps.getReference(step);
    ev.active = true;
    ev.pitchOffset = pitchOffset;
    sendChangeMessage();
    publishSnapshot();
}

StepSequencerModel::Snapshot StepSequencerModel::getSnapshot() const {
    const juce::ScopedLock sl(lock_);
    Snapshot snap;
    const auto& p = patterns_[currentPatternIndex_];
    snap.patternId = p.id;
    snap.patternId_hash = (uint32_t)(p.id.hash() & 0xFFFFFFFFu);
    snap.name = p.name;
    snap.stepsPerBeat = p.stepsPerBeat;
    snap.beatsPerBar = p.beatsPerBar;
    snap.barsPerPattern = p.barsPerPattern;
    snap.globalSwing = p.globalSwing;
    snap.totalSteps = p.getTotalSteps();
    for (const auto& ln : p.lanes) {
        Snapshot::LaneSnapshot ls;
        ls.id = ln.id;
        ls.name = ln.name;
        ls.volume = ln.volume;
        ls.pan = ln.pan;
        ls.muted = ln.muted;
        ls.soloed = ln.soloed;
        ls.midiNote = ln.midiNote;
        ls.midiChannel = ln.midiChannel;
        ls.swingAmount = ln.swingAmount;
        ls.laneLength = ln.laneLength;
        ls.steps = ln.steps;
        snap.lanes.add(std::move(ls));
    }
    return snap;
}

void StepSequencerModel::publishSnapshot()
{
    // Build snapshot on message thread under lock (allocation is safe here).
    auto snap = std::make_shared<Snapshot>(getSnapshot());
    // Publish with a release store — the audio thread acquires it lock-free
    // in getSnapshotRT(). No mutex on either side of the handoff. The
    // replaced snapshot is held in retiredSnapshot_ until the NEXT publish
    // (message thread), so the audio thread is never the last owner.
    const std::shared_ptr<const Snapshot> immutableSnap = std::move(snap);
    auto old = std::atomic_exchange_explicit(&publishedSnapshot_, immutableSnap, std::memory_order_acq_rel);
    retiredSnapshot_ = std::move(old);
}

juce::ValueTree StepSequencerModel::toValueTree() const {
    const juce::ScopedLock sl(lock_);
    juce::ValueTree tree("StepSequencer");
    tree.setProperty("currentPattern", currentPatternIndex_, nullptr);
    for (const auto& p : patterns_) {
        juce::ValueTree patTree("Pattern");
        patTree.setProperty("name", p.name, nullptr);
        patTree.setProperty("stepsPerBeat", p.stepsPerBeat, nullptr);
        patTree.setProperty("beatsPerBar", p.beatsPerBar, nullptr);
        patTree.setProperty("barsPerPattern", p.barsPerPattern, nullptr);
        patTree.setProperty("globalSwing", p.globalSwing, nullptr);
        for (const auto& ln : p.lanes) {
            juce::ValueTree chTree("Lane");
            chTree.setProperty("name", ln.name, nullptr);
            chTree.setProperty("volume", ln.volume, nullptr);
            chTree.setProperty("pan", ln.pan, nullptr);
            chTree.setProperty("muted", ln.muted, nullptr);
            chTree.setProperty("soloed", ln.soloed, nullptr);
            chTree.setProperty("midiNote", ln.midiNote, nullptr);
            chTree.setProperty("midiChannel", ln.midiChannel, nullptr);
            chTree.setProperty("swingAmount", ln.swingAmount, nullptr);
            chTree.setProperty("laneLength", ln.laneLength, nullptr);
            for (const auto& s : ln.steps) {
                juce::ValueTree stepTree("Step");
                stepTree.setProperty("active", s.active, nullptr);
                stepTree.setProperty("velocity", (double)s.velocity, nullptr);
                stepTree.setProperty("pitchOffset", (int)s.pitchOffset, nullptr);
                stepTree.setProperty("pan", (double)s.pan, nullptr);
                stepTree.setProperty("timingShift", (int)s.timingShift, nullptr);
                stepTree.setProperty("probability", s.probability, nullptr);
                chTree.addChild(stepTree, -1, nullptr);
            }
            patTree.addChild(chTree, -1, nullptr);
        }
        tree.addChild(patTree, -1, nullptr);
    }
    return tree;
}

void StepSequencerModel::fromValueTree(const juce::ValueTree& tree) {
    const juce::ScopedLock sl(lock_);
    patterns_.clear();
    currentPatternIndex_ = (int)tree.getProperty("currentPattern", 0);
    for (int pi = 0; pi < tree.getNumChildren(); ++pi) {
        const auto& patTree = tree.getChild(pi);
        PatternData p;
        p.name = patTree.getProperty("name", "Pattern");
        p.stepsPerBeat = (int)patTree.getProperty("stepsPerBeat", 4);
        p.beatsPerBar = (int)patTree.getProperty("beatsPerBar", 4);
        p.barsPerPattern = (int)patTree.getProperty("barsPerPattern", 1);
        p.globalSwing = (float)patTree.getProperty("globalSwing", 0.0f);
        for (int ci = 0; ci < patTree.getNumChildren(); ++ci) {
            const auto& chTree = patTree.getChild(ci);
            LaneData ln;
            ln.name = chTree.getProperty("name", "Lane");
            ln.volume = (float)chTree.getProperty("volume", 1.0f);
            ln.pan = (float)chTree.getProperty("pan", 0.0f);
            ln.muted = (bool)chTree.getProperty("muted", false);
            ln.soloed = (bool)chTree.getProperty("soloed", false);
            ln.midiNote = (int)chTree.getProperty("midiNote", 60);
            ln.midiChannel = (int)chTree.getProperty("midiChannel", 0);
            ln.swingAmount = (float)chTree.getProperty("swingAmount", 0.0f);
            ln.laneLength = (int)chTree.getProperty("laneLength", 0);
            for (int si = 0; si < chTree.getNumChildren(); ++si) {
                const auto& sTree = chTree.getChild(si);
                StepEvent s;
                s.active = (bool)sTree.getProperty("active", false);
                s.velocity = (float)(double)sTree.getProperty("velocity", 1.0);
                s.pitchOffset = (int8_t)(int)sTree.getProperty("pitchOffset", 0);
                s.pan = (float)(double)sTree.getProperty("pan", 0.0);
                s.timingShift = (int8_t)(int)sTree.getProperty("timingShift", 0);
                s.probability = (float)sTree.getProperty("probability", 1.0f);
                ln.steps.add(s);
            }
            p.lanes.add(std::move(ln));
        }
        patterns_.add(std::move(p));
    }
    sendChangeMessage();
    publishSnapshot();
}

} // namespace DAW
