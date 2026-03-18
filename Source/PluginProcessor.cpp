#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <functional>

namespace
{
    static juce::MidiMessage makeMiniLab3PadColorSysex(uint8_t padIdx, uint8_t r, uint8_t g, uint8_t b)
    {
        const uint8_t data[] = { 0xF0, 0x00, 0x20, 0x6B, 0x7F, 0x42, 0x02, 0x01, 0x16, padIdx,
                                 static_cast<uint8_t>(r & 0x7F),
                                 static_cast<uint8_t>(g & 0x7F),
                                 static_cast<uint8_t>(b & 0x7F),
                                 0xF7 };
        return juce::MidiMessage(data, sizeof(data));
    }

    static juce::String joinStepFloatValues(const std::array<StepData, MiniLAB3StepSequencerAudioProcessor::numSteps>& steps,
                                            float StepData::* member)
    {
        juce::String result;
        for (int i = 0; i < MiniLAB3StepSequencerAudioProcessor::numSteps; ++i)
        {
            if (i > 0)
                result << ",";
            result << juce::String(steps[(size_t) i].*member, 6);
        }
        return result;
    }

    static juce::String joinStepIntValues(const std::array<StepData, MiniLAB3StepSequencerAudioProcessor::numSteps>& steps,
                                          int StepData::* member)
    {
        juce::String result;
        for (int i = 0; i < MiniLAB3StepSequencerAudioProcessor::numSteps; ++i)
        {
            if (i > 0)
                result << ",";
            result << juce::String(steps[(size_t) i].*member);
        }
        return result;
    }

    static juce::String joinActiveBits(const std::array<StepData, MiniLAB3StepSequencerAudioProcessor::numSteps>& steps)
    {
        juce::String result;
        for (const auto& step : steps)
            result << (step.isActive ? "1" : "0");
        return result;
    }

    static void parseFloatTokenList(const juce::String& text,
                                    const std::function<void(int, float)>& apply)
    {
        juce::StringArray tokens;
        tokens.addTokens(text, ",", "");
        for (int i = 0; i < tokens.size(); ++i)
            apply(i, tokens[i].getFloatValue());
    }

    static void parseIntTokenList(const juce::String& text,
                                  const std::function<void(int, int)>& apply)
    {
        juce::StringArray tokens;
        tokens.addTokens(text, ",", "");
        for (int i = 0; i < tokens.size(); ++i)
            apply(i, tokens[i].getIntValue());
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout MiniLAB3StepSequencerAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>("master_vol", "Master Volume", 0.0f, 1.0f, 0.8f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("swing", "Swing", 0.0f, 1.0f, 0.0f));

    for (int i = 0; i < numTracks; ++i)
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
    : AudioProcessor(BusesProperties()),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    masterVolParam = apvts.getRawParameterValue("master_vol");
    swingParam = apvts.getRawParameterValue("swing");

    for (int pattern = 0; pattern < numPatterns; ++pattern)
        patternNames[pattern] = "Pattern " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + pattern));

    const juce::ScopedLock sl(stateLock);
    for (int track = 0; track < numTracks; ++track)
    {
        instrumentNames[track] = "Track " + juce::String(track + 1);

        const juce::String trk = juce::String(track + 1);
        muteParams[track] = apvts.getRawParameterValue("mute_" + trk);
        soloParams[track] = apvts.getRawParameterValue("solo_" + trk);
        noteParams[track] = apvts.getRawParameterValue("note_" + trk);
        nudgeParams[track] = apvts.getRawParameterValue("nudge_" + trk);

        for (int pattern = 0; pattern < numPatterns; ++pattern)
        {
            for (int step = 0; step < numSteps; ++step)
            {
                patterns[(size_t) pattern][(size_t) track][(size_t) step] = {};
                lastFiredVelocity[track][step] = 0.0f;
            }
            patternTrackLengths[pattern][track] = stepsPerPage;
        }
    }

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

