#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class MiniLAB3StepSequencerAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    MiniLAB3StepSequencerAudioProcessorEditor(MiniLAB3StepSequencerAudioProcessor&);
    ~MiniLAB3StepSequencerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override {} // Handled by WebBrowserComponent
    void resized() override;
    void timerCallback() override;

private:
    MiniLAB3StepSequencerAudioProcessor& audioProcessor;
    juce::WebBrowserComponent webComponent;

    double lastBpm = 0.0;
    bool lastIsPlaying = false;
    int lastStep = -1;
    uint32_t lastUiStateVersion = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniLAB3StepSequencerAudioProcessorEditor)
};
