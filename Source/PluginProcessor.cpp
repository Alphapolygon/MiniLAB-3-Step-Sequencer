#include "PluginProcessor.h"
#include "PluginEditor.h"

// ==============================================================================
// HARDWARE COMMUNICATION HELPERS
// ==============================================================================

/**
 * Generates the specific SysEx message required to write a color to the MiniLab 3's Edit Buffer.
 * Header: 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42
 * Command: 0x02 (Write), 0x01 (Edit Buffer), 0x16 (LED)
 */
static juce::MidiMessage makeMiniLab3PadColorSysex(uint8_t padIdx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t data[] = {
        0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, // Manufacturer Header
        0x02, 0x01, 0x16,                   // Write to Edit Buffer -> LED
        padIdx,                             // Target Pad ID
        (uint8_t)(r & 0x7F),                // Red (0-127)
        (uint8_t)(g & 0x7F),                // Green (0-127)
        (uint8_t)(b & 0x7F),                // Blue (0-127)
        0xF7                                // End of SysEx
    };
    return juce::MidiMessage(data, sizeof(data));
}

// ==============================================================================
// LIFECYCLE
// ==============================================================================

MiniLAB3StepSequencerAudioProcessor::MiniLAB3StepSequencerAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // Default GM Drum Map approximations
    const char* names[] = { "Kick", "Snare", "Clap", "Hat Cl", "Hat Op", "Tom L", "Tom M", "Tom H", "Rim", "Shaker", "Cowbell", "Perc 1", "Perc 2", "Conga", "Maracas", "Clave" };

    const juce::ScopedLock sl(stateLock);
    for (int i = 0; i < 16; ++i) {
        instrumentNames[i] = juce::String(names[i]);
        for (int s = 0; s < 32; ++s) {
            sequencerMatrix[i][s] = { false, 0.8f, 1.0f };
            lastFiredVelocity[i][s] = 0.0f;
        }
        updateTrackLength(i);
    }

    // 50Hz continuous loop for UI repainting and hardware LED state syncing
    startTimer(20);
}

MiniLAB3StepSequencerAudioProcessor::~MiniLAB3StepSequencerAudioProcessor() {
    stopTimer();
    resetHardwareState();
}

// ==============================================================================
// HARDWARE INTEGRATION
// ==============================================================================

uint8_t MiniLAB3StepSequencerAudioProcessor::getHardwarePadId(int softwareIndex) {
    // MiniLab 3 maps LEDs 0-3 to the Shift/Octave buttons. 
    // The physical 8 drum pads start at ID 4. We apply a flat +4 offset to protect the control buttons.
    return (uint8_t)(softwareIndex + 4);
}

void MiniLAB3StepSequencerAudioProcessor::openHardwareOutput() {
    if (hardwareOutput != nullptr || isAttemptingConnection) return;
    isAttemptingConnection = true;

    for (auto& device : juce::MidiOutput::getAvailableDevices()) {
        if (device.name.containsIgnoreCase("Minilab3") && device.name.containsIgnoreCase("MIDI")
            && !device.name.containsIgnoreCase("DIN") && !device.name.containsIgnoreCase("MCU"))
        {
            hardwareOutput = juce::MidiOutput::openDevice(device.identifier);
            if (hardwareOutput) {
                // Initialize physical pads to black
                for (int i = 0; i < 8; i++) {
                    hardwareOutput->sendMessageNow(makeMiniLab3PadColorSysex(getHardwarePadId(i), 0, 0, 0));
                }
                requestLedRefresh();
            }
            break;
        }
    }
    isAttemptingConnection = false;
}

void MiniLAB3StepSequencerAudioProcessor::resetHardwareState() {
    if (hardwareOutput) {
        // Clear pads back to black on plugin close so they don't get stuck on custom colors
        for (int i = 0; i < 8; i++) {
            hardwareOutput->sendMessageNow(makeMiniLab3PadColorSysex(getHardwarePadId(i), 0, 0, 0));
        }
        // Give the MIDI driver a moment to flush the buffer before closing
        juce::Thread::sleep(30);
    }
}

