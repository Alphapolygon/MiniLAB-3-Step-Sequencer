#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>

struct StepData
{
    bool isActive = false;
    float velocity = 0.8f;      // 0..1
    float probability = 1.0f;   // 0..1
    float gate = 0.75f;         // 0..1
    float shift = 0.0f;         // -1..1, fraction of half-step
    float swing = 0.0f;         // 0..1, additional delay on odd steps
    int repeats = 1;            // 1..4
};

struct PadColor
{
    uint8_t r = 0, g = 0, b = 0;

    bool operator!=(const PadColor& other) const
    {
        return r != other.r || g != other.g || b != other.b;
    }
};

class MiniLAB3StepSequencerAudioProcessor : public juce::AudioProcessor,
                                            public juce::Timer
{
public:
    static constexpr int numTracks = 16;
    static constexpr int numSteps = 32;
    static constexpr int numPages = 4;
    static constexpr int stepsPerPage = 8;
    static constexpr int numPatterns = 10;
    static constexpr float minMicroTimingMs = -20.0f;
    static constexpr float maxMicroTimingMs = 20.0f;

    MiniLAB3StepSequencerAudioProcessor();
    ~MiniLAB3StepSequencerAudioProcessor() override;

    int getGeneralMidiNote(int t);

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
    void updateTrackLength(int patternIndex, int trackIndex);
    int getTrackLength(int trackIndex) const;
    int getTrackLength(int patternIndex, int trackIndex) const;

    StepData getStepData(int trackIndex, int stepIndex) const;
    StepData getStepData(int patternIndex, int trackIndex, int stepIndex) const;
    void setStepData(int trackIndex, int stepIndex, const StepData& step);
    void setStepData(int patternIndex, int trackIndex, int stepIndex, const StepData& step);
    void toggleStepActive(int trackIndex, int stepIndex, float velocityIfEnabled = 0.8f);
    void clearCurrentPageForCurrentInstrument();

    void setCurrentPattern(int patternIndex);
    int getCurrentPattern() const;
    juce::String getPatternName(int patternIndex) const;

    void setLoopSection(int sectionIndex);
    int getLoopSection() const;

    void openHardwareOutput();
    void resetHardwareState();
    bool isHardwareConnected() const { return hardwareOutput != nullptr; }
    void requestLedRefresh();
    void timerCallback() override;

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* masterVolParam = nullptr;
    std::atomic<float>* swingParam = nullptr;
    std::atomic<float>* muteParams[numTracks]{};
    std::atomic<float>* soloParams[numTracks]{};
    std::atomic<float>* noteParams[numTracks]{};
    std::atomic<float>* nudgeParams[numTracks]{};

    mutable juce::CriticalSection stateLock;
    std::array<std::array<std::array<StepData, numSteps>, numTracks>, numPatterns> patterns{};
    float lastFiredVelocity[numTracks][numSteps]{};
    int patternTrackLengths[numPatterns][numTracks]{};
    juce::String patternNames[numPatterns];
    juce::String instrumentNames[numTracks];

    std::atomic<int> currentInstrument{ 0 };
    std::atomic<int> currentPage{ 0 };
    std::atomic<int> currentPattern{ 0 };
    std::atomic<int> activeLoopSection{ -1 };
    std::atomic<int> global16thNote{ -1 };
    std::atomic<bool> pageChangedTrigger{ false };
    std::atomic<double> lastKnownBpm{ 120.0 };

private:
    std::unique_ptr<juce::MidiOutput> hardwareOutput;
    bool isAttemptingConnection = false;
    std::atomic<int> ledRefreshCountdown{ 0 };
    PadColor lastPadColor[stepsPerPage];
    juce::Random probabilityRandom;

    void handleMidiInput(const juce::MidiMessage& msg, juce::MidiBuffer& midiMessages);
    void updateHardwareLEDs(bool forceOverwrite);
    uint8_t getHardwarePadId(int softwareIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniLAB3StepSequencerAudioProcessor)
};
