#include "PluginProcessor.h"
#include "PluginEditor.h"

static juce::MidiMessage makeMiniLab3PadColorSysex(uint8_t padIdx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t data[] = { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02, 0x01, 0x16, padIdx, (uint8_t)(r & 0x7F), (uint8_t)(g & 0x7F), (uint8_t)(b & 0x7F), 0xF7 };
    return juce::MidiMessage(data, sizeof(data));
}

juce::AudioProcessorValueTreeState::ParameterLayout MiniLAB3StepSequencerAudioProcessor::createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>("master_vol", "Master Volume", 0.0f, 1.0f, 0.8f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("swing", "Swing", 0.0f, 1.0f, 0.0f));

    for (int i = 0; i < 16; ++i) {
        juce::String trk = juce::String(i + 1);
        layout.add(std::make_unique<juce::AudioParameterBool>("mute_" + trk, "Mute " + trk, false));
        layout.add(std::make_unique<juce::AudioParameterBool>("solo_" + trk, "Solo " + trk, false));
        layout.add(std::make_unique<juce::AudioParameterInt>("note_" + trk, "MIDI Note " + trk, 0, 127, 36 + i));
    }
    return layout;
}

MiniLAB3StepSequencerAudioProcessor::MiniLAB3StepSequencerAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    masterVolParam = apvts.getRawParameterValue("master_vol");
    swingParam = apvts.getRawParameterValue("swing");
    const char* names[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" };

    const juce::ScopedLock sl(stateLock);
    for (int i = 0; i < 16; ++i) {
        instrumentNames[i] = juce::String(names[i]);
        juce::String trk = juce::String(i + 1);
        muteParams[i] = apvts.getRawParameterValue("mute_" + trk);
        soloParams[i] = apvts.getRawParameterValue("solo_" + trk);
        noteParams[i] = apvts.getRawParameterValue("note_" + trk);

        for (int s = 0; s < 32; ++s) {
            sequencerMatrix[i][s] = { false, 0.8f, 1.0f, 0.75f, 0.0f, 0.0f, 1 };
        }
        updateTrackLength(i);
    }
    startTimer(20);
}

MiniLAB3StepSequencerAudioProcessor::~MiniLAB3StepSequencerAudioProcessor() { stopTimer(); resetHardwareState(); }