void MiniLAB3StepSequencerAudioProcessor::openHardwareOutput()
{
    if (hardwareOutput != nullptr || isAttemptingConnection)
        return;

    isAttemptingConnection = true;

    for (auto& device : juce::MidiOutput::getAvailableDevices())
    {
        if (device.name.containsIgnoreCase("Minilab3")
            && device.name.containsIgnoreCase("MIDI")
            && !device.name.containsIgnoreCase("DIN")
            && !device.name.containsIgnoreCase("MCU"))
        {
            hardwareOutput = juce::MidiOutput::openDevice(device.identifier);
            if (hardwareOutput)
            {
                for (int i = 0; i < stepsPerPage; ++i)
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
        for (int i = 0; i < stepsPerPage; ++i)
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

    const int pattern = currentPattern.load();
    const int page = currentPage.load();
    const int start = page * stepsPerPage;
    const int instrument = currentInstrument.load();
    const int current16th = global16thNote.load();
    const int loopSection = activeLoopSection.load();
    const int trackLen = getTrackLength(pattern, instrument);

    int playingStep = -1;
    if (current16th >= 0)
    {
        if (loopSection >= 0)
            playingStep = (loopSection * stepsPerPage) + (current16th % stepsPerPage);
        else
            playingStep = current16th % juce::jmax(1, trackLen);
    }

    const juce::ScopedLock sl(stateLock);
    const auto& trackSteps = patterns[(size_t) pattern][(size_t) instrument];

    for (int pad = 0; pad < stepsPerPage; ++pad)
    {
        const int step = start + pad;
        const auto& stepData = trackSteps[(size_t) step];
        const bool active = stepData.isActive;
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
            const float brightness = 0.1f + (juce::jlimit(0.0f, 1.0f, stepData.velocity) * 0.9f);
            const uint8_t c = static_cast<uint8_t>(127.0f * brightness);

            if (page == 0)      { r = 0; g = c; b = c; }
            else if (page == 1) { r = c; g = 0; b = c; }
            else if (page == 2) { r = c; g = c; b = 0; }
            else                { r = c; g = c; b = c; }
        }

        const PadColor newColor { r, g, b };
        if (forceOverwrite || newColor != lastPadColor[pad])
        {
            lastPadColor[pad] = newColor;
            hardwareOutput->sendMessageNow(makeMiniLab3PadColorSysex(getHardwarePadId(pad), r, g, b));
        }
    }
}

void MiniLAB3StepSequencerAudioProcessor::updateTrackLength(int trackIndex)
{
    updateTrackLength(currentPattern.load(), trackIndex);
}

void MiniLAB3StepSequencerAudioProcessor::updateTrackLength(int patternIndex, int trackIndex)
{
    if (!juce::isPositiveAndBelow(patternIndex, numPatterns) || !juce::isPositiveAndBelow(trackIndex, numTracks))
        return;

    const juce::ScopedLock sl(stateLock);
    int maxActiveStep = -1;
    const auto& track = patterns[(size_t) patternIndex][(size_t) trackIndex];
    for (int s = numSteps - 1; s >= 0; --s)
    {
        if (track[(size_t) s].isActive)
        {
            maxActiveStep = s;
            break;
        }
    }

    if (maxActiveStep >= 24)      patternTrackLengths[patternIndex][trackIndex] = 32;
    else if (maxActiveStep >= 16) patternTrackLengths[patternIndex][trackIndex] = 24;
    else if (maxActiveStep >= 8)  patternTrackLengths[patternIndex][trackIndex] = 16;
    else                          patternTrackLengths[patternIndex][trackIndex] = 8;
}

int MiniLAB3StepSequencerAudioProcessor::getTrackLength(int trackIndex) const
{
    return getTrackLength(currentPattern.load(), trackIndex);
}

int MiniLAB3StepSequencerAudioProcessor::getTrackLength(int patternIndex, int trackIndex) const
{
    if (!juce::isPositiveAndBelow(patternIndex, numPatterns) || !juce::isPositiveAndBelow(trackIndex, numTracks))
        return stepsPerPage;

    return juce::jlimit(stepsPerPage, numSteps, patternTrackLengths[patternIndex][trackIndex]);
}

StepData MiniLAB3StepSequencerAudioProcessor::getStepData(int trackIndex, int stepIndex) const
{
    return getStepData(currentPattern.load(), trackIndex, stepIndex);
}

StepData MiniLAB3StepSequencerAudioProcessor::getStepData(int patternIndex, int trackIndex, int stepIndex) const
{
    const juce::ScopedLock sl(stateLock);
    if (!juce::isPositiveAndBelow(patternIndex, numPatterns)
        || !juce::isPositiveAndBelow(trackIndex, numTracks)
        || !juce::isPositiveAndBelow(stepIndex, numSteps))
    {
        return {};
    }

    return patterns[(size_t) patternIndex][(size_t) trackIndex][(size_t) stepIndex];
}

void MiniLAB3StepSequencerAudioProcessor::setStepData(int trackIndex, int stepIndex, const StepData& step)
{
    setStepData(currentPattern.load(), trackIndex, stepIndex, step);
}

void MiniLAB3StepSequencerAudioProcessor::setStepData(int patternIndex, int trackIndex, int stepIndex, const StepData& step)
{
    if (!juce::isPositiveAndBelow(patternIndex, numPatterns)
        || !juce::isPositiveAndBelow(trackIndex, numTracks)
        || !juce::isPositiveAndBelow(stepIndex, numSteps))
        return;

    {
        const juce::ScopedLock sl(stateLock);
        auto clamped = step;
        clamped.velocity = juce::jlimit(0.0f, 1.0f, clamped.velocity);
        clamped.probability = juce::jlimit(0.0f, 1.0f, clamped.probability);
        clamped.gate = juce::jlimit(0.05f, 1.0f, clamped.gate);
        clamped.shift = juce::jlimit(-1.0f, 1.0f, clamped.shift);
        clamped.swing = juce::jlimit(0.0f, 1.0f, clamped.swing);
        clamped.repeats = juce::jlimit(1, 4, clamped.repeats);

        patterns[(size_t) patternIndex][(size_t) trackIndex][(size_t) stepIndex] = clamped;
    }

    updateTrackLength(patternIndex, trackIndex);
    requestLedRefresh();
}

void MiniLAB3StepSequencerAudioProcessor::toggleStepActive(int trackIndex, int stepIndex, float velocityIfEnabled)
{
    if (!juce::isPositiveAndBelow(trackIndex, numTracks) || !juce::isPositiveAndBelow(stepIndex, numSteps))
        return;

    {
        const juce::ScopedLock sl(stateLock);
        auto& step = patterns[(size_t) currentPattern.load()][(size_t) trackIndex][(size_t) stepIndex];
        step.isActive = !step.isActive;
        if (step.isActive)
            step.velocity = juce::jlimit(0.0f, 1.0f, velocityIfEnabled);
    }

    updateTrackLength(trackIndex);
    requestLedRefresh();
}

void MiniLAB3StepSequencerAudioProcessor::clearCurrentPageForCurrentInstrument()
{
    const int pattern = currentPattern.load();
    const int page = currentPage.load();
    const int instrument = currentInstrument.load();
    const int start = page * stepsPerPage;

    {
        const juce::ScopedLock sl(stateLock);
        for (int s = start; s < start + stepsPerPage; ++s)
            patterns[(size_t) pattern][(size_t) instrument][(size_t) s].isActive = false;
    }

    updateTrackLength(pattern, instrument);
    requestLedRefresh();
}

void MiniLAB3StepSequencerAudioProcessor::setCurrentPattern(int patternIndex)
{
    const int newPattern = juce::jlimit(0, numPatterns - 1, patternIndex);
    if (currentPattern.exchange(newPattern) != newPattern)
        requestLedRefresh();
}

int MiniLAB3StepSequencerAudioProcessor::getCurrentPattern() const
{
    return currentPattern.load();
}

juce::String MiniLAB3StepSequencerAudioProcessor::getPatternName(int patternIndex) const
{
    if (!juce::isPositiveAndBelow(patternIndex, numPatterns))
        return {};
    return patternNames[patternIndex];
}

void MiniLAB3StepSequencerAudioProcessor::setLoopSection(int sectionIndex)
{
    activeLoopSection.store(sectionIndex < 0 ? -1 : juce::jlimit(0, numPages - 1, sectionIndex));
}

int MiniLAB3StepSequencerAudioProcessor::getLoopSection() const
{
    return activeLoopSection.load();
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
            newInst = juce::jlimit(0, numTracks - 1, newInst);
            if (currentInstrument.exchange(newInst) != newInst)
                requestLedRefresh();
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
                const int pattern = currentPattern.load();
                const int instrument = currentInstrument.load();
                const int step = (page * stepsPerPage) + knobIdx;

                const juce::ScopedLock sl(stateLock);
                auto& stepData = patterns[(size_t) pattern][(size_t) instrument][(size_t) step];
                if (stepData.isActive)
                    stepData.velocity = juce::jlimit(0.0f, 1.0f, val / 127.0f);
            }
        }
        else if (cc == 114)
        {
            const auto page = currentPage.load();
            if (val > 64) currentPage.store(juce::jmin(numPages - 1, page + 1));
            else if (val < 64) currentPage.store(juce::jmax(0, page - 1));
            pageChangedTrigger.store(true);
            requestLedRefresh();
        }
        else if (cc == 115 && val == 127)
        {
            clearCurrentPageForCurrentInstrument();
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
    else if (msg.isNoteOn())
    {
        const int noteNumber = msg.getNoteNumber();
        if (noteNumber >= 36 && noteNumber <= 43)
        {
            const int page = currentPage.load();
            const int instrument = currentInstrument.load();
            const int step = (page * stepsPerPage) + (noteNumber - 36);

            {
                const juce::ScopedLock sl(stateLock);
                auto& stepData = patterns[(size_t) currentPattern.load()][(size_t) instrument][(size_t) step];
                stepData.isActive = !stepData.isActive;
                if (stepData.isActive)
                    stepData.velocity = juce::jlimit(0.0f, 1.0f, msg.getFloatVelocity());
            }

            updateTrackLength(instrument);
            requestLedRefresh();
        }
    }
}

void MiniLAB3StepSequencerAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    juce::MidiBuffer incoming;
    incoming.swapWith(midiMessages);
    for (const auto metadata : incoming)
        handleMidiInput(metadata.getMessage(), midiMessages);

    bool anySolo = false;
    for (int t = 0; t < numTracks; ++t)
    {
        if (soloParams[t]->load() > 0.5f)
        {
            anySolo = true;
            break;
        }
    }

    if (auto* playHead = getPlayHead())
    {
        if (auto pos = playHead->getPosition())
        {
            const double hostBpm = pos->getBpm().orFallback(120.0);
            lastKnownBpm.store(hostBpm);

            if (pos->getIsPlaying())
            {
                const double ppqStart = pos->getPpqPosition().orFallback(0.0);
                const double bpm = hostBpm;
                const double sampleRate = getSampleRate();
                const double ppqPerSample = bpm / (60.0 * juce::jmax(1.0, sampleRate));
                const double blockEndPpq = ppqStart + (numSamples * ppqPerSample);
                const double stepPpq = 0.25;
                const double globalSwingPpq = swingParam->load() * (stepPpq * 0.5);
                const int current16th = static_cast<int>(std::floor(ppqStart * 4.0));
                global16thNote.store(current16th);

                const int firstCandidate16th = juce::jmax(0, static_cast<int>(std::floor(ppqStart * 4.0)) - 1);
                const int lastCandidate16th = juce::jmax(firstCandidate16th,
                                                         static_cast<int>(std::ceil(blockEndPpq * 4.0)) + 1);

                const int pattern = currentPattern.load();
                const int loopSection = activeLoopSection.load();
                const juce::ScopedLock sl(stateLock);

                for (int track = 0; track < numTracks; ++track)
                {
                    const bool canPlay = anySolo ? (soloParams[track]->load() > 0.5f)
                                                 : (muteParams[track]->load() < 0.5f);
                    if (!canPlay)
                        continue;

                    const int note = static_cast<int>(noteParams[track]->load());
                    const double nudgePpqOffset = (nudgeParams[track]->load() * 0.001 * bpm / 60.0);
                    const int trackLen = juce::jmax(1, patternTrackLengths[pattern][track]);
                    const auto& trackSteps = patterns[(size_t) pattern][(size_t) track];

                    for (int absolute16th = firstCandidate16th; absolute16th <= lastCandidate16th; ++absolute16th)
                    {
                        int stepIdx = 0;
                        if (loopSection >= 0)
                            stepIdx = (loopSection * stepsPerPage) + (absolute16th % stepsPerPage);
                        else
                            stepIdx = absolute16th % trackLen;

                        const auto& step = trackSteps[(size_t) stepIdx];
                        if (!step.isActive)
                            continue;

                        if (step.probability < 0.999f && probabilityRandom.nextFloat() > step.probability)
                            continue;

                        const int repeats = juce::jlimit(1, 4, step.repeats);
                        const double repeatPpq = stepPpq / static_cast<double>(repeats);
                        const double stepShiftPpq = static_cast<double>(step.shift) * (stepPpq * 0.5);
                        const double oddSwingPpq = ((absolute16th % 2) != 0)
                            ? (globalSwingPpq + (static_cast<double>(step.swing) * (stepPpq * 0.5)))
                            : 0.0;

                        for (int repeatIndex = 0; repeatIndex < repeats; ++repeatIndex)
                        {
                            const double eventPpq = (absolute16th * stepPpq)
                                                  + (repeatIndex * repeatPpq)
                                                  + oddSwingPpq
                                                  + stepShiftPpq
                                                  + nudgePpqOffset;

                            const int noteOnSample = static_cast<int>(std::round((eventPpq - ppqStart) / ppqPerSample));
                            if (noteOnSample < 0 || noteOnSample >= numSamples)
                                continue;

                            const double noteOffPpq = eventPpq + (repeatPpq * juce::jlimit(0.05f, 1.0f, step.gate));
                            const int noteOffSample = juce::jlimit(noteOnSample,
                                                                    numSamples - 1,
                                                                    static_cast<int>(std::round((noteOffPpq - ppqStart) / ppqPerSample)));

                            const float velocity = juce::jlimit(0.0f, 1.0f, step.velocity * masterVolParam->load());
                            midiMessages.addEvent(juce::MidiMessage::noteOn(1, note, velocity), noteOnSample);
                            midiMessages.addEvent(juce::MidiMessage::noteOff(1, note, 0.0f), noteOffSample);
                        }

                        lastFiredVelocity[track][stepIdx] = step.velocity;
                    }
                }
            }
            else
            {
                global16thNote.store(-1);
            }
        }
    }
    else
    {
        global16thNote.store(-1);
    }

    const juce::ScopedLock sl(stateLock);
    for (int track = 0; track < numTracks; ++track)
        for (int step = 0; step < numSteps; ++step)
            lastFiredVelocity[track][step] *= 0.85f;
}

