#pragma once
#include <JuceHeader.h>

namespace DAW {

/**
 * IBubblegumTaskbarHost
 *
 * Interface that any minimizable panel implements. The Bubblegum Taskbar uses
 * this interface to read the chip's label/accent colour and to dispatch the
 * three lifecycle actions back to the panel:
 *
 *   restoreFromTaskbar()  - make the panel visible (chip click while idle)
 *   minimizeFromTaskbar() - hide the panel (chip click while active)
 *   closeFromTaskbar()    - destroy the panel completely (chip X click)
 *
 * The host is identified by its pointer; each panel instance is one host.
 * When a panel is destroyed it MUST call BubblegumTaskbarCore::unregisterHost()
 * in its destructor to remove its chip and avoid dangling pointers.
 */
class IBubblegumTaskbarHost
{
public:
    virtual ~IBubblegumTaskbarHost() = default;

    virtual juce::String getChipLabel()        const = 0;
    virtual juce::Colour getChipAccentColour() const = 0;

    virtual void restoreFromTaskbar()  = 0;
    virtual void minimizeFromTaskbar() = 0;
    virtual void closeFromTaskbar()    = 0;
};

} // namespace DAW
