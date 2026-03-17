#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::MidiMessage makeMiniLab3PadColorSysex(uint8_t padIdx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t data[] = { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02, 0x01, 0x16, padIdx, (uint8_t)(r & 0x7F), (uint8_t)(g & 0x7F), (uint8_t)(b & 0x7F), 0xF7 };
    return juce::MidiMessage(data, sizeof(data));
}

// ==============================================================================
// APVTS PARAMETER LAYOUT
// ==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MiniLAB3StepSequencerAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Global Controls
    layout.add(std::make_unique<juce::AudioParameterFloat>("master_vol", "Master Volume", 0.0f, 1.0f, 0.8f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("swing", "Swing", 0.0f, 1.0f, 0.0f));

    // Track Controls (Mutes, Solos, MIDI Notes)
    for (int i = 0; i < 16; ++i) {
        juce::String trk = juce::String(i + 1);
        layout.add(std::make_unique<juce::AudioParameterBool>("mute_" + trk, "Mute " + trk, false));
        layout.add(std::make_unique<juce::AudioParameterBool>("solo_" + trk, "Solo " + trk, false));
        layout.add(std::make_unique<juce::AudioParameterInt>("note_" + trk, "MIDI Note " + trk, 0, 127, 36 + i));
        layout.add(std::make_unique<juce::AudioParameterFloat>("nudge_" + trk, "Micro-Timing " + trk,
            juce::NormalisableRange<float>(minMicroTimingMs, maxMicroTimingMs, 0.1f), 0.0f));
    }
    return layout;
}

MiniLAB3StepSequencerAudioProcessor::MiniLAB3StepSequencerAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // Fetch real-time safe pointers from APVTS so processBlock doesn't choke looking up strings
    masterVolParam = apvts.getRawParameterValue("master_vol");
    swingParam = apvts.getRawParameterValue("swing");

    const char* names[] = { "Kick", "Snare", "Clap", "Hat Cl", "Hat Op", "Tom L", "Tom M", "Tom H", "Rim", "Shaker", "Cowbell", "Perc 1", "Perc 2", "Conga", "Maracas", "Clave" };

    const juce::ScopedLock sl(stateLock);
    for (int i = 0; i < 16; ++i) {
        instrumentNames[i] = juce::String(names[i]);

        juce::String trk = juce::String(i + 1);
        muteParams[i] = apvts.getRawParameterValue("mute_" + trk);
        soloParams[i] = apvts.getRawParameterValue("solo_" + trk);
        noteParams[i] = apvts.getRawParameterValue("note_" + trk);
        nudgeParams[i] = apvts.getRawParameterValue("nudge_" + trk);

        for (int s = 0; s < 32; ++s) {
            sequencerMatrix[i][s] = { false, 0.8f, 1.0f };
            lastFiredVelocity[i][s] = 0.0f;
        }
        updateTrackLength(i);
    }
    startTimer(20);
}

MiniLAB3StepSequencerAudioProcessor::~MiniLAB3StepSequencerAudioProcessor() { stopTimer(); resetHardwareState(); }
uint8_t MiniLAB3StepSequencerAudioProcessor::getHardwarePadId(int softwareIndex) { return (uint8_t)(softwareIndex + 4); }

void MiniLAB3StepSequencerAudioProcessor::openHardwareOutput() {
    if (hardwareOutput != nullptr || isAttemptingConnection) return;
    isAttemptingConnection = true;
    for (auto& device : juce::MidiOutput::getAvailableDevices()) {
        if (device.name.containsIgnoreCase("Minilab3") && device.name.containsIgnoreCase("MIDI")
            && !device.name.containsIgnoreCase("DIN") && !device.name.containsIgnoreCase("MCU"))
        {
            hardwareOutput = juce::MidiOutput::openDevice(device.identifier);
            if (hardwareOutput) {
                for (int i = 0; i < 8; i++) hardwareOutput->sendMessageNow(makeMiniLab3PadColorSysex(getHardwarePadId(i), 0, 0, 0));
                requestLedRefresh();
            }
            break;
        }
    }
    isAttemptingConnection = false;
}

