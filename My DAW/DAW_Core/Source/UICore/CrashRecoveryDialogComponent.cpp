#include "CrashRecoveryDialogComponent.h"

namespace DAW {

CrashRecoveryDialogComponent::CrashRecoveryDialogComponent(const RecoveryInfo& info)
    : info_(info)
{
    auto setupLabel = [](juce::Label& l, const juce::String& text, float fontSize,
                          bool bold, juce::Colour colour)
    {
        l.setText(text, juce::dontSendNotification);
        l.setFont(juce::Font(fontSize, bold ? juce::Font::bold : juce::Font::plain));
        l.setColour(juce::Label::textColourId, colour);
        l.setJustificationType(juce::Justification::centredLeft);
    };

    setupLabel(titleLabel_,
               "APEX found an autosaved recovery project from your last session.",
               15.0f, true, juce::Colours::white);

    auto projName = info_.wasUnsavedProject
                      ? juce::String("(Unsaved project session)")
                      : info_.originalProjectFile.getFileNameWithoutExtension();
    setupLabel(projectLabel_, "Project:  " + projName, 13.0f, false, juce::Colour(0xFFCCCCCC));

    auto origPath = info_.wasUnsavedProject
                      ? juce::String("Unsaved project")
                      : info_.originalProjectFile.getFullPathName();
    setupLabel(pathLabel_, "Original: " + origPath, 12.0f, false, juce::Colour(0xFF999999));

    auto timeStr = info_.autosaveTime.toString(true, true, false, true);
    setupLabel(timestampLabel_, "Autosave: " + timeStr, 12.0f, false, juce::Colour(0xFF999999));

    auto elapsed  = juce::Time::getCurrentTime() - info_.autosaveTime;
    auto elapsedStr = (elapsed.inMinutes() < 60)
                        ? juce::String((int)elapsed.inMinutes()) + " minutes ago"
                        : juce::String((int)elapsed.inHours())   + " hours ago";
    setupLabel(ageLabel_, "Age:      " + elapsedStr, 12.0f, false, juce::Colour(0xFF999999));

    auto sizeKb = (int)(info_.autosaveFile.getSize() / 1024);
    setupLabel(sizeLabel_, "Size:     " + juce::String(sizeKb) + " KB", 12.0f, false, juce::Colour(0xFF999999));

    // Buttons
    recoverBtn_.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xFF2D6A4F));
    recoverBtn_.setColour(juce::TextButton::textColourOffId,  juce::Colours::white);
    origBtn_.setColour(juce::TextButton::buttonColourId,      juce::Colour(0xFF1B4F72));
    origBtn_.setColour(juce::TextButton::textColourOffId,     juce::Colours::white);
    showFolderBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF444444));
    showFolderBtn_.setColour(juce::TextButton::textColourOffId,juce::Colours::white);
    discardBtn_.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xFF6B2737));
    discardBtn_.setColour(juce::TextButton::textColourOffId,  juce::Colours::white);

    if (!info_.originalProjectFile.existsAsFile())
        origBtn_.setEnabled(false);

    recoverBtn_.onClick = [this]
    {
        if (onRecoverAutosave) onRecoverAutosave(info_.autosaveFile);
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(1);
    };

    origBtn_.onClick = [this]
    {
        if (onOpenOriginal) onOpenOriginal(info_.originalProjectFile);
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(2);
    };

    showFolderBtn_.onClick = [this]
    {
        info_.autosaveFile.getParentDirectory().revealToUser();
        // Don't close — user may still want to act
    };

    discardBtn_.onClick = [this]
    {
        if (onDiscardRecovery) onDiscardRecovery();
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };

    addAndMakeVisible(titleLabel_);
    addAndMakeVisible(projectLabel_);
    addAndMakeVisible(pathLabel_);
    addAndMakeVisible(timestampLabel_);
    addAndMakeVisible(ageLabel_);
    addAndMakeVisible(sizeLabel_);
    addAndMakeVisible(recoverBtn_);
    addAndMakeVisible(origBtn_);
    addAndMakeVisible(showFolderBtn_);
    addAndMakeVisible(discardBtn_);

    setSize(540, 320);
}

void CrashRecoveryDialogComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1A1A1A));

    g.setColour(juce::Colour(0xFFE07B39));
    g.fillRect(0, 0, getWidth(), 4);

    g.setColour(juce::Colour(0xFF333333));
    g.drawRect(getLocalBounds().reduced(1), 1);
}

void CrashRecoveryDialogComponent::resized()
{
    const int pad   = 20;
    const int lineH = 22;
    const int btnH  = 32;
    int y = pad + 8;

    titleLabel_.setBounds(pad, y, getWidth() - pad * 2, 36);
    y += 40;

    projectLabel_.setBounds(pad, y, getWidth() - pad * 2, lineH); y += lineH + 2;
    pathLabel_.setBounds(pad, y, getWidth() - pad * 2, lineH);    y += lineH + 2;
    timestampLabel_.setBounds(pad, y, getWidth() - pad * 2, lineH); y += lineH + 2;
    ageLabel_.setBounds(pad, y, getWidth() - pad * 2, lineH);     y += lineH + 2;
    sizeLabel_.setBounds(pad, y, getWidth() - pad * 2, lineH);    y += lineH + 12;

    const int btnW = (getWidth() - pad * 2 - 8 * 3) / 4;
    recoverBtn_.setBounds   (pad,                      y, btnW, btnH);
    origBtn_.setBounds      (pad + btnW + 8,           y, btnW, btnH);
    showFolderBtn_.setBounds(pad + (btnW + 8) * 2,     y, btnW, btnH);
    discardBtn_.setBounds   (pad + (btnW + 8) * 3,     y, btnW, btnH);
}

juce::DialogWindow* CrashRecoveryDialogComponent::showAsync(
    const RecoveryInfo& info,
    std::function<void(const juce::File&)> onRecover,
    std::function<void(const juce::File&)> onOpenOrig,
    std::function<void()>                  onDiscard)
{
    auto* content = new CrashRecoveryDialogComponent(info);
    content->onRecoverAutosave = std::move(onRecover);
    content->onOpenOriginal    = std::move(onOpenOrig);
    content->onDiscardRecovery = std::move(onDiscard);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(content);
    opts.dialogTitle              = juce::String (juce::CharPointer_UTF8 ("APEX \xe2\x80\x94 Recover Previous Session"));
    opts.dialogBackgroundColour   = juce::Colour(0xFF1A1A1A);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar        = false;
    opts.resizable                = false;

    // launchAsync registers the window with ModalComponentManager which
    // deletes it automatically on exitModalState — do NOT wrap in unique_ptr.
    return opts.launchAsync();
}

} // namespace DAW
