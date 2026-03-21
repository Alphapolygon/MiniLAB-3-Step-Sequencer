#include "PluginProcessor.h"

namespace
{
    struct PadColor
    {
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;

        bool operator!=(const PadColor& other) const
        {
            return r != other.r || g != other.g || b != other.b;
        }
    };

    class ArturiaMiniLab3Profile : public ControllerProfile
    {
    public:
        juce::String getDeviceNameSubstring() const override { return "Minilab3"; }

        void initializeHardware(juce::MidiOutput* out) override
        {
            for (int pad = 0; pad < MiniLAB3Seq::kPadsPerPage; ++pad)
                out->sendMessageNow(makePadColorSysex(getHardwarePadId(pad), 0, 0, 0));
        }

        void resetHardware(juce::MidiOutput* out) override
        {
            initializeHardware(out);
        }

        void updateLEDs(juce::MidiOutput* out, MiniLAB3StepSequencerAudioProcessor& processor, bool forceOverwrite) override
        {
            const int page = processor.currentPage.load(std::memory_order_acquire);
            const int startStep = page * MiniLAB3Seq::kPadsPerPage;
            const int instrument = processor.currentInstrument.load(std::memory_order_acquire);
            const int current16th = processor.global16thNote.load(std::memory_order_acquire);
            const int trackLength = processor.trackLengths[instrument].load(std::memory_order_acquire);
            const int playingStep = (current16th >= 0 && trackLength > 0) ? (current16th % trackLength) : -1;

            const int pIdx = processor.activePatternIndex.load(std::memory_order_acquire);
            const auto& matrix = processor.getActiveMatrix()[pIdx];

            for (int pad = 0; pad < MiniLAB3Seq::kPadsPerPage; ++pad)
            {
                const int step = startStep + pad;
                const bool active = matrix[instrument][step].isActive;
                const float velocity = matrix[instrument][step].velocity;
                const bool isPlayhead = (step == playingStep);

                uint8_t r = 0, g = 0, b = 0;

                if (isPlayhead)
                {
                    r = active ? 127 : 15;
                    g = active ? 127 : 15;
                    b = active ? 127 : 15;
                }
                else if (active)
                {
                    const float brightness = 0.1f + (velocity * 0.9f);
                    const uint8_t c = static_cast<uint8_t>(127.0f * brightness);

                    if (page == 0) { r = 0; g = c; b = c; }
                    else if (page == 1) { r = c; g = 0; b = c; }
                    else if (page == 2) { r = c; g = c; b = 0; }
                    else if (page == 3) { r = c; g = c; b = c; }
                }

                const PadColor newColor{ r, g, b };
                if (forceOverwrite || newColor != lastPadColor[pad])
                {
                    lastPadColor[pad] = newColor;
                    out->sendMessageNow(makePadColorSysex(getHardwarePadId(pad), r, g, b));
                }
            }
        }

