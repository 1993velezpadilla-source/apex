#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <memory>

namespace DAW
{
// Single application-wide persisted choice. APEX Radial is the default for
// new users, independent of OS/device type. A classic JUCE/OS-look menu is
// always available. Keep storage separate from session state.
class ApexContextMenuStyle final
{
public:
    enum class Mode { Classic = 0, Radial = 1 };

    static ApexContextMenuStyle& get()
    {
        static ApexContextMenuStyle instance;
        return instance;
    }

    Mode getMode() const noexcept
    {
        return static_cast<Mode>(mode_.load(std::memory_order_relaxed));
    }

    void setMode(Mode mode)
    {
        mode_.store(static_cast<int>(mode), std::memory_order_relaxed);
        if (settings_ != nullptr)
        {
            settings_->setValue("contextMenuStyle", static_cast<int>(mode));
            settings_->saveIfNeeded();
        }
    }

private:
    ApexContextMenuStyle()
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "APEX Context Menus";
        options.folderName = "APEX";
        options.filenameSuffix = "settings";
        options.osxLibrarySubFolder = "Application Support";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        settings_ = std::make_unique<juce::PropertiesFile>(options);
        mode_.store(settings_->getIntValue("contextMenuStyle", 1) == 0
                        ? 0 : 1, std::memory_order_relaxed);
    }

    std::unique_ptr<juce::PropertiesFile> settings_;
    std::atomic<int> mode_ { 1 };
};
} // namespace DAW
