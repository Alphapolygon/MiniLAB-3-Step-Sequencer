#pragma once

#include <JuceHeader.h>
#include <array>
#include "PluginProcessor.h"

class MiniLAB3StepSequencerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                                  public juce::Timer
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
    enum class FooterLane
    {
        velocity = 0,
        gate,
        probability,
        shift,
        swing
    };

    struct Theme
    {
        juce::String name;
        juce::Colour bg;
        juce::Colour panel;
        juce::Colour sidebar;
        juce::Colour border;
        juce::Colour text;
        juce::Colour accent;
        std::array<juce::Colour, 5> laneColours;
    };

    MiniLAB3StepSequencerAudioProcessor& audioProcessor;

    int selectedTrack = 0;
    int themeIndex = 0;
    int previewStep = 0;
    bool previewRunning = false;
    double lastPreviewAdvanceMs = 0.0;
    float pageFlashAlpha = 0.0f;

    std::unique_ptr<juce::FileChooser> midiExportChooser;

    int footerDragStep = -1;
    bool footerDrawing = false;
    FooterLane footerLane = FooterLane::velocity;

    static const std::array<Theme, 6>& getThemes();
    const Theme& getTheme() const;

    juce::Rectangle<int> getHeaderBounds() const;
    juce::Rectangle<int> getFooterBounds() const;
    juce::Rectangle<int> getSidebarBounds() const;
    juce::Rectangle<int> getMainBounds() const;
    juce::Rectangle<int> getGridBounds() const;
    juce::Rectangle<int> getRowBounds(int track) const;
    juce::Rectangle<int> getTrackLabelBounds(int track) const;
    juce::Rectangle<int> getNoteBounds(int track) const;
    juce::Rectangle<int> getMuteBounds(int track) const;
    juce::Rectangle<int> getSoloBounds(int track) const;
    juce::Rectangle<int> getStepBounds(int track, int step) const;

    juce::Rectangle<int> getTransportPlayBounds() const;
    juce::Rectangle<int> getTransportStopBounds() const;
    juce::Rectangle<int> getTempoBounds() const;
    juce::Rectangle<int> getLoopButtonBounds(int section) const; // -1 = all

    juce::Rectangle<int> getPatternButtonBounds(int index) const;
    juce::Rectangle<int> getThemeButtonBounds(int index) const;
    juce::Rectangle<int> getExportButtonBounds() const;

    juce::Rectangle<int> getFooterTabButtonBounds(int index) const;
    juce::Rectangle<int> getFooterGraphBounds() const;
    juce::Rectangle<int> getFooterRepeatGridBounds() const;
    juce::Rectangle<int> getFooterBarBounds(int step) const;
    juce::Rectangle<int> getFooterRepeatButtonBounds(int step, int repeatValue) const;

    int getDisplayStepForTrack(int track) const;
    int getCurrentTrackPageLength(int track) const;
    float getFooterLaneValueAsNormalized(const StepData& step) const;
    float getFooterLaneValueFromPoint(const juce::Point<float>& position) const;
    void applyFooterLaneValueAtPoint(const juce::Point<float>& position);

    void drawHeader(juce::Graphics& g);
    void drawGrid(juce::Graphics& g);
    void drawSidebar(juce::Graphics& g);
    void drawFooter(juce::Graphics& g);
    void drawKnob(juce::Graphics& g, juce::Rectangle<float> bounds, float normalized, juce::Colour colour) const;
    void drawPillButton(juce::Graphics& g, juce::Rectangle<int> bounds, const juce::String& text,
                        bool active, juce::Colour activeColour, juce::Colour inactiveColour,
                        juce::Colour textColour) const;
    void exportCurrentPatternToMidi(int trackIndex = -1);

    static juce::String getMidiNoteName(int note);
    static juce::String getFooterLaneName(FooterLane lane);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniLAB3StepSequencerAudioProcessorEditor)
};