        bool handleMidiInput(const juce::MidiMessage& msg, MiniLAB3StepSequencerAudioProcessor& processor) override
        {
            if (msg.isController())
            {
                const int cc = msg.getControllerNumber();
                const int value = msg.getControllerValue();

                if (cc == 1)
                {
                    int newInstrument = (127 - value) / 8;
                    newInstrument = juce::jlimit(0, MiniLAB3Seq::kNumTracks - 1, newInstrument);
                    processor.currentInstrument.store(newInstrument, std::memory_order_release);
                    return true;
                }
                else if (cc == 74 || cc == 71 || cc == 76 || cc == 77 || cc == 93 || cc == 18 || cc == 19 || cc == 16)
                {
                    int knobIndex = -1;
                    if (cc == 74) knobIndex = 0; else if (cc == 71) knobIndex = 1;
                    else if (cc == 76) knobIndex = 2; else if (cc == 77) knobIndex = 3;
                    else if (cc == 93) knobIndex = 4; else if (cc == 18) knobIndex = 5;
                    else if (cc == 19) knobIndex = 6; else if (cc == 16) knobIndex = 7;

                    if (knobIndex >= 0)
                    {
                        processor.modifySequencerState([&](auto& writeMatrix)
                            {
                                const int pIdx = processor.activePatternIndex.load(std::memory_order_acquire);
                                const int step = (processor.currentPage.load(std::memory_order_acquire) * MiniLAB3Seq::kPadsPerPage) + knobIndex;
                                const int instrument = processor.currentInstrument.load(std::memory_order_acquire);
                                if (writeMatrix[pIdx][instrument][step].isActive)
                                    writeMatrix[pIdx][instrument][step].velocity = value / 127.0f;
                            });
                        return true;
                    }
                }
                else if (cc == 114)
                {
                    const auto page = processor.currentPage.load(std::memory_order_acquire);
                    if (value > 64)      processor.currentPage.store(juce::jmin(MiniLAB3Seq::kNumPages - 1, page + 1), std::memory_order_release);
                    else if (value < 64) processor.currentPage.store(juce::jmax(0, page - 1), std::memory_order_release);
                    return true;
                }
                else if (cc == 115 && value == 127)
                {
                    const int instrument = processor.currentInstrument.load(std::memory_order_acquire);
                    processor.modifySequencerState([&](auto& writeMatrix)
                        {
                            const int pIdx = processor.activePatternIndex.load(std::memory_order_acquire);
                            const int page = processor.currentPage.load(std::memory_order_acquire);
                            for (int step = page * MiniLAB3Seq::kPadsPerPage;
                                step < (page * MiniLAB3Seq::kPadsPerPage) + MiniLAB3Seq::kPadsPerPage;
                                ++step)
                            {
                                writeMatrix[pIdx][instrument][step].isActive = false;
                            }
                        });
                    processor.updateTrackLength(instrument);
                    return true;
                }
            }
            else if (msg.isNoteOn())
            {
                const int note = msg.getNoteNumber();
                if (note >= 36 && note <= 43)
                {
                    const int instrument = processor.currentInstrument.load(std::memory_order_acquire);
                    processor.modifySequencerState([&](auto& writeMatrix)
                        {
                            const int pIdx = processor.activePatternIndex.load(std::memory_order_acquire);
                            const int step = (processor.currentPage.load(std::memory_order_acquire) * MiniLAB3Seq::kPadsPerPage) + (note - 36);
                            writeMatrix[pIdx][instrument][step].isActive = !writeMatrix[pIdx][instrument][step].isActive;
                            if (writeMatrix[pIdx][instrument][step].isActive)
                                writeMatrix[pIdx][instrument][step].velocity = msg.getFloatVelocity();
                        });
                    processor.updateTrackLength(instrument);
                    return true;
                }
            }

            return false;
        }

    private:
        PadColor lastPadColor[MiniLAB3Seq::kPadsPerPage]{};

        uint8_t getHardwarePadId(int softwareIndex) const
        {
            return static_cast<uint8_t>(softwareIndex + 4);
        }

        juce::MidiMessage makePadColorSysex(uint8_t padIdx, uint8_t r, uint8_t g, uint8_t b) const
        {
            const uint8_t data[] =
            {
                0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02, 0x01, 0x16,
                padIdx,
                static_cast<uint8_t>(r & 0x7F),
                static_cast<uint8_t>(g & 0x7F),
                static_cast<uint8_t>(b & 0x7F),
                0xF7
            };
            return juce::MidiMessage(data, sizeof(data));
        }
    };
} // namespace

