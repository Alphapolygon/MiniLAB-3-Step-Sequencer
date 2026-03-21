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
    apvts(*this, nullptr, "PARAMETERS", createParameterLayout()),
    playbackRandom(0x31337) // Initial deterministic seed
{
    masterVolParam = apvts.getRawParameterValue("master_vol");
    swingParam = apvts.getRawParameterValue("swing");

    modifySequencerState([this](MatrixSnapshot& writeMatrix)
        {
            for (int p = 0; p < MiniLAB3Seq::kNumPatterns; ++p)
            {
                for (int t = 0; t < MiniLAB3Seq::kNumTracks; ++t)
                {
                    if (p == 0) {
                        instrumentNames[t] = juce::String(t + 1);
                        trackMidiChannels[t].store(1, std::memory_order_relaxed);
                        muteParams[t] = apvts.getRawParameterValue("mute_" + juce::String(t + 1));
                        soloParams[t] = apvts.getRawParameterValue("solo_" + juce::String(t + 1));
                        noteParams[t] = apvts.getRawParameterValue("note_" + juce::String(t + 1));
                        nudgeParams[t] = apvts.getRawParameterValue("nudge_" + juce::String(t + 1));
                    }

                    for (int s = 0; s < MiniLAB3Seq::kNumSteps; ++s)
                    {
                        writeMatrix[p][t][s] = { false, 0.8f, 1.0f, 0.75f, 1, 0.5f, 0.0f };
                        if (p == 0) lastFiredVelocity[t][s] = 0.0f;
                    }
                }
            }
        });

    for (int i = 0; i < MiniLAB3Seq::kNumTracks; ++i)
        updateTrackLength(i);

    markUiStateDirty();
    requestLedRefresh();
    startTimer(250);
    initialising.store(false, std::memory_order_release);
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

    for (int p = 0; p < MiniLAB3Seq::kNumPatterns; ++p)
        for (int t = 0; t < MiniLAB3Seq::kNumTracks; ++t)
            for (int s = 0; s < MiniLAB3Seq::kNumSteps; ++s)
                sequencerMatrix[inactiveIdx][p][t][s] = sequencerMatrix[activeIdx][p][t][s];

    modifier(sequencerMatrix[inactiveIdx]);

    activeMatrixIndex.store(inactiveIdx, std::memory_order_release);
}

const MiniLAB3StepSequencerAudioProcessor::MatrixSnapshot& MiniLAB3StepSequencerAudioProcessor::getActiveMatrix() const
{
    return sequencerMatrix[activeMatrixIndex.load(std::memory_order_acquire)];
}

int MiniLAB3StepSequencerAudioProcessor::getGeneralMidiNote(int trackIndex)
{
    return 36 + trackIndex;
}

void MiniLAB3StepSequencerAudioProcessor::updateTrackLength(int trackIndex)
{
    const auto& matrix = getActiveMatrix();
    const int pIdx = activePatternIndex.load(std::memory_order_acquire);
    int maxActiveStep = -1;

    for (int step = MiniLAB3Seq::kNumSteps - 1; step >= 0; --step)
    {
        if (matrix[pIdx][trackIndex][step].isActive)
        {
            maxActiveStep = step;
            break;
        }
    }

    int length = 8;
    if (maxActiveStep >= 24)      length = 32;
    else if (maxActiveStep >= 16) length = 24;
    else if (maxActiveStep >= 8)  length = 16;

    trackLengths[trackIndex].store(length, std::memory_order_release);
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

void MiniLAB3StepSequencerAudioProcessor::applyPendingParameterUpdates()
{
    const float pendingMaster = pendingMasterVolNormalized.exchange(-1.0f, std::memory_order_acq_rel);
    if (pendingMaster >= 0.0f)
    {
        if (auto* parameter = apvts.getParameter("master_vol"))
            parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, pendingMaster));
    }

    const float pendingSwing = pendingSwingNormalized.exchange(-1.0f, std::memory_order_acq_rel);
    if (pendingSwing >= 0.0f)
    {
        if (auto* parameter = apvts.getParameter("swing"))
            parameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, pendingSwing));
    }
}

void MiniLAB3StepSequencerAudioProcessor::releaseResources() {}
bool MiniLAB3StepSequencerAudioProcessor::isBusesLayoutSupported(const juce::AudioProcessor::BusesLayout& layouts) const { return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
juce::AudioProcessorEditor* MiniLAB3StepSequencerAudioProcessor::createEditor() { return new MiniLAB3StepSequencerAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MiniLAB3StepSequencerAudioProcessor(); }