void MiniLAB3StepSequencerAudioProcessor::requestLedRefresh() {
    updateHardwareLEDs(true);
    // Queue 3 consecutive overwrites to mask the hardware's internal "Local Control" behavior
    ledRefreshCountdown.store(3);
}

void MiniLAB3StepSequencerAudioProcessor::timerCallback() {
    int current = ledRefreshCountdown.load();
    if (current > 0) {
        // The MiniLab flashes blue automatically when a pad is hit. 
        // We force-overwrite this multiple times over ~60ms to kill the blue flash.
        updateHardwareLEDs(true);
        ledRefreshCountdown.store(current - 1);
    }
    else {
        // Normal continuous check for Playhead & Velocity animations
        updateHardwareLEDs(false);
    }
}

void MiniLAB3StepSequencerAudioProcessor::updateHardwareLEDs(bool forceOverwrite) {
    if (!hardwareOutput) return;

    int page = currentPage.load();
    int start = page * 8;
    int instrument = currentInstrument.load();

    // Calculate playhead position for visual feedback on the pads
    int current16th = global16thNote.load();
    int trackLen = trackLengths[instrument];
    int playingStep = (current16th >= 0 && trackLen > 0) ? (current16th % trackLen) : -1;

    const juce::ScopedLock sl(stateLock);

    for (int p = 0; p < 8; ++p) {
        int step = start + p;
        bool active = sequencerMatrix[instrument][step].isActive;
        float vel = sequencerMatrix[instrument][step].velocity;
        bool isPlayhead = (step == playingStep);

        uint8_t r = 0, g = 0, b = 0;

        if (isPlayhead) {
            // PLAYHEAD VISUAL: Bright White if the step is active, Dim White if empty
            r = active ? 127 : 15;
            g = active ? 127 : 15;
            b = active ? 127 : 15;
        }
        else if (active) {
            // VELOCITY SCALING: Brightness goes from 10% to 100%
            float brightness = 0.1f + (vel * 0.9f);
            uint8_t c = (uint8_t)(127.0f * brightness);

            // PAGE COLORS: Different colors so the user knows which 8-step block they are editing
            if (page == 0) { r = 0; g = c; b = c; } // Cyan
            else if (page == 1) { r = c; g = 0; b = c; } // Magenta
            else if (page == 2) { r = c; g = c; b = 0; } // Yellow
            else if (page == 3) { r = c; g = c; b = c; } // White
        }

        PadColor newColor{ r, g, b };

        // Only send SysEx if the color changed to avoid clogging the USB MIDI bandwidth
        if (forceOverwrite || newColor != lastPadColor[p]) {
            lastPadColor[p] = newColor;
            hardwareOutput->sendMessageNow(makeMiniLab3PadColorSysex(getHardwarePadId(p), r, g, b));
        }
    }
}

// ==============================================================================
// SEQUENCER LOGIC & MIDI PROCESSING
// ==============================================================================

void MiniLAB3StepSequencerAudioProcessor::updateTrackLength(int trackIndex)
{
    const juce::ScopedLock sl(stateLock);
    int maxActiveStep = -1;
    for (int s = 31; s >= 0; --s) {
        if (sequencerMatrix[trackIndex][s].isActive) {
            maxActiveStep = s;
            break;
        }
    }

    // Auto-size track to 8-step blocks based on last placed note
    if (maxActiveStep >= 24)      trackLengths[trackIndex] = 32;
    else if (maxActiveStep >= 16) trackLengths[trackIndex] = 24;
    else if (maxActiveStep >= 8)  trackLengths[trackIndex] = 16;
    else                          trackLengths[trackIndex] = 8;
}

