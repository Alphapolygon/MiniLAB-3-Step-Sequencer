#pragma once
#include <JuceHeader.h>
#include <atomic>

// ==============================================================================
// DATA STRUCTURES
// ==============================================================================

/** Represents a single step in the sequencer. */
struct StepData {
    bool isActive = false;
    float velocity = 0.8f;
    float probability = 1.0f; // Ready for future expansion (e.g., generative sequencing)
};

/** Tracks exact RGB values to prevent MIDI flooding to the hardware. */
struct PadColor {
    uint8_t r = 0, g = 0, b = 0;

    // Helper to check if the color has changed since the last frame
    bool operator!=(const PadColor& other) const {
        return r != other.r || g != other.g || b != other.b;
    }
};

// ==============================================================================
// MAIN PROCESSOR CLASS
// ==============================================================================

class MiniLAB3StepSequencerAudioProcessor : public juce::AudioProcessor, public juce::Timer
{
public:
    MiniLAB3StepSequencerAudioProcessor();
    ~MiniLAB3StepSequencerAudioProcessor() override;

    // --- Standard JUCE Processor Overrides ---
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

    // --- Sequencer Logic ---
    /** Shrinks or grows the track length (8, 16, 24, 32) based on the highest active note. */
    void updateTrackLength(int trackIndex);

    // --- Hardware Interface ---
    void openHardwareOutput();
    void resetHardwareState();
    bool isHardwareConnected() const { return hardwareOutput != nullptr; }

    /** Requests an immediate rewrite of the hardware LEDs (used to override default behavior) */
    void requestLedRefresh();

    /** High-speed loop to handle LED fading and state syncing */
    void timerCallback() override;

    // ==============================================================================
    // SHARED STATE (Thread-safe communication between Audio Thread and UI/Hardware)
    // ==============================================================================

    mutable juce::CriticalSection stateLock;          // Locks the matrix during read/writes
    StepData sequencerMatrix[16][32];                 // The core 16-track, 32-step grid
    float lastFiredVelocity[16][32];                  // Used for UI visual feedback decay
    int trackLengths[16];                             // Dynamic track lengths
    juce::String instrumentNames[16];                 // Track names

    // Atomics for safe cross-thread reading
    std::atomic<int> currentInstrument{ 0 };          // Which track is focused in the hardware
    std::atomic<int> currentPage{ 0 };                // Which 8-step block (0-3) is visible on the pads
    std::atomic<int> global16thNote{ -1 };            // The global playhead position
    std::atomic<bool> pageChangedTrigger{ false };    // Tells UI to flash the page-change indicator
    std::atomic<float> masterVolume{ 0.8f };          // Sequencer global velocity multiplier
    std::atomic<float> swingAmount{ 0.0f };           // Delay offset for even 16th notes

private:
    std::unique_ptr<juce::MidiOutput> hardwareOutput;
    bool isAttemptingConnection = false;
    std::atomic<int> ledRefreshCountdown{ 0 };        // Counter to force-overwrite hardware LEDs

    // Smart Color Caching to prevent USB flooding
    PadColor lastPadColor[8];

    // --- Internal Helpers ---
    void handleMidiInput(const juce::MidiMessage& msg, juce::MidiBuffer& midiMessages);
    int getGeneralMidiNote(int trackIndex);
    void updateHardwareLEDs(bool forceOverwrite);

    /** Translates the software pad index (0-7) to the physical MiniLab 3 Pad ID */
    uint8_t getHardwarePadId(int softwareIndex);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MiniLAB3StepSequencerAudioProcessor)
};