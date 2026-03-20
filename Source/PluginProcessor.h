#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>

#include "PluginProcessorTypes.h"
#include "PluginProcessorHelpers.h"

class MiniLAB3StepSequencerAudioProcessor : public juce::AudioProcessor,
    public juce::Timer
{
public:
    MiniLAB3StepSequencerAudioProcessor();
    ~MiniLAB3StepSequencerAudioProcessor() override;

    int getGeneralMidiNote(int trackIndex);

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const juce::AudioProcessor::BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "MiniLAB3Seq"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    void updateTrackLength(int trackIndex);
    void openHardwareOutput();
    void resetHardwareState();
    bool isHardwareConnected() const { return hardwareOutput != nullptr; }
    void requestLedRefresh();
    void timerCallback() override;

    static constexpr float minMicroTimingMs = -20.0f;
    static constexpr float maxMicroTimingMs = 20.0f;

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Automation Parameters
    std::atomic<float>* masterVolParam = nullptr;
    std::atomic<float>* swingParam = nullptr;
    std::atomic<float>* muteParams[MiniLAB3Seq::kNumTracks] = {};
    std::atomic<float>* soloParams[MiniLAB3Seq::kNumTracks] = {};
    std::atomic<float>* noteParams[MiniLAB3Seq::kNumTracks] = {};
    std::atomic<float>* nudgeParams[MiniLAB3Seq::kNumTracks] = {};
    std::atomic<bool> initialising { true };

    // Lock-Free Double Buffering State
    using MatrixSnapshot = StepData[MiniLAB3Seq::kNumTracks][MiniLAB3Seq::kNumSteps];
    void modifySequencerState(const std::function<void(MatrixSnapshot&)>& modifier);
    const MatrixSnapshot& getActiveMatrix() const;

    std::atomic<int> trackMidiChannels[MiniLAB3Seq::kNumTracks];
    float lastFiredVelocity[MiniLAB3Seq::kNumTracks][MiniLAB3Seq::kNumSteps];
    int trackLengths[MiniLAB3Seq::kNumTracks] = {};
    juce::String instrumentNames[MiniLAB3Seq::kNumTracks];

    std::atomic<int> currentInstrument{ 0 };
    std::atomic<int> currentPage{ 0 };
    std::atomic<int> global16thNote{ -1 };
    std::atomic<bool> pageChangedTrigger{ false };
    std::atomic<int> activePatternIndex{ 0 };

    std::atomic<double> currentBpm{ 120.0 };
    std::atomic<bool> isPlaying{ false };
    std::atomic<uint32_t> uiStateVersion{ 1 };

    juce::String fullUiStateJson;

    void setStepDataFromVar(const juce::var& stateVar);
    juce::var buildCurrentPatternStateVar() const;
    juce::String buildFullUiStateJsonForEditor() const;
    uint32_t getUiStateVersion() const noexcept { return uiStateVersion.load(); }
    void requestUiStateBroadcast() noexcept { markUiStateDirty(); }
    void markUiStateDirty() noexcept;

private:
    mutable juce::CriticalSection writerLock;
    std::atomic<int> activeMatrixIndex{ 0 };
    StepData sequencerMatrix[2][MiniLAB3Seq::kNumTracks][MiniLAB3Seq::kNumSteps];

    std::unique_ptr<juce::MidiOutput> hardwareOutput;
    std::unique_ptr<ControllerProfile> activeController;
    bool isAttemptingConnection = false;
    std::atomic<int> ledRefreshCountdown{ 0 };

    int lastProcessedStep = -1;
    static constexpr size_t MaxMidiEvents = 4096;
    std::vector<ScheduledMidiEvent> eventQueue;
    std::atomic<int> droppedNotesCount{ 0 };

    void scheduleMidiEvent(double ppqTime, const juce::MidiMessage& msg);
    void handleMidiInput(const juce::MidiMessage& msg, juce::MidiBuffer& midiMessages);
    void updateHardwareLEDs(bool forceOverwrite);
    void setParameterFromPlainValue(const juce::String& parameterId, float plainValue);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniLAB3StepSequencerAudioProcessor)
};