void MiniLAB3StepSequencerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());

    xml->setAttribute("currentPattern", currentPattern.load());
    xml->setAttribute("currentPage", currentPage.load());
    xml->setAttribute("currentInstrument", currentInstrument.load());
    xml->setAttribute("loopSection", activeLoopSection.load());

    auto* patternsXml = xml->createNewChildElement("Patterns");

    const juce::ScopedLock sl(stateLock);
    for (int pattern = 0; pattern < numPatterns; ++pattern)
    {
        auto* patternXml = patternsXml->createNewChildElement("Pattern");
        patternXml->setAttribute("index", pattern);
        patternXml->setAttribute("name", patternNames[pattern]);

        for (int track = 0; track < numTracks; ++track)
        {
            auto* trackXml = patternXml->createNewChildElement("Track");
            trackXml->setAttribute("index", track);
            trackXml->setAttribute("name", instrumentNames[track]);
            trackXml->setAttribute("length", patternTrackLengths[pattern][track]);

            const auto& steps = patterns[(size_t) pattern][(size_t) track];
            trackXml->setAttribute("active", joinActiveBits(steps));
            trackXml->setAttribute("velocity", joinStepFloatValues(steps, &StepData::velocity));
            trackXml->setAttribute("probability", joinStepFloatValues(steps, &StepData::probability));
            trackXml->setAttribute("gate", joinStepFloatValues(steps, &StepData::gate));
            trackXml->setAttribute("shift", joinStepFloatValues(steps, &StepData::shift));
            trackXml->setAttribute("stepSwing", joinStepFloatValues(steps, &StepData::swing));
            trackXml->setAttribute("repeats", joinStepIntValues(steps, &StepData::repeats));
        }
    }

    copyXmlToBinary(*xml, destData);
}

