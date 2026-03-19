#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    static constexpr const char* kNoteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static constexpr const char* kPatternLabels[10] = { "A", "B", "C", "D", "E", "F", "G", "H", "I", "J" };

    juce::MidiMessage makeMiniLab3PadColorSysex(uint8_t padIdx, uint8_t r, uint8_t g, uint8_t b)
    {
        const uint8_t data[] = {
            0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02, 0x01, 0x16,
            padIdx,
            static_cast<uint8_t>(r & 0x7F),
            static_cast<uint8_t>(g & 0x7F),
            static_cast<uint8_t>(b & 0x7F),
            0xF7
        };
        return juce::MidiMessage(data, sizeof(data));
    }

    // FIXED: Strips quotes and safely parses the exact MIDI note
    int parseMidiNoteName(const juce::String& noteName)
    {
        auto s = noteName.replace("\"", "").replace("'", "").trim().toUpperCase();
        if (s.isEmpty()) return 36;

        int octaveIndex = 0;
        while (octaveIndex < s.length() && !juce::CharacterFunctions::isDigit(s[octaveIndex]) && s[octaveIndex] != '-') {
            octaveIndex++;
        }

        auto namePart = s.substring(0, octaveIndex).trim();
        auto octavePart = s.substring(octaveIndex).trim();

        int semitone = 0;
        for (int i = 0; i < 12; ++i) {
            if (namePart == kNoteNames[i]) {
                semitone = i;
                break;
            }
        }

        int octave = octavePart.isEmpty() ? 2 : octavePart.getIntValue();
        return juce::jlimit(0, 127, (octave + 1) * 12 + semitone);
    }

    juce::String midiNoteToName(int midiNote)
    {
        const int clamped = juce::jlimit(0, 127, midiNote);
        const int semitone = clamped % 12;
        const int octave = (clamped / 12) - 1;
        return juce::String(kNoteNames[semitone]) + juce::String(octave);
    }

    juce::var makeEmptyPatternDataVar()
    {
        juce::DynamicObject::Ptr data = new juce::DynamicObject();
        juce::Array<juce::var> activeSteps, velocities, probabilities, gates, shifts, swings, repeats;
        juce::Array<juce::var> midiKeys, trackStates;

        for (int t = 0; t < 16; ++t)
        {
            juce::Array<juce::var> activeRow, velocityRow, probabilityRow, gateRow, shiftRow, swingRow, repeatRow;
            for (int s = 0; s < 32; ++s)
            {
                activeRow.add(false);
                velocityRow.add(100);
                probabilityRow.add(100);
                gateRow.add(75);
                shiftRow.add(50);
                swingRow.add(0);
                repeatRow.add(1);
            }

            activeSteps.add(activeRow);
            velocities.add(velocityRow);
            probabilities.add(probabilityRow);
            gates.add(gateRow);
            shifts.add(shiftRow);
            swings.add(swingRow);
            repeats.add(repeatRow);
            midiKeys.add(midiNoteToName(36 + t));

            juce::DynamicObject::Ptr state = new juce::DynamicObject();
            state->setProperty("mute", false);
            state->setProperty("solo", false);
            trackStates.add(juce::var(state.get()));
        }

        data->setProperty("activeSteps", activeSteps);
        data->setProperty("velocities", velocities);
        data->setProperty("probabilities", probabilities);
        data->setProperty("gates", gates);
        data->setProperty("shifts", shifts);
        data->setProperty("swings", swings);
        data->setProperty("repeats", repeats);
        data->setProperty("midiKeys", midiKeys);
        data->setProperty("trackStates", trackStates);
        return juce::var(data.get());
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout MiniLAB3StepSequencerAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>("master_vol", "Master Volume", 0.0f, 1.0f, 0.8f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("swing", "Swing", 0.0f, 1.0f, 0.0f));

    for (int i = 0; i < 16; ++i)
    {
        const juce::String trk = juce::String(i + 1);
        layout.add(std::make_unique<juce::AudioParameterBool>("mute_" + trk, "Mute " + trk, false));
        layout.add(std::make_unique<juce::AudioParameterBool>("solo_" + trk, "Solo " + trk, false));
        layout.add(std::make_unique<juce::AudioParameterInt>("note_" + trk, "MIDI Note " + trk, 0, 127, 36 + i));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            "nudge_" + trk,
            "Micro-Timing " + trk,
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

    const char* names[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" };

    const juce::ScopedLock sl(stateLock);
    for (int i = 0; i < 16; ++i)
    {
        instrumentNames[i] = juce::String(names[i]);

        const juce::String trk = juce::String(i + 1);
        muteParams[i] = apvts.getRawParameterValue("mute_" + trk);
        soloParams[i] = apvts.getRawParameterValue("solo_" + trk);
        noteParams[i] = apvts.getRawParameterValue("note_" + trk);
        nudgeParams[i] = apvts.getRawParameterValue("nudge_" + trk);

        for (int s = 0; s < 32; ++s)
        {
            sequencerMatrix[i][s] = { false, 0.8f, 1.0f, 0.75f, 1, 0.5f, 0.0f };
            lastFiredVelocity[i][s] = 0.0f;
        }
        updateTrackLength(i);
    }

    openHardwareOutput();
    startTimer(20);
}

MiniLAB3StepSequencerAudioProcessor::~MiniLAB3StepSequencerAudioProcessor()
{
    stopTimer();
    resetHardwareState();
}

uint8_t MiniLAB3StepSequencerAudioProcessor::getHardwarePadId(int softwareIndex)
{
    return static_cast<uint8_t>(softwareIndex + 4);
}

void MiniLAB3StepSequencerAudioProcessor::markUiStateDirty() noexcept
{
    uiStateVersion.fetch_add(1, std::memory_order_relaxed);
}

void MiniLAB3StepSequencerAudioProcessor::setParameterFromPlainValue(const juce::String& parameterId, float plainValue)
{
    if (auto* parameter = apvts.getParameter(parameterId))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

void MiniLAB3StepSequencerAudioProcessor::openHardwareOutput()
{
    if (hardwareOutput != nullptr || isAttemptingConnection)
        return;

    isAttemptingConnection = true;
    for (auto& device : juce::MidiOutput::getAvailableDevices())
    {
        if (device.name.containsIgnoreCase("Minilab3") && device.name.containsIgnoreCase("MIDI")
            && !device.name.containsIgnoreCase("DIN") && !device.name.containsIgnoreCase("MCU"))
        {
            hardwareOutput = juce::MidiOutput::openDevice(device.identifier);
            if (hardwareOutput)
            {
                for (int i = 0; i < 8; ++i)
                    hardwareOutput->sendMessageNow(makeMiniLab3PadColorSysex(getHardwarePadId(i), 0, 0, 0));
                requestLedRefresh();
            }
            break;
        }
    }
    isAttemptingConnection = false;
}

void MiniLAB3StepSequencerAudioProcessor::resetHardwareState()
{
    if (hardwareOutput)
    {
        for (int i = 0; i < 8; ++i)
            hardwareOutput->sendMessageNow(makeMiniLab3PadColorSysex(getHardwarePadId(i), 0, 0, 0));
        juce::Thread::sleep(30);
    }
}

void MiniLAB3StepSequencerAudioProcessor::requestLedRefresh()
{
    updateHardwareLEDs(true);
    ledRefreshCountdown.store(3);
}

void MiniLAB3StepSequencerAudioProcessor::timerCallback()
{
    if (!hardwareOutput) {
        static int connectionRetry = 0;
        if (++connectionRetry >= 25) {
            openHardwareOutput();
            connectionRetry = 0;
        }
    }

    const int current = ledRefreshCountdown.load();
    if (current > 0)
    {
        updateHardwareLEDs(true);
        ledRefreshCountdown.store(current - 1);
    }
    else
    {
        updateHardwareLEDs(false);
    }
}

void MiniLAB3StepSequencerAudioProcessor::updateHardwareLEDs(bool forceOverwrite)
{
    if (!hardwareOutput)
        return;

    const int page = currentPage.load();
    const int start = page * 8;
    const int instrument = currentInstrument.load();
    const int current16th = global16thNote.load();
    const int trackLen = trackLengths[instrument];
    const int playingStep = (current16th >= 0 && trackLen > 0) ? (current16th % trackLen) : -1;

    const juce::ScopedLock sl(stateLock);
    for (int p = 0; p < 8; ++p)
    {
        const int step = start + p;
        const bool active = sequencerMatrix[instrument][step].isActive;
        const float vel = sequencerMatrix[instrument][step].velocity;
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
            const float brightness = 0.1f + (vel * 0.9f);
            const uint8_t c = static_cast<uint8_t>(127.0f * brightness);
            if (page == 0) { r = 0; g = c; b = c; }
            else if (page == 1) { r = c; g = 0; b = c; }
            else if (page == 2) { r = c; g = c; b = 0; }
            else if (page == 3) { r = c; g = c; b = c; }
        }

        const PadColor newColor{ r, g, b };
        if (forceOverwrite || newColor != lastPadColor[p])
        {
            lastPadColor[p] = newColor;
            hardwareOutput->sendMessageNow(makeMiniLab3PadColorSysex(getHardwarePadId(p), r, g, b));
        }
    }
}

void MiniLAB3StepSequencerAudioProcessor::updateTrackLength(int trackIndex)
{
    const juce::ScopedLock sl(stateLock);
    int maxActiveStep = -1;
    for (int s = 31; s >= 0; --s)
    {
        if (sequencerMatrix[trackIndex][s].isActive)
        {
            maxActiveStep = s;
            break;
        }
    }

    if (maxActiveStep >= 24)      trackLengths[trackIndex] = 32;
    else if (maxActiveStep >= 16) trackLengths[trackIndex] = 24;
    else if (maxActiveStep >= 8)  trackLengths[trackIndex] = 16;
    else                          trackLengths[trackIndex] = 8;
}

void MiniLAB3StepSequencerAudioProcessor::handleMidiInput(const juce::MidiMessage& msg, juce::MidiBuffer&)
{
    if (msg.isController())
    {
        const int cc = msg.getControllerNumber();
        const int val = msg.getControllerValue();

        if (cc == 1)
        {
            int newInst = (127 - val) / 8;
            newInst = juce::jlimit(0, 15, newInst);
            if (currentInstrument.load() != newInst)
            {
                currentInstrument.store(newInst);
                requestLedRefresh();
                markUiStateDirty();
            }
        }
        else if (cc == 74 || cc == 71 || cc == 76 || cc == 77 || cc == 93 || cc == 18 || cc == 19 || cc == 16)
        {
            int knobIdx = -1;
            if (cc == 74) knobIdx = 0;
            else if (cc == 71) knobIdx = 1;
            else if (cc == 76) knobIdx = 2;
            else if (cc == 77) knobIdx = 3;
            else if (cc == 93) knobIdx = 4;
            else if (cc == 18) knobIdx = 5;
            else if (cc == 19) knobIdx = 6;
            else if (cc == 16) knobIdx = 7;

            if (knobIdx >= 0)
            {
                const int page = currentPage.load();
                const int instrument = currentInstrument.load();
                const int step = (page * 8) + knobIdx;
                bool changed = false;
                {
                    const juce::ScopedLock sl(stateLock);
                    if (sequencerMatrix[instrument][step].isActive)
                    {
                        sequencerMatrix[instrument][step].velocity = val / 127.0f;
                        changed = true;
                    }
                }
                if (changed)
                {
                    requestLedRefresh();
                    markUiStateDirty();
                }
            }
        }
        else if (cc == 114)
        {
            const auto page = currentPage.load();
            if (val > 64) currentPage.store(juce::jmin(3, page + 1));
            else if (val < 64) currentPage.store(juce::jmax(0, page - 1));
            pageChangedTrigger.store(true);
            requestLedRefresh();
            markUiStateDirty();
        }
        else if (cc == 115 && val == 127)
        {
            const int page = currentPage.load();
            const int instrument = currentInstrument.load();
            {
                const juce::ScopedLock sl(stateLock);
                for (int s = page * 8; s < (page * 8) + 8; ++s)
                    sequencerMatrix[instrument][s].isActive = false;
            }
            updateTrackLength(instrument);
            requestLedRefresh();
            markUiStateDirty();
        }
        else if (cc == 7)
        {
            if (auto* p = apvts.getParameter("master_vol"))
                p->setValueNotifyingHost(val / 127.0f);
        }
        else if (cc == 11)
        {
            if (auto* p = apvts.getParameter("swing"))
                p->setValueNotifyingHost(val / 127.0f);
        }
    }
    else if (msg.isNoteOn() || msg.isNoteOff())
    {
        const int n = msg.getNoteNumber();
        // Hardware pad handling
        if (n >= 36 && n <= 43 && msg.isNoteOn())
        {
            const int page = currentPage.load();
            const int instrument = currentInstrument.load();
            const int step = (page * 8) + (n - 36);

            {
                const juce::ScopedLock sl(stateLock);
                sequencerMatrix[instrument][step].isActive = !sequencerMatrix[instrument][step].isActive;
                if (sequencerMatrix[instrument][step].isActive)
                    sequencerMatrix[instrument][step].velocity = msg.getFloatVelocity();
            }
            updateTrackLength(instrument);
            requestLedRefresh();
            markUiStateDirty();
        }
    }
}

void MiniLAB3StepSequencerAudioProcessor::prepareToPlay(double, int)
{
    global16thNote.store(-1);
    lastProcessedStep = -1;
    for (auto& event : eventQueue)
        event.isActive = false;
}

void MiniLAB3StepSequencerAudioProcessor::scheduleMidiEvent(double ppqTime, const juce::MidiMessage& msg)
{
    for (auto& event : eventQueue)
    {
        if (!event.isActive)
        {
            event.ppqTime = ppqTime;
            event.message = msg;
            event.isActive = true;
            return;
        }
    }
}

void MiniLAB3StepSequencerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    const int numSamples = buffer.getNumSamples();
    juce::MidiBuffer incoming;
    incoming.swapWith(midiMessages);

    for (const auto metadata : incoming)
    {
        auto msg = metadata.getMessage();
        handleMidiInput(msg, midiMessages);

        // FIXED: Block the Minilab hardware controls from leaking into the synth and overriding the track notes!
        bool isHardwareControl = false;
        if (msg.isNoteOn() || msg.isNoteOff()) {
            if (msg.getNoteNumber() >= 36 && msg.getNoteNumber() <= 43) isHardwareControl = true;
        }
        if (msg.isController()) {
            int cc = msg.getControllerNumber();
            if (cc == 1 || cc == 7 || cc == 11 || cc == 114 || cc == 115 || cc == 74 || cc == 71 || cc == 76 || cc == 77 || cc == 93 || cc == 18 || cc == 19 || cc == 16) {
                isHardwareControl = true;
            }
        }

        if (!isHardwareControl) {
            midiMessages.addEvent(msg, metadata.samplePosition);
        }
    }

    buffer.clear();

    bool anySolo = false;
    for (int t = 0; t < 16; ++t)
    {
        if (soloParams[t]->load() > 0.5f)
        {
            anySolo = true;
            break;
        }
    }

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            const bool playing = pos->getIsPlaying();
            isPlaying.store(playing);

            if (playing)
            {
                const double ppqStart = pos->getPpqPosition().orFallback(0.0);
                const double bpm = pos->getBpm().orFallback(120.0);
                const double sampleRate = getSampleRate();
                const double ppqPerSample = bpm / (60.0 * juce::jmax(1.0, sampleRate));
                const double blockEndPpq = ppqStart + (numSamples * ppqPerSample);

                currentBpm.store(bpm);
                if (ppqStart < (lastProcessedStep * 0.25))
                {
                    lastProcessedStep = static_cast<int>(std::floor(ppqStart * 4.0)) - 1;
                    for (auto& ev : eventQueue)
                        ev.isActive = false;
                    for (int i = 1; i <= 16; ++i)
                        midiMessages.addEvent(juce::MidiMessage::allNotesOff(i), 0);
                }

                const int firstStep = static_cast<int>(std::floor(ppqStart * 4.0));
                const int lastStep = static_cast<int>(std::floor(blockEndPpq * 4.0));

                const juce::ScopedLock sl(stateLock);
                for (int step = firstStep; step <= lastStep; ++step)
                {
                    if (step > lastProcessedStep)
                    {
                        lastProcessedStep = step;
                        const int gridStep = step % 32;
                        global16thNote.store(step); // Stores absolute step

                        for (int t = 0; t < 16; ++t)
                        {
                            const bool canPlay = anySolo ? (soloParams[t]->load() > 0.5f) : (muteParams[t]->load() < 0.5f);
                            if (!canPlay)
                                continue;

                            const int len = juce::jlimit(1, 32, trackLengths[t]);
                            const int wrappedStep = step % len;
                            const StepData& stepData = sequencerMatrix[t][wrappedStep];

                            if (!stepData.isActive)
                                continue;

                            const float prob = stepData.probability;
                            if ((juce::Random::getSystemRandom().nextInt(100) / 100.0f) >= prob)
                                continue;

                            const int note = static_cast<int>(std::round(noteParams[t]->load()));
                            const float vel = juce::jlimit(0.0f, 1.0f, stepData.velocity * masterVolParam->load());
                            const float gate = juce::jlimit(0.0f, 1.0f, stepData.gate);
                            const int repeats = juce::jlimit(1, 4, stepData.repeats);
                            const float shift = stepData.shift;
                            const float stepSwing = juce::jlimit(0.0f, 1.0f, stepData.swing);

                            constexpr double stepLengthPpq = 0.25;
                            const double shiftPpq = (shift - 0.5f) * (stepLengthPpq / 2.0);
                            const double globalSwingPpqOffset = (step % 2 != 0) ? (swingParam->load() * (stepLengthPpq / 2.0)) : 0.0;
                            const double localSwingPpqOffset = (step % 2 != 0) ? (stepSwing * (stepLengthPpq / 2.0)) : 0.0;
                            const double nudgePpqOffset = (nudgeParams[t]->load() * 0.001 * bpm / 60.0);
                            const double basePpq = (step * 0.25) + shiftPpq + globalSwingPpqOffset + localSwingPpqOffset + nudgePpqOffset;
                            const double repeatIntervalPpq = stepLengthPpq / repeats;

                            for (int r = 0; r < repeats; ++r)
                            {
                                const double onPpq = basePpq + (r * repeatIntervalPpq);
                                const double offPpq = onPpq + (repeatIntervalPpq * gate);

                                scheduleMidiEvent(onPpq, juce::MidiMessage::noteOn(1, note, vel));
                                scheduleMidiEvent(offPpq, juce::MidiMessage::noteOff(1, note, 0.0f));
                            }

                            lastFiredVelocity[t][wrappedStep] = vel;
                        }
                    }
                }

                for (auto& event : eventQueue)
                {
                    if (!event.isActive)
                        continue;

                    if (event.ppqTime >= ppqStart && event.ppqTime < blockEndPpq)
                    {
                        const double ratio = (event.ppqTime - ppqStart) / (blockEndPpq - ppqStart);
                        int sampleOffset = static_cast<int>(ratio * numSamples);
                        sampleOffset = juce::jlimit(0, numSamples - 1, sampleOffset);
                        midiMessages.addEvent(event.message, sampleOffset);
                        event.isActive = false;
                    }
                    else if (event.ppqTime < ppqStart)
                    {
                        if (event.message.isNoteOff())
                            midiMessages.addEvent(event.message, 0);
                        event.isActive = false;
                    }
                }
            }
            else
            {
                if (lastProcessedStep != -1)
                {
                    global16thNote.store(-1);
                    lastProcessedStep = -1;
                    for (auto& ev : eventQueue)
                        ev.isActive = false;
                    for (int i = 1; i <= 16; ++i)
                        midiMessages.addEvent(juce::MidiMessage::allNotesOff(i), 0);
                }
            }
        }
    }

    const juce::ScopedLock sl(stateLock);
    for (int t = 0; t < 16; ++t)
        for (int s = 0; s < 32; ++s)
            lastFiredVelocity[t][s] *= 0.85f;
}

