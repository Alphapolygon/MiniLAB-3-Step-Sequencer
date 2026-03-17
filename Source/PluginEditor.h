#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class MiniLAB3StepSequencerAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    MiniLAB3StepSequencerAudioProcessorEditor(MiniLAB3StepSequencerAudioProcessor&);
    ~MiniLAB3StepSequencerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    MiniLAB3StepSequencerAudioProcessor& audioProcessor;
    float pageFlashAlpha = 0.0f;
    float nudgeDragStartValue = 0.0f;
    int nudgeDragTrack = -1;
    int nudgeDragStartX = 0;
    float nudgeDragPixelsForFullRange = 120.0f;

    juce::Image backgroundImage;
    juce::Image inactiveBgA;
    juce::Image inactiveBgB;
    juce::Image activeKeys[16];
    juce::Image nudgeKnobStrip;
    int nudgeKnobFrames = 15;



    juce::Rectangle<float> getNudgeKnobBounds(int trackIndex) const;
    void setTrackNudgeFromMs(int trackIndex, float newValueMs);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniLAB3StepSequencerAudioProcessorEditor)
};
