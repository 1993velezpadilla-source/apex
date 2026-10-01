#pragma once
#include <JuceHeader.h>

namespace DAW {

// Type aliases for clarity and future flexibility
using TrackID = juce::String;
using ClipID = juce::String;
using PluginID = juce::String;
using RouteID = juce::String;
using SidechainID = juce::String;
using SamplePosition = int64_t;
using BarBeatPosition = double;

// ID Generator
class IDGenerator
{
public:
    static TrackID generateTrackID();
    static ClipID generateClipID();
    static PluginID generatePluginID();
    static RouteID generateRouteID();
    static SidechainID generateSidechainID();

    /** Advance trackCounter to at least minValue (monotonic, relaxed).
     *  Called after restoring persisted tracks so freshly created tracks can
     *  never collide with IDs that were loaded from disk. */
    static void seedTrackCounter(int minValue);
    
private:
    static std::atomic<int> trackCounter;
    static std::atomic<int> pluginCounter;
    static std::atomic<int> routeCounter;
    static std::atomic<int> sidechainCounter;
};

// Result type for operations
template<typename T>
class Result
{
public:
    static Result success(T value) { return Result(std::move(value), true, ""); }
    static Result failure(const juce::String& error) { return Result(T(), false, error); }
    
    bool isSuccess() const { return success_; }
    bool isFailure() const { return !success_; }
    const T& getValue() const { return value_; }
    const juce::String& getError() const { return error_; }
    
private:
    Result(T val, bool success, juce::String error)
        : value_(std::move(val)), success_(success), error_(std::move(error)) {}
    
    T value_;
    bool success_;
    juce::String error_;
};

} // namespace DAW
