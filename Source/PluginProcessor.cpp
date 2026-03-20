#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout MiniLAB3StepSequencerAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>("master_vol", "Master Volume", 0.0f, 1.0f, 0.8f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("swing", "Swing", 0.0f, 1.0f, 0.0f));

    for (int i = 0; i < MiniLAB3Seq::kNumTracks; ++i)
    {
        const auto trackLabel = juce::String(i + 1);
        layout.add(std::make_unique<juce::AudioParameterBool>("mute_" + trackLabel, "Mute " + trackLabel, false));
        layout.add(std::make_unique<juce::AudioParameterBool>("solo_" + trackLabel, "Solo " + trackLabel, false));
        layout.add(std::make_unique<juce::AudioParameterInt>("note_" + trackLabel, "MIDI Note " + trackLabel, 0, 127, 36 + i));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "nudge_" + trackLabel,
            "Micro-Timing " + trackLabel,
            juce::NormalisableRange<float>(minMicroTimingMs, maxMicroTimingMs, 0.1f),
            0.0f));
    }

    return layout;
}

MiniLAB3StepSequencerAudioProcessor::MiniLAB3StepSequencerAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    masterVolParam = apvts.getRawParameterValue("master_vol");
    swingParam = apvts.getRawParameterValue("swing");

    eventQueue.resize(MaxMidiEvents);

    modifySequencerState([this](MatrixSnapshot& writeMatrix)
        {
            for (int i = 0; i < MiniLAB3Seq::kNumTracks; ++i)
            {
                instrumentNames[i] = juce::String(i + 1);
                trackMidiChannels[i].store(1, std::memory_order_relaxed); // Default all to Ch 1 initially

                const auto trackLabel = juce::String(i + 1);
                muteParams[i] = apvts.getRawParameterValue("mute_" + trackLabel);
                soloParams[i] = apvts.getRawParameterValue("solo_" + trackLabel);
                noteParams[i] = apvts.getRawParameterValue("note_" + trackLabel);
                nudgeParams[i] = apvts.getRawParameterValue("nudge_" + trackLabel);

                for (int step = 0; step < MiniLAB3Seq::kNumSteps; ++step)
                {
                    writeMatrix[i][step] = { false, 0.8f, 1.0f, 0.75f, 1, 0.5f, 0.0f };
                    lastFiredVelocity[i][step] = 0.0f;
                }
            }
        });

    for (int i = 0; i < MiniLAB3Seq::kNumTracks; ++i)
        updateTrackLength(i);

    //openHardwareOutput();
    startTimer(250);
}

MiniLAB3StepSequencerAudioProcessor::~MiniLAB3StepSequencerAudioProcessor()
{
    stopTimer();
    resetHardwareState();
}

void MiniLAB3StepSequencerAudioProcessor::modifySequencerState(const std::function<void(MatrixSnapshot&)>& modifier)
{
    const juce::ScopedLock lock(writerLock);
    const int activeIdx = activeMatrixIndex.load(std::memory_order_acquire);
    const int inactiveIdx = 1 - activeIdx;

    // Deep copy current state to the back buffer
    for (int t = 0; t < MiniLAB3Seq::kNumTracks; ++t) {
        for (int s = 0; s < MiniLAB3Seq::kNumSteps; ++s) {
            sequencerMatrix[inactiveIdx][t][s] = sequencerMatrix[activeIdx][t][s];
        }
    }

    // Apply UI or Hardware edits
    modifier(sequencerMatrix[inactiveIdx]);

    // Atomically flip the back buffer to the front
    activeMatrixIndex.store(inactiveIdx, std::memory_order_release);
}

const MiniLAB3StepSequencerAudioProcessor::MatrixSnapshot& MiniLAB3StepSequencerAudioProcessor::getActiveMatrix() const
{
    return sequencerMatrix[activeMatrixIndex.load(std::memory_order_acquire)];
}

int MiniLAB3StepSequencerAudioProcessor::getGeneralMidiNote(int trackIndex) { return 36 + trackIndex; }

void MiniLAB3StepSequencerAudioProcessor::updateTrackLength(int trackIndex)
{
    const auto& matrix = getActiveMatrix();
    int maxActiveStep = -1;

    for (int step = MiniLAB3Seq::kNumSteps - 1; step >= 0; --step)
    {
        if (matrix[trackIndex][step].isActive)
        {
            maxActiveStep = step;
            break;
        }
    }

    if (maxActiveStep >= 24)      trackLengths[trackIndex] = 32;
    else if (maxActiveStep >= 16) trackLengths[trackIndex] = 24;
    else if (maxActiveStep >= 8)  trackLengths[trackIndex] = 16;
    else                          trackLengths[trackIndex] = 8;
}

void MiniLAB3StepSequencerAudioProcessor::markUiStateDirty() noexcept
{
    uiStateVersion.fetch_add(1, std::memory_order_relaxed);
}

void MiniLAB3StepSequencerAudioProcessor::setParameterFromPlainValue(const juce::String& parameterId, float plainValue)
{
    if (auto* parameter = apvts.getParameter(parameterId))
    {
        const float normalizedValue = juce::jlimit(0.0f, 1.0f, parameter->convertTo0to1(plainValue));
        const float currentValue = parameter->getValue();

        if (currentValue > normalizedValue - 0.0001f && currentValue < normalizedValue + 0.0001f)
            return;

        parameter->setValueNotifyingHost(normalizedValue);
    }
}

void MiniLAB3StepSequencerAudioProcessor::releaseResources() {}
bool MiniLAB3StepSequencerAudioProcessor::isBusesLayoutSupported(const juce::AudioProcessor::BusesLayout& layouts) const { return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
juce::AudioProcessorEditor* MiniLAB3StepSequencerAudioProcessor::createEditor() { return new MiniLAB3StepSequencerAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MiniLAB3StepSequencerAudioProcessor(); }