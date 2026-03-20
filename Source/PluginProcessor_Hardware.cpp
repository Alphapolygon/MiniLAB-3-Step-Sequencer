#include "PluginProcessor.h"

namespace
{
    struct PadColor
    {
        uint8_t r = 0; uint8_t g = 0; uint8_t b = 0;
        bool operator!=(const PadColor& other) const { return r != other.r || g != other.g || b != other.b; }
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
            const int page = processor.currentPage.load();
            const int startStep = page * MiniLAB3Seq::kPadsPerPage;
            const int instrument = processor.currentInstrument.load();
            const int current16th = processor.global16thNote.load();
            const int trackLength = processor.trackLengths[instrument];
            const int playingStep = (current16th >= 0 && trackLength > 0) ? (current16th % trackLength) : -1;

            const auto& matrix = processor.getActiveMatrix();

            for (int pad = 0; pad < MiniLAB3Seq::kPadsPerPage; ++pad)
            {
                const int step = startStep + pad;
                const bool active = matrix[instrument][step].isActive;
                const float velocity = matrix[instrument][step].velocity;
                const bool isPlayhead = (step == playingStep);

                uint8_t r = 0, g = 0, b = 0;

                if (isPlayhead)
                {
                    r = active ? 127 : 15; g = active ? 127 : 15; b = active ? 127 : 15;
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
                    processor.currentInstrument.store(newInstrument);
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
                        processor.modifySequencerState([&](auto& writeMatrix) {
                            const int step = (processor.currentPage.load() * MiniLAB3Seq::kPadsPerPage) + knobIndex;
                            if (writeMatrix[processor.currentInstrument.load()][step].isActive)
                                writeMatrix[processor.currentInstrument.load()][step].velocity = value / 127.0f;
                            });
                        return true;
                    }
                }
                else if (cc == 114)
                {
                    const auto page = processor.currentPage.load();
                    if (value > 64)      processor.currentPage.store(juce::jmin(MiniLAB3Seq::kNumPages - 1, page + 1));
                    else if (value < 64) processor.currentPage.store(juce::jmax(0, page - 1));
                    processor.pageChangedTrigger.store(true);
                    return true;
                }
                else if (cc == 115 && value == 127)
                {
                    const int instrument = processor.currentInstrument.load();
                    processor.modifySequencerState([&](auto& writeMatrix) {
                        const int page = processor.currentPage.load();
                        for (int step = page * MiniLAB3Seq::kPadsPerPage; step < (page * MiniLAB3Seq::kPadsPerPage) + MiniLAB3Seq::kPadsPerPage; ++step)
                            writeMatrix[instrument][step].isActive = false;
                        });
                    processor.updateTrackLength(instrument);
                    return true;
                }
            }
            else if ((msg.isNoteOn() || msg.isNoteOff()) && msg.isNoteOn())
            {
                const int note = msg.getNoteNumber();
                if (note >= 36 && note <= 43)
                {
                    const int instrument = processor.currentInstrument.load();
                    processor.modifySequencerState([&](auto& writeMatrix) {
                        const int step = (processor.currentPage.load() * MiniLAB3Seq::kPadsPerPage) + (note - 36);
                        writeMatrix[instrument][step].isActive = !writeMatrix[instrument][step].isActive;
                        if (writeMatrix[instrument][step].isActive)
                            writeMatrix[instrument][step].velocity = msg.getFloatVelocity();
                        });
                    processor.updateTrackLength(instrument);
                    return true;
                }
            }
            return false;
        }

    private:
        PadColor lastPadColor[8];

        uint8_t getHardwarePadId(int softwareIndex) const { return static_cast<uint8_t>(softwareIndex + 4); }
        juce::MidiMessage makePadColorSysex(uint8_t padIdx, uint8_t r, uint8_t g, uint8_t b) const
        {
            const uint8_t data[] = { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02, 0x01, 0x16, padIdx, static_cast<uint8_t>(r & 0x7F), static_cast<uint8_t>(g & 0x7F), static_cast<uint8_t>(b & 0x7F), 0xF7 };
            return juce::MidiMessage(data, sizeof(data));
        }
    };
} // namespace

void MiniLAB3StepSequencerAudioProcessor::openHardwareOutput()
{
    if (hardwareOutput != nullptr || isAttemptingConnection)
        return;

    isAttemptingConnection = true;

    std::vector<std::unique_ptr<ControllerProfile>> profiles;
    profiles.push_back(std::make_unique<ArturiaMiniLab3Profile>());

    // This MUST run on the main thread to prevent the MidiDeviceList assertion
    for (const auto& device : juce::MidiOutput::getAvailableDevices())
    {
        for (auto& profile : profiles)
        {
            if (device.name.containsIgnoreCase(profile->getDeviceNameSubstring()) &&
                device.name.containsIgnoreCase("MIDI") &&
                !device.name.containsIgnoreCase("DIN") &&
                !device.name.containsIgnoreCase("MCU"))
            {
                hardwareOutput = juce::MidiOutput::openDevice(device.identifier);
                if (hardwareOutput != nullptr)
                {
                    activeController = std::move(profile);
                    activeController->initializeHardware(hardwareOutput.get());
                    requestLedRefresh();
                }
                break;
            }
        }
        if (hardwareOutput != nullptr) break;
    }

    isAttemptingConnection = false;
}

void MiniLAB3StepSequencerAudioProcessor::resetHardwareState()
{
    if (hardwareOutput != nullptr && activeController != nullptr)
        activeController->resetHardware(hardwareOutput.get());
    juce::Thread::sleep(30);
}

void MiniLAB3StepSequencerAudioProcessor::requestLedRefresh()
{
   // updateHardwareLEDs(true);
    ledRefreshCountdown.store(3);
}

void MiniLAB3StepSequencerAudioProcessor::timerCallback()
{
    if (initialising.load(std::memory_order_acquire))
        return;

    if (hardwareOutput == nullptr)
    {
        static int connectionRetry = 0;
        if (++connectionRetry >= 25)
        {
            openHardwareOutput();
            connectionRetry = 0;
        }
    }

    const int currentCountdown = ledRefreshCountdown.load();
    if (currentCountdown > 0)
    {
        updateHardwareLEDs(true);
        ledRefreshCountdown.store(currentCountdown - 1);
    }
    else
    {
        updateHardwareLEDs(false);
    }
}

void MiniLAB3StepSequencerAudioProcessor::updateHardwareLEDs(bool forceOverwrite)
{
    if (hardwareOutput != nullptr && activeController != nullptr)
        activeController->updateLEDs(hardwareOutput.get(), *this, forceOverwrite);
}

void MiniLAB3StepSequencerAudioProcessor::handleMidiInput(const juce::MidiMessage& msg, juce::MidiBuffer&)
{
    if (initialising.load(std::memory_order_acquire))
        return;

    if (activeController != nullptr)
    {
        if (activeController->handleMidiInput(msg, *this))
        {
            requestLedRefresh();
            markUiStateDirty();
        }
    }

    // Master Volume & Swing intercept fallback (in case profiles don't catch it)
    if (msg.isController())
    {
        const int cc = msg.getControllerNumber();
        const float val = msg.getControllerValue() / 127.0f;

        if (cc == 7)
        {
            if (auto* parameter = apvts.getParameter("master_vol"))
                parameter->setValueNotifyingHost(val);
        }
        else if (cc == 11)
        {
            if (auto* parameter = apvts.getParameter("swing"))
                parameter->setValueNotifyingHost(val);
        }
    }
}