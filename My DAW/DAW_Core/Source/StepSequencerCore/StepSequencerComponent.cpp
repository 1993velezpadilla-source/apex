#include "StepSequencerComponent.h"

namespace DAW {

StepSequencerComponent::StepSequencerComponent()
    : header_(model_)
{
    addAndMakeVisible(header_);
    model_.addChangeListener(this);

    // Wire header callbacks
    header_.onAddChannel = [this]() {
        model_.addChannel();
    };
    header_.onRemoveChannel = [this]() {
        if (model_.getLaneCount() > 0)
            model_.removeChannel(model_.getLaneCount() - 1);
    };

    // Seed default drum lanes so the sequencer is immediately usable
    model_.addChannel("Kick");
    model_.addChannel("Snare");
    model_.addChannel("HiHat");
    model_.addChannel("Clap");

    rebuildChannelStrips();
}

StepSequencerComponent::~StepSequencerComponent() {
    model_.removeChangeListener(this);
}

void StepSequencerComponent::rebuildChannelStrips() {
    channelStrips_.clear();
    const auto& channels = model_.getChannels();
    for (int i = 0; i < channels.size(); ++i)
    {
        auto* strip = new StepSequencerChannelStrip(model_, i);
        strip->onSampleLoaded = [this](int channelIndex, const juce::String& filePath)
        {
            if (drumSampler_)
            {
                // Load sample into drum sampler pad (pad index = channel index)
                drumSampler_->loadSample(channelIndex, filePath);
            }
        };
        addAndMakeVisible(strip);
        channelStrips_.add(strip);
    }
    resized();
}

void StepSequencerComponent::changeListenerCallback(juce::ChangeBroadcaster*) {
    rebuildChannelStrips();
}

void StepSequencerComponent::setCurrentStep(int step) {
    for (auto* strip : channelStrips_)
        strip->setCurrentStep(step);
}

void StepSequencerComponent::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1a1a2e));
}

void StepSequencerComponent::resized() {
    auto area = getLocalBounds();
    header_.setBounds(area.removeFromTop(60));

    int y = area.getY();
    const int stripHeight = 40;
    const int controlsWidth = 160;
    for (auto* strip : channelStrips_) {
        strip->setBounds(area.getX(), y, area.getWidth(), stripHeight);
        y += stripHeight;
    }

    if (!channelStrips_.isEmpty()) {
        auto gridArea = juce::Rectangle<int>(area.getX() + controlsWidth, 0,
            area.getWidth() - controlsWidth, 60);
        header_.setGridArea(gridArea);
        header_.repaint();
    }
}

} // namespace DAW