void MiniLAB3StepSequencerAudioProcessor::setStepDataFromJson(const juce::String& jsonStr)
{
    const auto parsedJson = juce::JSON::parse(jsonStr);
    if (parsedJson.isVoid() || !parsedJson.isObject())
        return;

    auto* obj = parsedJson.getDynamicObject();
    if (obj == nullptr)
        return;

    if (obj->hasProperty("activePatternIndex"))
        activePatternIndex.store(juce::jlimit(0, 9, static_cast<int>(obj->getProperty("activePatternIndex"))));

    if (obj->hasProperty("selectedTrack"))
        currentInstrument.store(juce::jlimit(0, 15, static_cast<int>(obj->getProperty("selectedTrack"))));

    if (obj->hasProperty("currentPage"))
        currentPage.store(juce::jlimit(0, 3, static_cast<int>(obj->getProperty("currentPage"))));

    {
        const juce::ScopedLock sl(stateLock);

        if (obj->hasProperty("activeSteps"))
        {
            if (auto* tracksArray = obj->getProperty("activeSteps").getArray())
            {
                for (int t = 0; t < juce::jmin(16, tracksArray->size()); ++t)
                {
                    if (auto* stepsArray = tracksArray->getReference(t).getArray())
                    {
                        for (int s = 0; s < juce::jmin(32, stepsArray->size()); ++s) {
                            // FIXED: Explicit bool extraction handles array toggles perfectly
                            auto& v = stepsArray->getReference(s);
                            if (v.isBool() || v.isInt()) sequencerMatrix[t][s].isActive = static_cast<bool>(v);
                            else sequencerMatrix[t][s].isActive = (v.toString().equalsIgnoreCase("true") || v.toString() == "1");
                        }
                    }
                }
            }
        }

        const juce::String parameters[] = { "velocities", "gates", "probabilities", "repeats", "shifts", "swings" };
        for (const auto& param : parameters)
        {
            if (!obj->hasProperty(param))
                continue;

            if (auto* tracksArray = obj->getProperty(param).getArray())
            {
                for (int t = 0; t < juce::jmin(16, tracksArray->size()); ++t)
                {
                    if (auto* stepsArray = tracksArray->getReference(t).getArray())
                    {
                        for (int s = 0; s < juce::jmin(32, stepsArray->size()); ++s)
                        {
                            // FIXED: Aggressive extraction. Handles both numbers and strings safely so values never drop to 0!
                            auto& v = stepsArray->getReference(s);
                            float val = 0.0f;
                            if (v.isDouble() || v.isInt()) val = static_cast<float>(static_cast<double>(v));
                            else val = v.toString().getFloatValue();

                            if (param == "velocities") sequencerMatrix[t][s].velocity = val / 100.0f;
                            else if (param == "gates") sequencerMatrix[t][s].gate = val / 100.0f;
                            else if (param == "probabilities") sequencerMatrix[t][s].probability = val / 100.0f;
                            else if (param == "repeats") sequencerMatrix[t][s].repeats = static_cast<int>(val);
                            else if (param == "shifts") sequencerMatrix[t][s].shift = val / 100.0f;
                            else if (param == "swings") sequencerMatrix[t][s].swing = val / 100.0f;
                        }
                    }
                }
            }
        }
    }

    if (obj->hasProperty("midiKeys"))
    {
        if (auto* notesArray = obj->getProperty("midiKeys").getArray())
        {
            for (int t = 0; t < juce::jmin(16, notesArray->size()); ++t)
            {
                const int midiNote = parseMidiNoteName(notesArray->getReference(t).toString());
                setParameterFromPlainValue("note_" + juce::String(t + 1), static_cast<float>(midiNote));
            }
        }
    }

    if (obj->hasProperty("trackStates"))
    {
        if (auto* statesArray = obj->getProperty("trackStates").getArray())
        {
            for (int t = 0; t < juce::jmin(16, statesArray->size()); ++t)
            {
                if (auto* stateObj = statesArray->getReference(t).getDynamicObject())
                {
                    const juce::String muteStr = stateObj->getProperty("mute").toString();
                    const juce::String soloStr = stateObj->getProperty("solo").toString();
                    setParameterFromPlainValue("mute_" + juce::String(t + 1), muteStr.equalsIgnoreCase("true") ? 1.0f : 0.0f);
                    setParameterFromPlainValue("solo_" + juce::String(t + 1), soloStr.equalsIgnoreCase("true") ? 1.0f : 0.0f);
                }
            }
        }
    }

    for (int t = 0; t < 16; ++t)
        updateTrackLength(t);

    requestLedRefresh();
    markUiStateDirty();
}