void MiniLAB3StepSequencerAudioProcessor::openHardwareOutput()
{
    // Strict atomic guard against concurrent execution threads
    bool expected = false;
    if (!isAttemptingConnection.compare_exchange_strong(expected, true))
        return;

    {
        const juce::SpinLock::ScopedLockType lock(hardwareLock);
        if (hardwareOutput != nullptr)
        {
            isAttemptingConnection.store(false, std::memory_order_release);
            return;
        }
    }

    // Explicitly runs on the Message Thread, natively preventing JUCE assertions
    auto devices = juce::MidiOutput::getAvailableDevices();

    std::unique_ptr<juce::MidiOutput> newOutput;
    std::unique_ptr<ControllerProfile> newProfile;

    std::vector<std::unique_ptr<ControllerProfile>> profiles;
    profiles.push_back(std::make_unique<ArturiaMiniLab3Profile>());

    for (const auto& device : devices)
    {
        for (auto& profile : profiles)
        {
            if (device.name.containsIgnoreCase(profile->getDeviceNameSubstring())
                && device.name.containsIgnoreCase("MIDI")
                && !device.name.containsIgnoreCase("DIN")
                && !device.name.containsIgnoreCase("MCU"))
            {
                newOutput = juce::MidiOutput::openDevice(device.identifier);
                if (newOutput != nullptr)
                {
                    newProfile = std::move(profile);
                    newProfile->initializeHardware(newOutput.get());
                }
                break;
            }
        }
        if (newOutput != nullptr) break;
    }

    if (newOutput != nullptr)
    {
        const juce::SpinLock::ScopedLockType lock(hardwareLock);
        hardwareOutput = std::move(newOutput);
        activeController = std::move(newProfile);
        requestLedRefresh();
    }

    // Always clear the flag once entirely finished
    isAttemptingConnection.store(false, std::memory_order_release);
}

void MiniLAB3StepSequencerAudioProcessor::resetHardwareState()
{
    const juce::SpinLock::ScopedLockType lock(hardwareLock);
    if (hardwareOutput != nullptr && activeController != nullptr)
        activeController->resetHardware(hardwareOutput.get());
    juce::Thread::sleep(30);
}

void MiniLAB3StepSequencerAudioProcessor::requestLedRefresh()
{
    ledRefreshCountdown.store(3, std::memory_order_release);
}

void MiniLAB3StepSequencerAudioProcessor::timerCallback()
{
    applyPendingParameterUpdates();

    if (initialising.load(std::memory_order_acquire))
        return;

    auto readHandle = hwFifo.read(hwFifo.getNumReady());
    auto processQueue = [&](int start, int size) {
        juce::MidiBuffer dummyBuffer;
        for (int i = 0; i < size; ++i) {
            juce::MidiMessage msg(hwQueue[start + i].d, hwQueue[start + i].len, 0);
            handleMidiInput(msg, dummyBuffer);
        }
        };
    processQueue(readHandle.startIndex1, readHandle.blockSize1);
    processQueue(readHandle.startIndex2, readHandle.blockSize2);

    if (hardwareOutput == nullptr)
    {
        static int connectionRetry = 0;
        if (++connectionRetry >= 25)
        {
            openHardwareOutput();
            connectionRetry = 0;
        }
    }

    const int currentCountdown = ledRefreshCountdown.load(std::memory_order_acquire);
    if (currentCountdown > 0)
    {
        updateHardwareLEDs(true);
        ledRefreshCountdown.store(currentCountdown - 1, std::memory_order_release);
    }
    else
    {
        updateHardwareLEDs(false);
    }
}

void MiniLAB3StepSequencerAudioProcessor::updateHardwareLEDs(bool forceOverwrite)
{
    const juce::SpinLock::ScopedLockType lock(hardwareLock);
    if (hardwareOutput != nullptr && activeController != nullptr)
        activeController->updateLEDs(hardwareOutput.get(), *this, forceOverwrite);
}

void MiniLAB3StepSequencerAudioProcessor::handleMidiInput(const juce::MidiMessage& msg, juce::MidiBuffer&)
{
    if (initialising.load(std::memory_order_acquire))
        return;

    bool wasHandledByController = false;
    {
        const juce::SpinLock::ScopedTryLockType lock(hardwareLock);
        if (lock.isLocked() && activeController != nullptr)
        {
            wasHandledByController = activeController->handleMidiInput(msg, *this);
        }
    }

    if (wasHandledByController)
    {
        requestLedRefresh();
        markUiStateDirty();
    }

    if (msg.isController())
    {
        const int cc = msg.getControllerNumber();
        const float val = msg.getControllerValue() / 127.0f;

        if (cc == 7)
            pendingMasterVolNormalized.store(val, std::memory_order_release);
        else if (cc == 11)
            pendingSwingNormalized.store(val, std::memory_order_release);
    }
}