#include "ArrangementEditor/ArrangementViewCore.h"

class MyDAWMainWindow : public juce::Component
{
public:
    MyDAWMainWindow()
    {
        addAndMakeVisible(m_arrangement);
        
        // Demo: Create clips
        auto clip1 = ArrangementEditor::ArrangementClipModel::createNew(0, 0.0, 4.0);
        clip1.clipName = "Drums";
        clip1.colour = juce::Colour(0xFFE07B39);
        
        auto clip2 = ArrangementEditor::ArrangementClipModel::createNew(1, 2.0, 6.0);
        clip2.clipName = "Bass";
        clip2.colour = juce::Colour(0xFF4EC94E);
        
        m_arrangement.getClipState().addClip(clip1);
        m_arrangement.getClipState().addClip(clip2);
    }
    
    void resized() override
    {
        m_arrangement.setBounds(getLocalBounds());
    }

private:
    ArrangementEditor::ArrangementViewCore m_arrangement;
};