juce::var MiniLAB3StepSequencerAudioProcessor::buildCurrentPatternStateVar() const
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::DynamicObject::Ptr pattern = new juce::DynamicObject();

    juce::Array<juce::var> activeSteps, velocities, probabilities, gates, shifts, swings, repeats;
    juce::Array<juce::var> midiKeys, trackStates;

    {
        const juce::ScopedLock sl(stateLock);
        for (int t = 0; t < 16; ++t)
        {
            juce::Array<juce::var> activeRow, velocityRow, probabilityRow, gateRow, shiftRow, swingRow, repeatRow;
            for (int s = 0; s < 32; ++s)
            {
                const StepData& step = sequencerMatrix[t][s];
                activeRow.add(step.isActive);
                velocityRow.add(juce::jlimit(0, 100, static_cast<int>(std::round(step.velocity * 100.0f))));
                probabilityRow.add(juce::jlimit(0, 100, static_cast<int>(std::round(step.probability * 100.0f))));
                gateRow.add(juce::jlimit(0, 100, static_cast<int>(std::round(step.gate * 100.0f))));
                shiftRow.add(juce::jlimit(0, 100, static_cast<int>(std::round(step.shift * 100.0f))));
                swingRow.add(juce::jlimit(0, 100, static_cast<int>(std::round(step.swing * 100.0f))));
                repeatRow.add(step.repeats);
            }

            activeSteps.add(activeRow);
            velocities.add(velocityRow);
            probabilities.add(probabilityRow);
            gates.add(gateRow);
            shifts.add(shiftRow);
            swings.add(swingRow);
            repeats.add(repeatRow);

            midiKeys.add(midiNoteToName(static_cast<int>(std::round(noteParams[t]->load()))));

            juce::DynamicObject::Ptr state = new juce::DynamicObject();
            state->setProperty("mute", muteParams[t]->load() > 0.5f);
            state->setProperty("solo", soloParams[t]->load() > 0.5f);
            trackStates.add(juce::var(state.get()));
        }
    }

    pattern->setProperty("activeSteps", activeSteps);
    pattern->setProperty("velocities", velocities);
    pattern->setProperty("probabilities", probabilities);
    pattern->setProperty("gates", gates);
    pattern->setProperty("shifts", shifts);
    pattern->setProperty("swings", swings);
    pattern->setProperty("repeats", repeats);
    pattern->setProperty("midiKeys", midiKeys);
    pattern->setProperty("trackStates", trackStates);

    root->setProperty("patternData", juce::var(pattern.get()));
    root->setProperty("currentInstrument", currentInstrument.load());
    root->setProperty("currentPage", currentPage.load());
    root->setProperty("activePatternIndex", activePatternIndex.load());
    return juce::var(root.get());
}