void MiniLAB3StepSequencerAudioProcessor::setStateInformation(const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, size));
    if (xmlState == nullptr)
        return;

    if (xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));

    currentPattern.store(xmlState->getIntAttribute("currentPattern", 0));
    currentPage.store(xmlState->getIntAttribute("currentPage", 0));
    currentInstrument.store(xmlState->getIntAttribute("currentInstrument", 0));
    activeLoopSection.store(xmlState->getIntAttribute("loopSection", -1));

    if (auto* patternsXml = xmlState->getChildByName("Patterns"))
    {
        const juce::ScopedLock sl(stateLock);
        forEachXmlChildElementWithTagName(*patternsXml, patternXml, "Pattern")
        {
            const int pattern = patternXml->getIntAttribute("index", -1);
            if (!juce::isPositiveAndBelow(pattern, numPatterns))
                continue;

            patternNames[pattern] = patternXml->getStringAttribute("name", patternNames[pattern]);

            forEachXmlChildElementWithTagName(*patternXml, trackXml, "Track")
            {
                const int track = trackXml->getIntAttribute("index", -1);
                if (!juce::isPositiveAndBelow(track, numTracks))
                    continue;

                instrumentNames[track] = trackXml->getStringAttribute("name", instrumentNames[track]);
                patternTrackLengths[pattern][track] = juce::jlimit(stepsPerPage, numSteps,
                                                                   trackXml->getIntAttribute("length", stepsPerPage));

                auto& steps = patterns[(size_t) pattern][(size_t) track];
                const auto active = trackXml->getStringAttribute("active");
                for (int step = 0; step < numSteps; ++step)
                    steps[(size_t) step].isActive = (step < active.length() && active[step] == '1');

                parseFloatTokenList(trackXml->getStringAttribute("velocity"), [&](int i, float value)
                {
                    if (juce::isPositiveAndBelow(i, numSteps)) steps[(size_t) i].velocity = juce::jlimit(0.0f, 1.0f, value);
                });
                parseFloatTokenList(trackXml->getStringAttribute("probability"), [&](int i, float value)
                {
                    if (juce::isPositiveAndBelow(i, numSteps)) steps[(size_t) i].probability = juce::jlimit(0.0f, 1.0f, value);
                });
                parseFloatTokenList(trackXml->getStringAttribute("gate"), [&](int i, float value)
                {
                    if (juce::isPositiveAndBelow(i, numSteps)) steps[(size_t) i].gate = juce::jlimit(0.05f, 1.0f, value);
                });
                parseFloatTokenList(trackXml->getStringAttribute("shift"), [&](int i, float value)
                {
                    if (juce::isPositiveAndBelow(i, numSteps)) steps[(size_t) i].shift = juce::jlimit(-1.0f, 1.0f, value);
                });
                parseFloatTokenList(trackXml->getStringAttribute("stepSwing"), [&](int i, float value)
                {
                    if (juce::isPositiveAndBelow(i, numSteps)) steps[(size_t) i].swing = juce::jlimit(0.0f, 1.0f, value);
                });
                parseIntTokenList(trackXml->getStringAttribute("repeats"), [&](int i, int value)
                {
                    if (juce::isPositiveAndBelow(i, numSteps)) steps[(size_t) i].repeats = juce::jlimit(1, 4, value);
                });
            }
        }
    }

    for (int pattern = 0; pattern < numPatterns; ++pattern)
        for (int track = 0; track < numTracks; ++track)
            updateTrackLength(pattern, track);

    requestLedRefresh();
}

int MiniLAB3StepSequencerAudioProcessor::getGeneralMidiNote(int t)
{
    return 36 + t;
}

void MiniLAB3StepSequencerAudioProcessor::prepareToPlay(double, int)
{
    global16thNote.store(-1);
}

void MiniLAB3StepSequencerAudioProcessor::releaseResources() {}

bool MiniLAB3StepSequencerAudioProcessor::isBusesLayoutSupported(const juce::AudioProcessor::BusesLayout& layouts) const
{
    juce::ignoreUnused(layouts);
    return true;
}

juce::AudioProcessorEditor* MiniLAB3StepSequencerAudioProcessor::createEditor()
{
    return new MiniLAB3StepSequencerAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MiniLAB3StepSequencerAudioProcessor();
}