void MiniLAB3StepSequencerAudioProcessor::resetHardwareState() {
    if (hardwareOutput) {
        for (int i = 0; i < 8; i++) hardwareOutput->sendMessageNow(makeMiniLab3PadColorSysex(getHardwarePadId(i), 0, 0, 0));
        juce::Thread::sleep(30);
    }
}

void MiniLAB3StepSequencerAudioProcessor::requestLedRefresh() { updateHardwareLEDs(true); ledRefreshCountdown.store(3); }

void MiniLAB3StepSequencerAudioProcessor::timerCallback() {
    int current = ledRefreshCountdown.load();
    if (current > 0) {
        updateHardwareLEDs(true);
        ledRefreshCountdown.store(current - 1);
    }
    else updateHardwareLEDs(false);
}

void MiniLAB3StepSequencerAudioProcessor::updateHardwareLEDs(bool forceOverwrite) {
    if (!hardwareOutput) return;

    int page = currentPage.load();
    int start = page * 8;
    int instrument = currentInstrument.load();
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

        if (isPlayhead) { r = active ? 127 : 15; g = active ? 127 : 15; b = active ? 127 : 15; }
        else if (active) {
            float brightness = 0.1f + (vel * 0.9f);
            uint8_t c = (uint8_t)(127.0f * brightness);
            if (page == 0) { r = 0; g = c; b = c; }
            else if (page == 1) { r = c; g = 0; b = c; }
            else if (page == 2) { r = c; g = c; b = 0; }
            else if (page == 3) { r = c; g = c; b = c; }
        }

        PadColor newColor{ r, g, b };
        if (forceOverwrite || newColor != lastPadColor[p]) {
            lastPadColor[p] = newColor;
            hardwareOutput->sendMessageNow(makeMiniLab3PadColorSysex(getHardwarePadId(p), r, g, b));
        }
    }
}

void MiniLAB3StepSequencerAudioProcessor::updateTrackLength(int trackIndex) {
    const juce::ScopedLock sl(stateLock);
    int maxActiveStep = -1;
    for (int s = 31; s >= 0; --s) {
        if (sequencerMatrix[trackIndex][s].isActive) { maxActiveStep = s; break; }
    }
    if (maxActiveStep >= 24)      trackLengths[trackIndex] = 32;
    else if (maxActiveStep >= 16) trackLengths[trackIndex] = 24;
    else if (maxActiveStep >= 8)  trackLengths[trackIndex] = 16;
    else                          trackLengths[trackIndex] = 8;
}