juce::String MiniLAB3StepSequencerAudioProcessor::buildFullUiStateJsonForEditor() const
{
    const auto currentState = buildCurrentPatternStateVar();
    auto* currentStateObj = currentState.getDynamicObject();
    if (currentStateObj == nullptr)
        return {};

    const auto currentPatternData = currentStateObj->getProperty("patternData");
    const int patternIndex = juce::jlimit(0, 9, activePatternIndex.load());

    auto wrapArrayAsObject = [&](const juce::Array<juce::var>& sourcePatterns)
        {
            juce::DynamicObject::Ptr root = new juce::DynamicObject();
            juce::Array<juce::var> patterns = sourcePatterns;

            while (patterns.size() < 10)
            {
                juce::DynamicObject::Ptr patternObj = new juce::DynamicObject();
                patternObj->setProperty("id", juce::Uuid().toString());
                patternObj->setProperty("name", "Pattern " + juce::String(kPatternLabels[patterns.size()]));
                patternObj->setProperty("data", makeEmptyPatternDataVar());
                patterns.add(juce::var(patternObj.get()));
            }

            if (patterns.size() > patternIndex)
            {
                if (auto* patternObj = patterns.getReference(patternIndex).getDynamicObject())
                    patternObj->setProperty("data", currentPatternData);
            }

            root->setProperty("patterns", patterns);
            root->setProperty("activeIdx", patternIndex);
            root->setProperty("themeIdx", 0);
            root->setProperty("selectedTrack", currentInstrument.load());
            root->setProperty("currentPage", currentPage.load());
            root->setProperty("footerTab", "Velocity");
            return juce::var(root.get());
        };

    auto parsed = juce::JSON::parse(fullUiStateJson);
    if (parsed.isObject())
    {
        if (auto* root = parsed.getDynamicObject())
        {
            auto patternsVar = root->getProperty("patterns");
            if (auto* patterns = patternsVar.getArray())
            {
                while (patterns->size() < 10)
                {
                    juce::DynamicObject::Ptr patternObj = new juce::DynamicObject();
                    patternObj->setProperty("id", juce::Uuid().toString());
                    patternObj->setProperty("name", "Pattern " + juce::String(kPatternLabels[patterns->size()]));
                    patternObj->setProperty("data", makeEmptyPatternDataVar());
                    patterns->add(juce::var(patternObj.get()));
                }

                if (patterns->size() > patternIndex)
                {
                    if (auto* patternObj = patterns->getReference(patternIndex).getDynamicObject())
                        patternObj->setProperty("data", currentPatternData);
                }

                root->setProperty("activeIdx", patternIndex);
                root->setProperty("selectedTrack", currentInstrument.load());
                root->setProperty("currentPage", currentPage.load());
                if (!root->hasProperty("themeIdx")) root->setProperty("themeIdx", 0);
                if (!root->hasProperty("footerTab")) root->setProperty("footerTab", "Velocity");
                return juce::JSON::toString(parsed);
            }
        }
    }
    else if (auto* patternArray = parsed.getArray())
    {
        return juce::JSON::toString(wrapArrayAsObject(*patternArray));
    }

    juce::Array<juce::var> defaultPatterns;
    for (int i = 0; i < 10; ++i)
    {
        juce::DynamicObject::Ptr patternObj = new juce::DynamicObject();
        patternObj->setProperty("id", juce::Uuid().toString());
        patternObj->setProperty("name", "Pattern " + juce::String(kPatternLabels[i]));
        patternObj->setProperty("data", i == patternIndex ? currentPatternData : makeEmptyPatternDataVar());
        defaultPatterns.add(juce::var(patternObj.get()));
    }

    return juce::JSON::toString(wrapArrayAsObject(defaultPatterns));
}