void MiniLAB3StepSequencerAudioProcessor::setStepDataFromJson(const juce::String& jsonString) {
    auto parsed = juce::JSON::parse(jsonString);
    if (parsed.isVoid() || !parsed.isObject()) return;

    const juce::ScopedLock sl(stateLock);

    auto activeSteps = parsed.getProperty("activeSteps", juce::var());
    auto velocities = parsed.getProperty("velocities", juce::var());
    auto probabilities = parsed.getProperty("probabilities", juce::var());
    auto gates = parsed.getProperty("gates", juce::var());
    auto shifts = parsed.getProperty("shifts", juce::var());
    auto swings = parsed.getProperty("swings", juce::var());
    auto repeats = parsed.getProperty("repeats", juce::var());
    auto trackStates = parsed.getProperty("trackStates", juce::var());

    for (int t = 0; t < 16; ++t) {
        if (trackStates.isArray() && t < trackStates.size()) {
            auto ts = trackStates[t];
            if (ts.isObject()) {
                if (muteParams[t]) *muteParams[t] = ts.getProperty("mute", false) ? 1.0f : 0.0f;
                if (soloParams[t]) *soloParams[t] = ts.getProperty("solo", false) ? 1.0f : 0.0f;
            }
        }

        if (activeSteps.isArray() && t < activeSteps.size()) {
            auto trackSteps = activeSteps[t];
            auto trackVels = velocities.isArray() ? velocities[t] : juce::var();
            auto trackProbs = probabilities.isArray() ? probabilities[t] : juce::var();
            auto trackGates = gates.isArray() ? gates[t] : juce::var();
            auto trackShifts = shifts.isArray() ? shifts[t] : juce::var();
            auto trackSwings = swings.isArray() ? swings[t] : juce::var();
            auto trackReps = repeats.isArray() ? repeats[t] : juce::var();

            for (int s = 0; s < 32; ++s) {
                if (trackSteps.isArray() && s < trackSteps.size()) sequencerMatrix[t][s].isActive = static_cast<bool>(trackSteps[s]);
                if (trackVels.isArray() && s < trackVels.size()) sequencerMatrix[t][s].velocity = static_cast<float>(trackVels[s]) / 100.0f;
                if (trackProbs.isArray() && s < trackProbs.size()) sequencerMatrix[t][s].probability = static_cast<float>(trackProbs[s]) / 100.0f;
                if (trackGates.isArray() && s < trackGates.size()) sequencerMatrix[t][s].gate = static_cast<float>(trackGates[s]) / 100.0f;
                if (trackShifts.isArray() && s < trackShifts.size()) sequencerMatrix[t][s].shift = (static_cast<float>(trackShifts[s]) - 50.0f) / 50.0f;
                if (trackSwings.isArray() && s < trackSwings.size()) sequencerMatrix[t][s].swing = static_cast<float>(trackSwings[s]) / 100.0f;
                if (trackReps.isArray() && s < trackReps.size()) sequencerMatrix[t][s].repeat = juce::jlimit(1, 4, static_cast<int>(trackReps[s]));
            }
            updateTrackLength(t);
        }
    }
    requestLedRefresh();
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
            isPlaying.store(pos->getIsPlaying());
            currentBpm.store(pos->getBpm().orFallback(120.0));

            if (pos->getIsPlaying()) {
                const double ppqStart = pos->getPpqPosition().orFallback(0.0);
                const double bpm = currentBpm.load();
                const double sampleRate = juce::jmax(1.0, getSampleRate());
                const double ppqPerSample = bpm / (60.0 * sampleRate);
                const double blockEndPpq = ppqStart + (numSamples * ppqPerSample);
                const double stepPpq = 0.25;

                global16thNote.store(static_cast<int>(std::floor(ppqStart * 4.0)));

                for (auto& pending : pendingNoteOffs) {
                    if (pending.isActive && pending.endPpq < blockEndPpq) {
                        int sampleOffset = juce::jlimit(0, numSamples - 1, static_cast<int>((pending.endPpq - ppqStart) / ppqPerSample));
                        midiMessages.addEvent(juce::MidiMessage::noteOff(pending.channel, pending.note, 0.0f), sampleOffset);
                        pending.isActive = false;
                    }
                }

                const int firstCandidate16th = juce::jmax(0, static_cast<int>(std::floor(ppqStart * 4.0)) - 1);
                const int lastCandidate16th = juce::jmax(firstCandidate16th, static_cast<int>(std::ceil(blockEndPpq * 4.0)) + 1);

                const juce::ScopedLock sl(stateLock);
                for (int t = 0; t < 16; ++t) {
                    if (anySolo ? (soloParams[t]->load() < 0.5f) : (muteParams[t]->load() > 0.5f)) continue;

                    const int len = juce::jlimit(1, 32, trackLengths[t]);
                    const int note = static_cast<int>(noteParams[t]->load());

                    for (int absolute16th = firstCandidate16th; absolute16th <= lastCandidate16th; ++absolute16th) {
                        const int stepIdx = absolute16th % len;
                        const auto& step = sequencerMatrix[t][stepIdx];

                        if (!step.isActive) continue;
                        if (juce::Random::getSystemRandom().nextFloat() > step.probability) continue;

                        double baseEventPpq = absolute16th * stepPpq;
                        baseEventPpq += (step.shift * stepPpq * 0.5);
                        if ((absolute16th % 2) != 0) baseEventPpq += (step.swing * stepPpq);

                        double repeatIntervalPpq = stepPpq / step.repeat;

                        for (int r = 0; r < step.repeat; ++r) {
                            double eventPpq = baseEventPpq + (r * repeatIntervalPpq);
                            double endPpq = eventPpq + (repeatIntervalPpq * step.gate);

                            int sampleOffset = static_cast<int>(std::round((eventPpq - ppqStart) / ppqPerSample));

                            if (sampleOffset >= 0 && sampleOffset < numSamples) {
                                float vel = step.velocity * masterVolParam->load();
                                midiMessages.addEvent(juce::MidiMessage::noteOn(1, note, vel), sampleOffset);

                                for (auto& pending : pendingNoteOffs) {
                                    if (!pending.isActive) {
                                        pending = { 1, note, endPpq, true };
                                        break;
                                    }
                                }
                            }
                            else if (sampleOffset < 0 && endPpq > ppqStart) {
                                for (auto& pending : pendingNoteOffs) {
                                    if (!pending.isActive) {
                                        pending = { 1, note, endPpq, true };
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            else {
                global16thNote.store(-1);
                for (auto& pending : pendingNoteOffs) {
                    if (pending.isActive) {
                        midiMessages.addEvent(juce::MidiMessage::noteOff(pending.channel, pending.note, 0.0f), 0);
                        pending.isActive = false;
                    }
                }
            }
        }
    }
}

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

int MiniLAB3StepSequencerAudioProcessor::getGeneralMidiNote(int t) { return 36 + t; }
void MiniLAB3StepSequencerAudioProcessor::prepareToPlay(double, int) { global16thNote.store(-1); for (auto& p : pendingNoteOffs) p.isActive = false; }
void MiniLAB3StepSequencerAudioProcessor::releaseResources() {}
bool MiniLAB3StepSequencerAudioProcessor::isBusesLayoutSupported(const juce::AudioProcessor::BusesLayout& l) const { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
juce::AudioProcessorEditor* MiniLAB3StepSequencerAudioProcessor::createEditor() { return new MiniLAB3StepSequencerAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MiniLAB3StepSequencerAudioProcessor(); }

void MiniLAB3StepSequencerAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MiniLAB3StepSequencerAudioProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, size));
    if (xmlState != nullptr) {
        if (xmlState->hasTagName(apvts.state.getType())) {
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
        }
    }
}