void MiniLAB3StepSequencerAudioProcessor::handleMidiInput(const juce::MidiMessage& msg, juce::MidiBuffer& midiMessages) {
    if (msg.isController()) {
        const int cc = msg.getControllerNumber();
        const int val = msg.getControllerValue();

        if (cc == 1) {
            int newInst = (127 - val) / 8;
            newInst = juce::jlimit(0, 15, newInst);
            if (currentInstrument.load() != newInst) {
                currentInstrument.store(newInst);
                requestLedRefresh();
            }
        }
        else if (cc == 74 || cc == 71 || cc == 76 || cc == 77 || cc == 93 || cc == 18 || cc == 19 || cc == 16) {
            int knobIdx = -1;
            if (cc == 74) knobIdx = 0; else if (cc == 71) knobIdx = 1; else if (cc == 76) knobIdx = 2; else if (cc == 77) knobIdx = 3;
            else if (cc == 93) knobIdx = 4; else if (cc == 18) knobIdx = 5; else if (cc == 19) knobIdx = 6; else if (cc == 16) knobIdx = 7;

            if (knobIdx >= 0) {
                int page = currentPage.load();
                int instrument = currentInstrument.load();
                int step = (page * 8) + knobIdx;
                const juce::ScopedLock sl(stateLock);
                if (sequencerMatrix[instrument][step].isActive) sequencerMatrix[instrument][step].velocity = val / 127.0f;
            }
        }
        else if (cc == 114) {
            auto page = currentPage.load();
            if (val > 64) currentPage.store(juce::jmin(3, page + 1));
            else if (val < 64) currentPage.store(juce::jmax(0, page - 1));
            pageChangedTrigger.store(true);
            requestLedRefresh();
        }
        else if (cc == 115 && val == 127) {
            const int page = currentPage.load();
            const int instrument = currentInstrument.load();
            {
                const juce::ScopedLock sl(stateLock);
                for (int s = page * 8; s < (page * 8) + 8; ++s) sequencerMatrix[instrument][s].isActive = false;
            }
            updateTrackLength(instrument);
            requestLedRefresh();
        }
        // Sync hardware to APVTS via setParameterNotifyingHost
        else if (cc == 7) {
            if (auto* p = apvts.getParameter("master_vol")) p->setValueNotifyingHost(val / 127.0f);
        }
        else if (cc == 11) {
            if (auto* p = apvts.getParameter("swing")) p->setValueNotifyingHost(val / 127.0f);
        }
    }
    else if (msg.isNoteOn()) {
        const int n = msg.getNoteNumber();
        if (n >= 36 && n <= 43) {
            const int page = currentPage.load();
            const int instrument = currentInstrument.load();
            const int step = (page * 8) + (n - 36);

            {
                const juce::ScopedLock sl(stateLock);
                sequencerMatrix[instrument][step].isActive = !sequencerMatrix[instrument][step].isActive;
                if (sequencerMatrix[instrument][step].isActive) sequencerMatrix[instrument][step].velocity = msg.getFloatVelocity();
            }
            updateTrackLength(instrument);
            requestLedRefresh();
        }
    }
}

void MiniLAB3StepSequencerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    const int numSamples = buffer.getNumSamples();
    juce::MidiBuffer incoming;
    incoming.swapWith(midiMessages);
    for (const auto metadata : incoming) handleMidiInput(metadata.getMessage(), midiMessages);

    bool anySolo = false;
    for (int t = 0; t < 16; ++t) {
        if (soloParams[t]->load() > 0.5f) { anySolo = true; break; }
    }

    if (auto* ph = getPlayHead()) {
        if (auto pos = ph->getPosition()) {
            if (pos->getIsPlaying()) {
                const double ppqStart = pos->getPpqPosition().orFallback(0.0);
                const double bpm = pos->getBpm().orFallback(120.0);
                const double sampleRate = getSampleRate();
                const double ppqPerSample = bpm / (60.0 * juce::jmax(1.0, sampleRate));
                const double blockEndPpq = ppqStart + (numSamples * ppqPerSample);
                const double stepPpq = 0.25;
                const double swingPpqOffset = (swingParam->load() * ((0.5 * sampleRate) / 8.0)) * ppqPerSample;
                const int current16th = static_cast<int>(std::floor(ppqStart * 4.0));
                global16thNote.store(current16th);

                const int firstCandidate16th = juce::jmax(0, static_cast<int>(std::floor(ppqStart * 4.0)) - 1);
                const int lastCandidate16th  = juce::jmax(firstCandidate16th, static_cast<int>(std::ceil(blockEndPpq * 4.0)) + 1);

                const juce::ScopedLock sl(stateLock);
                for (int t = 0; t < 16; ++t) {
                    const bool canPlay = anySolo ? (soloParams[t]->load() > 0.5f) : (muteParams[t]->load() < 0.5f);
                    if (!canPlay)
                        continue;

                    const int len = juce::jlimit(1, 32, trackLengths[t]);
                    const int note = static_cast<int>(noteParams[t]->load());
                    const double nudgePpqOffset = (nudgeParams[t]->load() * 0.001 * bpm / 60.0);

                    for (int absolute16th = firstCandidate16th; absolute16th <= lastCandidate16th; ++absolute16th) {
                        const int stepIdx = absolute16th % len;
                        if (!sequencerMatrix[t][stepIdx].isActive)
                            continue;

                        double eventPpq = absolute16th * stepPpq;
                        if ((absolute16th % 2) != 0)
                            eventPpq += swingPpqOffset;
                        eventPpq += nudgePpqOffset;

                        const int sampleOffset = static_cast<int>(std::round((eventPpq - ppqStart) / ppqPerSample));
                        if (sampleOffset < 0 || sampleOffset >= numSamples)
                            continue;

                        const float vel = sequencerMatrix[t][stepIdx].velocity * masterVolParam->load();
                        midiMessages.addEvent(juce::MidiMessage::noteOn(1, note, vel), sampleOffset);
                        midiMessages.addEvent(juce::MidiMessage::noteOff(1, note, 0.0f), juce::jmin(numSamples - 1, sampleOffset + 500));
                        lastFiredVelocity[t][stepIdx] = sequencerMatrix[t][stepIdx].velocity;
                    }
                }
            }
            else {
                global16thNote.store(-1);
            }
        }
    }

    const juce::ScopedLock sl(stateLock);
    for (int t = 0; t < 16; ++t)
        for (int s = 0; s < 32; ++s) lastFiredVelocity[t][s] *= 0.85f;
}

