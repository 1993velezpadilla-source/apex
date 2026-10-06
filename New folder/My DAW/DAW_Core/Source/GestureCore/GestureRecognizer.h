#pragma once
#include <JuceHeader.h>

namespace DAW {

// Gesture types
enum class GestureType
{
    None,
    Tap,
    DoubleTap,
    LongPress,
    Drag,
    Swipe,
    TwoFingerScroll,
    PinchZoom,
    Rotate
};

// Gesture event data
struct GestureEvent
{
    GestureType type = GestureType::None;
    juce::Point<float> startPosition;
    juce::Point<float> currentPosition;
    juce::Point<float> velocity;
    float scale = 1.0f;      // For pinch zoom
    float rotation = 0.0f;    // For rotation gesture
    int touchCount = 0;
    juce::int64 timestamp = 0;
};

// Gesture recognizer - detects multi-touch gestures
class GestureRecognizer
{
public:
    GestureRecognizer();
    ~GestureRecognizer();
    
    // Feed touch events
    void touchDown(int touchIndex, juce::Point<float> position, juce::int64 timestamp);
    void touchMove(int touchIndex, juce::Point<float> position, juce::int64 timestamp);
    void touchUp(int touchIndex, juce::Point<float> position, juce::int64 timestamp);
    void touchCancel();
    
    // Configuration
    void setLongPressDelay(int ms) { longPressDelayMs_ = ms; }
    void setDoubleTapMaxDelay(int ms) { doubleTapMaxDelayMs_ = ms; }
    void setDragThreshold(float pixels) { dragThresholdPx_ = pixels; }
    
    // Current gesture state
    GestureType getCurrentGesture() const { return currentGesture_; }
    const GestureEvent& getGestureEvent() const { return gestureEvent_; }
    
    // Listeners
    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void gestureDetected(const GestureEvent& event) {}
        virtual void gestureUpdated(const GestureEvent& event) {}
        virtual void gestureEnded(const GestureEvent& event) {}
    };
    
    void addListener(Listener* listener);
    void removeListener(Listener* listener);
    
private:
    struct TouchPoint
    {
        int id = -1;
        juce::Point<float> startPos;
        juce::Point<float> currentPos;
        juce::Point<float> lastPos;
        juce::int64 startTime = 0;
        juce::int64 lastTime = 0;
        bool moved = false;
    };
    
    std::map<int, TouchPoint> activeTouches_;
    GestureType currentGesture_ = GestureType::None;
    GestureEvent gestureEvent_;
    
    // Timers
    int longPressDelayMs_ = 400;
    int doubleTapMaxDelayMs_ = 300;
    float dragThresholdPx_ = 8.0f;
    
    // State for gesture detection
    juce::int64 lastTapTime_ = 0;
    juce::Point<float> lastTapPos_;
    int tapCount_ = 0;
    
    bool longPressArmed_ = false;
    juce::int64 longPressStartTime_ = 0;
    int longPressTouchId_ = -1;
    
    juce::ListenerList<Listener> listeners_;
    
    void detectGesture();
    void updateGesture();
    void endGesture();
    void startLongPressTimer(int touchId);
    void cancelLongPressTimer();
    void checkLongPress(juce::int64 currentTime);
    float calculatePinchScale() const;
    float calculateRotation() const;
    juce::Point<float> calculateCentroid() const;
    juce::Point<float> calculateVelocity() const;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GestureRecognizer)
};

} // namespace DAW
