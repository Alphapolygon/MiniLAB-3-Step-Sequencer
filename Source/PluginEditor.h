#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// ==============================================================================
// MAIN EDITOR CLASS
// ==============================================================================

class MiniLAB3StepSequencerAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    MiniLAB3StepSequencerAudioProcessorEditor(MiniLAB3StepSequencerAudioProcessor&);
    ~MiniLAB3StepSequencerAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Timer handles UI repaints, hardware connection polling, and flash animations */
    void timerCallback() override;

    /** Maps mouse clicks to grid interactions */
    void mouseDown(const juce::MouseEvent& e) override;

private:
    MiniLAB3StepSequencerAudioProcessor& audioProcessor;

    // Animation states
    float pageFlashAlpha = 0.0f;

    // --- UI Assets ---
    // Note: These expect images to be compiled into BinaryData via Projucer/CMake
    juce::Image backgroundImage;
    juce::Image inactiveBgA;       // Background for steps 1-8, 17-24
    juce::Image inactiveBgB;       // Background for steps 9-16, 25-32
    juce::Image activeKeys[16];    // Unique active colors for each of the 16 tracks

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniLAB3StepSequencerAudioProcessorEditor)
};