void MiniLAB3StepSequencerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());

    auto* matrix = xml->createNewChildElement("Matrix");
    const juce::ScopedLock sl(stateLock);
    for (int t = 0; t < 16; ++t)
    {
        auto* trk = matrix->createNewChildElement("Track");
        trk->setAttribute("name", instrumentNames[t]);
        trk->setAttribute("length", trackLengths[t]);

        juce::String steps, vels, gates, probs, repeats, shifts, swings;
        for (int s = 0; s < 32; ++s)
        {
            steps += sequencerMatrix[t][s].isActive ? "1" : "0";
            vels += juce::String(sequencerMatrix[t][s].velocity) + ",";
            gates += juce::String(sequencerMatrix[t][s].gate) + ",";
            probs += juce::String(sequencerMatrix[t][s].probability) + ",";
            repeats += juce::String(sequencerMatrix[t][s].repeats) + ",";
            shifts += juce::String(sequencerMatrix[t][s].shift) + ",";
            swings += juce::String(sequencerMatrix[t][s].swing) + ",";
        }
        trk->setAttribute("steps", steps);
        trk->setAttribute("velocities", vels);
        trk->setAttribute("gates", gates);
        trk->setAttribute("probabilities", probs);
        trk->setAttribute("repeats", repeats);
        trk->setAttribute("shifts", shifts);
        trk->setAttribute("swings", swings);
    }

    xml->setAttribute("activePatternIndex", activePatternIndex.load());

    // FIXED: Safely store the 50kb JSON string out of attribute jail
    auto* uiState = xml->createNewChildElement("ReactUIState");
    uiState->addTextElement(fullUiStateJson);

    copyXmlToBinary(*xml, destData);
}