// ==============================================================================
// APVTS SAVE & LOAD
// ==============================================================================
void MiniLAB3StepSequencerAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());

    auto* matrix = xml->createNewChildElement("Matrix");
    const juce::ScopedLock sl(stateLock);
    for (int t = 0; t < 16; ++t) {
        auto* trk = matrix->createNewChildElement("Track");
        trk->setAttribute("name", instrumentNames[t]);
        trk->setAttribute("length", trackLengths[t]);

        juce::String steps, vels;
        for (int s = 0; s < 32; ++s) {
            steps += sequencerMatrix[t][s].isActive ? "1" : "0";
            vels += juce::String(sequencerMatrix[t][s].velocity) + ",";
        }
        trk->setAttribute("steps", steps);
        trk->setAttribute("velocities", vels);
    }
    copyXmlToBinary(*xml, destData);
}

void MiniLAB3StepSequencerAudioProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, size));
    if (xmlState != nullptr) {
        // Load DAW Automation parameters
        if (xmlState->hasTagName(apvts.state.getType())) {
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
        }

        // Load the Sequencer Grid
        if (auto* matrix = xmlState->getChildByName("Matrix")) {
            const juce::ScopedLock sl(stateLock);
            auto* trk = matrix->getChildByName("Track");
            int t = 0;
            while (trk != nullptr && t < 16) {
                instrumentNames[t] = trk->getStringAttribute("name", instrumentNames[t]);

                const juce::String steps = trk->getStringAttribute("steps");
                juce::StringArray vels;
                vels.addTokens(trk->getStringAttribute("velocities"), ",", "");

                for (int s = 0; s < 32; ++s) {
                    sequencerMatrix[t][s].isActive = (s < steps.length() && steps[s] == '1');
                    if (s < vels.size()) sequencerMatrix[t][s].velocity = vels[s].getFloatValue();
                }
                trk = trk->getNextElementWithTagName("Track");
                ++t;
            }
        }
    }
    for (int t = 0; t < 16; ++t) updateTrackLength(t);
    requestLedRefresh();
}

int MiniLAB3StepSequencerAudioProcessor::getGeneralMidiNote(int t) { return 36 + t; }
void MiniLAB3StepSequencerAudioProcessor::prepareToPlay(double, int) { global16thNote.store(-1); }
void MiniLAB3StepSequencerAudioProcessor::releaseResources() {}
bool MiniLAB3StepSequencerAudioProcessor::isBusesLayoutSupported(const juce::AudioProcessor::BusesLayout& l) const { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
juce::AudioProcessorEditor* MiniLAB3StepSequencerAudioProcessor::createEditor() { return new MiniLAB3StepSequencerAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MiniLAB3StepSequencerAudioProcessor(); }