void MiniLAB3StepSequencerAudioProcessor::handleMidiInput(const juce::MidiMessage& msg, juce::MidiBuffer&)
{
    if (msg.isController()) {
        const int cc = msg.getControllerNumber();
        const int val = msg.getControllerValue();

        // MOD WHEEL (CC 1) -> Instrument Selection (Reversed: Up = Inst 1, Down = Inst 16)
        if (cc == 1) {
            int newInst = (127 - val) / 8;
            newInst = juce::jlimit(0, 15, newInst);
            if (currentInstrument.load() != newInst) {
                currentInstrument.store(newInst);
                requestLedRefresh();
            }
        }
        // KNOBS 1-8 -> Velocity Control (Mapped to default MiniLab 3 CCs)
        else if (cc == 74 || cc == 71 || cc == 76 || cc == 77 || cc == 93 || cc == 18 || cc == 19 || cc == 16) {
            int knobIdx = -1;
            if (cc == 74) knobIdx = 0; // Knob 1
            else if (cc == 71) knobIdx = 1; // Knob 2
            else if (cc == 76) knobIdx = 2; // Knob 3
            else if (cc == 77) knobIdx = 3; // Knob 4
            else if (cc == 93) knobIdx = 4; // Knob 5
            else if (cc == 18) knobIdx = 5; // Knob 6
            else if (cc == 19) knobIdx = 6; // Knob 7
            else if (cc == 16) knobIdx = 7; // Knob 8

            if (knobIdx >= 0) {
                int page = currentPage.load();
                int instrument = currentInstrument.load();
                int step = (page * 8) + knobIdx;

                const juce::ScopedLock sl(stateLock);
                // Only allow velocity adjustment if the step is actually active
                if (sequencerMatrix[instrument][step].isActive) {
                    sequencerMatrix[instrument][step].velocity = val / 127.0f;
                }
            }
        }
        // MAIN ENCODER (CC 114) -> Page Turn
        else if (cc == 114) {
            auto page = currentPage.load();
            if (val > 64) currentPage.store(juce::jmin(3, page + 1));
            else if (val < 64) currentPage.store(juce::jmax(0, page - 1));
            pageChangedTrigger.store(true);
            requestLedRefresh();
        }
        // MAIN ENCODER CLICK (CC 115) -> Clear current page
        else if (cc == 115 && val == 127) {
            const int page = currentPage.load();
            const int instrument = currentInstrument.load();
            {
                const juce::ScopedLock sl(stateLock);
                for (int s = page * 8; s < (page * 8) + 8; ++s)
                    sequencerMatrix[instrument][s].isActive = false;
            }
            updateTrackLength(instrument);
            requestLedRefresh();
        }
        else if (cc == 7) masterVolume.store(val / 127.0f);
        else if (cc == 11) swingAmount.store(val / 127.0f);
    }
    else if (msg.isNoteOn()) {
        const int n = msg.getNoteNumber();

        // PADS (36-43) -> Toggle Steps
        if (n >= 36 && n <= 43) {
            const int page = currentPage.load();
            const int instrument = currentInstrument.load();
            const int step = (page * 8) + (n - 36);

            {
                const juce::ScopedLock sl(stateLock);
                sequencerMatrix[instrument][step].isActive = !sequencerMatrix[instrument][step].isActive;

                // Set default velocity when toggled ON
                if (sequencerMatrix[instrument][step].isActive) {
                    sequencerMatrix[instrument][step].velocity = msg.getFloatVelocity();
                }
            }
            updateTrackLength(instrument);
            requestLedRefresh();
        }
    }
}