void MiniLAB3StepSequencerAudioProcessor::setStateInformation(const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, size));
    if (xmlState != nullptr)
    {
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));

        if (auto* matrix = xmlState->getChildByName("Matrix"))
        {
            const juce::ScopedLock sl(stateLock);
            auto* trk = matrix->getChildByName("Track");
            int t = 0;
            while (trk != nullptr && t < 16)
            {
                instrumentNames[t] = trk->getStringAttribute("name", instrumentNames[t]);

                const juce::String steps = trk->getStringAttribute("steps");
                juce::StringArray vels, gates, probs, repeats, shifts, swings;
                vels.addTokens(trk->getStringAttribute("velocities"), ",", "");
                gates.addTokens(trk->getStringAttribute("gates"), ",", "");
                probs.addTokens(trk->getStringAttribute("probabilities"), ",", "");
                repeats.addTokens(trk->getStringAttribute("repeats"), ",", "");
                shifts.addTokens(trk->getStringAttribute("shifts"), ",", "");
                swings.addTokens(trk->getStringAttribute("swings"), ",", "");

                for (int s = 0; s < 32; ++s)
                {
                    sequencerMatrix[t][s].isActive = (s < steps.length() && steps[s] == '1');
                    if (s < vels.size()) sequencerMatrix[t][s].velocity = vels[s].getFloatValue();
                    if (s < gates.size()) sequencerMatrix[t][s].gate = gates[s].getFloatValue();
                    if (s < probs.size()) sequencerMatrix[t][s].probability = probs[s].getFloatValue();
                    if (s < repeats.size()) sequencerMatrix[t][s].repeats = repeats[s].getIntValue();
                    if (s < shifts.size()) sequencerMatrix[t][s].shift = shifts[s].getFloatValue();
                    if (s < swings.size()) sequencerMatrix[t][s].swing = swings[s].getFloatValue();
                }
                trk = trk->getNextElementWithTagName("Track");
                ++t;
            }
        }

        // FIXED: Retrieve the JSON from the dedicated text element
        if (auto* uiState = xmlState->getChildByName("ReactUIState")) {
            fullUiStateJson = uiState->getAllSubText();
        }
        activePatternIndex.store(xmlState->getIntAttribute("activePatternIndex", 0));

        const auto savedUiState = juce::JSON::parse(fullUiStateJson);
        if (auto* savedUiObj = savedUiState.getDynamicObject())
        {
            if (savedUiObj->hasProperty("selectedTrack"))
                currentInstrument.store(juce::jlimit(0, 15, static_cast<int>(savedUiObj->getProperty("selectedTrack"))));

            if (savedUiObj->hasProperty("currentPage"))
                currentPage.store(juce::jlimit(0, 3, static_cast<int>(savedUiObj->getProperty("currentPage"))));
        }
    }

    for (int t = 0; t < 16; ++t)
        updateTrackLength(t);

    requestLedRefresh();
    markUiStateDirty();
}

int MiniLAB3StepSequencerAudioProcessor::getGeneralMidiNote(int t) { return 36 + t; }
void MiniLAB3StepSequencerAudioProcessor::releaseResources() {}
bool MiniLAB3StepSequencerAudioProcessor::isBusesLayoutSupported(const juce::AudioProcessor::BusesLayout& layouts) const { return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
juce::AudioProcessorEditor* MiniLAB3StepSequencerAudioProcessor::createEditor() { return new MiniLAB3StepSequencerAudioProcessorEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MiniLAB3StepSequencerAudioProcessor(); }