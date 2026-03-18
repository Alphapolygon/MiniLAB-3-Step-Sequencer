#pragma once
#include <JuceHeader.h>
#include <atomic>

struct ScheduledMidiEvent {
    double ppqTime = 0.0;
    juce::MidiMessage message;
    bool isActive = false;
};

struct StepData {
    bool isActive = false;
    float velocity = 0.8f;    // 0.0 to 1.0
    float probability = 1.0f; // 0.0 to 1.0
    float gate = 0.75f;       // 0.0 to 1.0
    int repeats = 1;          // 1 to 4
    float shift = 0.5f;       // 0.0 to 1.0 (0.5 is center)
};

struct PadColor {
    uint8_t r = 0, g = 0, b = 0;
    bool operator!=(const PadColor& other) const {
        return r != other.r || g != other.g || b != other.b;
    }
};

class MiniLAB3StepSequencerAudioProcessor : public juce::AudioProcessor, public juce::Timer
{
public:
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
    void setCurrentProgram(int index) override {}
    const juce::String getProgramName(int index) override { return {}; }
    void changeProgramName(int index, const juce::String& newName) override {}

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

    std::atomic<float>* masterVolParam;
    std::atomic<float>* swingParam;
    std::atomic<float>* muteParams[16];
    std::atomic<float>* soloParams[16];
    std::atomic<float>* noteParams[16];
    std::atomic<float>* nudgeParams[16];

    mutable juce::CriticalSection stateLock;
    StepData sequencerMatrix[16][32];
    float lastFiredVelocity[16][32];
    int trackLengths[16];
    juce::String instrumentNames[16];

    std::atomic<int> currentInstrument{ 0 };
    std::atomic<int> currentPage{ 0 };
    std::atomic<int> global16thNote{ -1 };
    std::atomic<bool> pageChangedTrigger{ false };

    std::atomic<double> currentBpm{ 120.0 };
    std::atomic<bool> isPlaying{ false };

    juce::String fullUiStateJson;

    // JSON Parser for React UI
    void setStepDataFromJson(const juce::String& jsonStr);

private:
    std::unique_ptr<juce::MidiOutput> hardwareOutput;
    bool isAttemptingConnection = false;
    std::atomic<int> ledRefreshCountdown{ 0 };
    PadColor lastPadColor[8];

    // --- PLAYBACK TRACKING ---
    int lastProcessedStep = -1;
    ScheduledMidiEvent eventQueue[1024];
    void scheduleMidiEvent(double ppqTime, const juce::MidiMessage& msg);

    void handleMidiInput(const juce::MidiMessage& msg, juce::MidiBuffer& midiMessages);
    void updateHardwareLEDs(bool forceOverwrite);
    uint8_t getHardwarePadId(int softwareIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniLAB3StepSequencerAudioProcessor)
};