void MiniLAB3StepSequencerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    const int numSamples = buffer.getNumSamples();
    juce::MidiBuffer incoming;
    incoming.swapWith(midiMessages);

    // Parse incoming UI/Hardware MIDI 
    for (const auto metadata : incoming)
        handleMidiInput(metadata.getMessage(), midiMessages);

    // Process Sequencer Playback
    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            if (pos->getIsPlaying()) {
                const double ppq = pos->getPpqPosition().orFallback(0.0);
                const int current16th = static_cast<int>(std::floor(ppq * 4.0));

                if (current16th != global16thNote.load()) {
                    global16thNote.store(current16th);

                    // Calculate swing offset (delays every even 16th note)
                    const bool isEvenNote = (current16th % 2 != 0);
                    int sampleOffset = isEvenNote ? static_cast<int>(swingAmount.load() * (getSampleRate() / 8.0) * 0.5) : 0;
                    sampleOffset = juce::jlimit(0, juce::jmax(0, numSamples - 1), sampleOffset);

                    const juce::ScopedLock sl(stateLock);
                    for (int t = 0; t < 16; ++t) {
                        const int len = juce::jlimit(1, 32, trackLengths[t]);
                        const int stepIdx = current16th % len;

                        if (sequencerMatrix[t][stepIdx].isActive) {
                            const float vel = sequencerMatrix[t][stepIdx].velocity * masterVolume.load();
                            midiMessages.addEvent(juce::MidiMessage::noteOn(1, getGeneralMidiNote(t), vel), sampleOffset);

                            // Schedule Note Off shortly after
                            const int noteOffSample = juce::jmin(numSamples - 1, sampleOffset + 500);
                            midiMessages.addEvent(juce::MidiMessage::noteOff(1, getGeneralMidiNote(t), 0.0f), noteOffSample);

                            // Update UI visual feedback
                            lastFiredVelocity[t][stepIdx] = sequencerMatrix[t][stepIdx].velocity;
                        }
                    }
                }
            }
            else global16thNote.store(-1); // Transport is stopped
        }
    }

    // Decay the visual feedback velocities
    const juce::ScopedLock sl(stateLock);
    for (int t = 0; t < 16; ++t)
        for (int s = 0; s < 32; ++s)
            lastFiredVelocity[t][s] *= 0.85f;
}

// ==============================================================================
// STATE SAVING & LOADING
// ==============================================================================

void MiniLAB3StepSequencerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::XmlElement xml("MiniLab3SeqState");
    xml.setAttribute("masterVolume", static_cast<double>(masterVolume.load()));
    xml.setAttribute("swingAmount", static_cast<double>(swingAmount.load()));

    auto* matrix = xml.createNewChildElement("Matrix");

    const juce::ScopedLock sl(stateLock);
    for (int t = 0; t < 16; ++t) {
        auto* trk = matrix->createNewChildElement("Track");
        trk->setAttribute("name", instrumentNames[t]);
        trk->setAttribute("length", trackLengths[t]);

        juce::String steps;
        for (int s = 0; s < 32; ++s)
            steps += sequencerMatrix[t][s].isActive ? "1" : "0";

        trk->setAttribute("steps", steps);
    }
    copyXmlToBinary(xml, destData);
}

void MiniLAB3StepSequencerAudioProcessor::setStateInformation(const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (xml == nullptr || !xml->hasTagName("MiniLab3SeqState")) return;

    masterVolume.store(static_cast<float>(xml->getDoubleAttribute("masterVolume", 0.8)));
    swingAmount.store(static_cast<float>(xml->getDoubleAttribute("swingAmount", 0.0)));

    if (auto* matrix = xml->getChildByName("Matrix")) {
        const juce::ScopedLock sl(stateLock);

        auto* trk = matrix->getChildByName("Track");
        int t = 0;
        while (trk != nullptr && t < 16) {
            instrumentNames[t] = trk->getStringAttribute("name", instrumentNames[t]);

            const juce::String steps = trk->getStringAttribute("steps");
            for (int s = 0; s < 32; ++s)
                sequencerMatrix[t][s].isActive = (s < steps.length() && steps[s] == '1');

            trk = trk->getNextElementWithTagName("Track");
            ++t;
        }
    }

    for (int t = 0; t < 16; ++t) updateTrackLength(t);
    requestLedRefresh();
}

int MiniLAB3StepSequencerAudioProcessor::getGeneralMidiNote(int t) { return 36 + t; } // Maps Track 0 -> C1
void MiniLAB3StepSequencerAudioProcessor::prepareToPlay(double, int) { global16thNote.store(-1); }
void MiniLAB3StepSequencerAudioProcessor::releaseResources() {}
bool MiniLAB3StepSequencerAudioProcessor::isBusesLayoutSupported(const juce::AudioProcessor::BusesLayout& l) const { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
juce::AudioProcessorEditor* MiniLAB3StepSequencerAudioProcessor::createEditor() { return new MiniLAB3StepSequencerAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MiniLAB3StepSequencerAudioProcessor(); }