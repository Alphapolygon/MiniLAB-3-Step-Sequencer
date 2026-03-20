#include "PluginProcessor.h"

namespace
{
    bool readBoolVar(const juce::var& value)
    {
        if (value.isBool()) return static_cast<bool>(value);
        if (value.isInt() || value.isDouble()) return static_cast<int>(value) > 0;
        return value.toString().trim().equalsIgnoreCase("true") || value.toString().trim() == "1";
    }

    float readFloatVar(const juce::var& value)
    {
        if (value.isDouble()) return static_cast<float>(static_cast<double>(value));
        if (value.isInt()) return static_cast<float>(static_cast<int>(value));
        return value.toString().getFloatValue();
    }

    void fillMissingPatterns(juce::Array<juce::var>& patterns)
    {
        while (patterns.size() < MiniLAB3Seq::kNumPatterns)
        {
            juce::DynamicObject::Ptr patternObject = new juce::DynamicObject();
            patternObject->setProperty("id", juce::Uuid().toString());
            patternObject->setProperty("name", "Pattern " + juce::String(MiniLAB3Seq::kPatternLabels[patterns.size()]));
            patternObject->setProperty("data", MiniLAB3Seq::makeEmptyPatternDataVar());
            patterns.add(juce::var(patternObject.get()));
        }
    }
} // namespace

void MiniLAB3StepSequencerAudioProcessor::setStepDataFromVar(const juce::var& stateVar)
{
    juce::var parsedVar = stateVar;

    if (parsedVar.isString())
        parsedVar = juce::JSON::parse(parsedVar.toString());

    if (parsedVar.isVoid() || !parsedVar.isObject())
        return;

    auto* object = parsedVar.getDynamicObject();
    if (object == nullptr)
        return;

    if (object->hasProperty("activePatternIndex"))
        activePatternIndex.store(juce::jlimit(0, MiniLAB3Seq::kNumPatterns - 1, static_cast<int>(object->getProperty("activePatternIndex"))));

    if (object->hasProperty("selectedTrack"))
        currentInstrument.store(juce::jlimit(0, MiniLAB3Seq::kNumTracks - 1, static_cast<int>(object->getProperty("selectedTrack"))));

    if (object->hasProperty("currentPage"))
        currentPage.store(juce::jlimit(0, MiniLAB3Seq::kNumPages - 1, static_cast<int>(object->getProperty("currentPage"))));

    const bool hasMatrixData =
        object->hasProperty("activeSteps") ||
        object->hasProperty("velocities") ||
        object->hasProperty("gates") ||
        object->hasProperty("probabilities") ||
        object->hasProperty("repeats") ||
        object->hasProperty("shifts") ||
        object->hasProperty("swings");

    if (hasMatrixData)
    {
        modifySequencerState([&](MatrixSnapshot& writeMatrix)
            {
                if (object->hasProperty("activeSteps"))
                {
                    if (auto* tracks = object->getProperty("activeSteps").getArray())
                    {
                        for (int track = 0; track < juce::jmin(MiniLAB3Seq::kNumTracks, tracks->size()); ++track)
                        {
                            if (auto* steps = tracks->getReference(track).getArray())
                            {
                                for (int step = 0; step < juce::jmin(MiniLAB3Seq::kNumSteps, steps->size()); ++step)
                                    writeMatrix[track][step].isActive = readBoolVar(steps->getReference(step));
                            }
                        }
                    }
                }

                const juce::StringArray matrixKeys{ "velocities", "gates", "probabilities", "repeats", "shifts", "swings" };

                for (const auto& key : matrixKeys)
                {
                    if (!object->hasProperty(key))
                        continue;

                    if (auto* tracks = object->getProperty(key).getArray())
                    {
                        for (int track = 0; track < juce::jmin(MiniLAB3Seq::kNumTracks, tracks->size()); ++track)
                        {
                            if (auto* steps = tracks->getReference(track).getArray())
                            {
                                for (int step = 0; step < juce::jmin(MiniLAB3Seq::kNumSteps, steps->size()); ++step)
                                {
                                    const float value = readFloatVar(steps->getReference(step));

                                    if (key == "velocities")             writeMatrix[track][step].velocity = value / 100.0f;
                                    else if (key == "gates")             writeMatrix[track][step].gate = value / 100.0f;
                                    else if (key == "probabilities")     writeMatrix[track][step].probability = value / 100.0f;
                                    else if (key == "repeats")           writeMatrix[track][step].repeats = static_cast<int>(value);
                                    else if (key == "shifts")            writeMatrix[track][step].shift = value / 100.0f;
                                    else if (key == "swings")            writeMatrix[track][step].swing = value / 100.0f;
                                }
                            }
                        }
                    }
                }
            });

        for (int track = 0; track < MiniLAB3Seq::kNumTracks; ++track)
            updateTrackLength(track);
    }

    if (object->hasProperty("midiKeys"))
    {
        if (auto* notes = object->getProperty("midiKeys").getArray())
        {
            for (int track = 0; track < juce::jmin(MiniLAB3Seq::kNumTracks, notes->size()); ++track)
            {
                const int midiNote = MiniLAB3Seq::parseMidiNoteName(notes->getReference(track).toString());
                setParameterFromPlainValue("note_" + juce::String(track + 1), static_cast<float>(midiNote));
            }
        }
    }

    if (object->hasProperty("trackStates"))
    {
        if (auto* trackStates = object->getProperty("trackStates").getArray())
        {
            for (int track = 0; track < juce::jmin(MiniLAB3Seq::kNumTracks, trackStates->size()); ++track)
            {
                if (auto* state = trackStates->getReference(track).getDynamicObject())
                {
                    const float muteValue = readBoolVar(state->getProperty("mute")) ? 1.0f : 0.0f;
                    const float soloValue = readBoolVar(state->getProperty("solo")) ? 1.0f : 0.0f;

                    setParameterFromPlainValue("mute_" + juce::String(track + 1), muteValue);
                    setParameterFromPlainValue("solo_" + juce::String(track + 1), soloValue);
                }
            }
        }
    }

    requestLedRefresh();
    // markUiStateDirty();
}

juce::var MiniLAB3StepSequencerAudioProcessor::buildCurrentPatternStateVar() const
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::DynamicObject::Ptr pattern = new juce::DynamicObject();

    juce::Array<juce::var> activeSteps, velocities, probabilities, gates, shifts, swings, repeats;
    juce::Array<juce::var> midiKeys, trackStates;

    const auto& readMatrix = getActiveMatrix();

    for (int track = 0; track < MiniLAB3Seq::kNumTracks; ++track)
    {
        juce::Array<juce::var> activeRow, velocityRow, probabilityRow, gateRow, shiftRow, swingRow, repeatRow;
        for (int step = 0; step < MiniLAB3Seq::kNumSteps; ++step)
        {
            const auto& stepData = readMatrix[track][step];
            activeRow.add(stepData.isActive);
            velocityRow.add(juce::jlimit(0, 100, static_cast<int>(std::round(stepData.velocity * 100.0f))));
            probabilityRow.add(juce::jlimit(0, 100, static_cast<int>(std::round(stepData.probability * 100.0f))));
            gateRow.add(juce::jlimit(0, 100, static_cast<int>(std::round(stepData.gate * 100.0f))));
            shiftRow.add(juce::jlimit(0, 100, static_cast<int>(std::round(stepData.shift * 100.0f))));
            swingRow.add(juce::jlimit(0, 100, static_cast<int>(std::round(stepData.swing * 100.0f))));
            repeatRow.add(stepData.repeats);
        }

        activeSteps.add(activeRow);
        velocities.add(velocityRow);
        probabilities.add(probabilityRow);
        gates.add(gateRow);
        shifts.add(shiftRow);
        swings.add(swingRow);
        repeats.add(repeatRow);

        midiKeys.add(MiniLAB3Seq::midiNoteToName(static_cast<int>(std::round(noteParams[track]->load()))));

        juce::DynamicObject::Ptr state = new juce::DynamicObject();
        state->setProperty("mute", muteParams[track]->load() > 0.5f);
        state->setProperty("solo", soloParams[track]->load() > 0.5f);
        trackStates.add(juce::var(state.get()));
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
    auto* currentStateObject = currentState.getDynamicObject();
    if (currentStateObject == nullptr) return {};

    const auto currentPatternData = currentStateObject->getProperty("patternData");
    const int patternIndex = juce::jlimit(0, MiniLAB3Seq::kNumPatterns - 1, activePatternIndex.load());

    auto wrapPatternsAsObject = [&](const juce::Array<juce::var>& sourcePatterns)
        {
            juce::DynamicObject::Ptr root = new juce::DynamicObject();
            juce::Array<juce::var> patterns = sourcePatterns;
            fillMissingPatterns(patterns);
            if (patterns.size() > patternIndex)
            {
                if (auto* patternObject = patterns.getReference(patternIndex).getDynamicObject())
                    patternObject->setProperty("data", currentPatternData);
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
            if (auto* patternsVar = root->getProperty("patterns").getArray())
            {
                fillMissingPatterns(*patternsVar);
                if (patternsVar->size() > patternIndex)
                {
                    if (auto* patternObject = patternsVar->getReference(patternIndex).getDynamicObject())
                        patternObject->setProperty("data", currentPatternData);
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
        return juce::JSON::toString(wrapPatternsAsObject(*patternArray));
    }

    juce::Array<juce::var> defaultPatterns;
    for (int i = 0; i < MiniLAB3Seq::kNumPatterns; ++i)
    {
        juce::DynamicObject::Ptr patternObject = new juce::DynamicObject();
        patternObject->setProperty("id", juce::Uuid().toString());
        patternObject->setProperty("name", "Pattern " + juce::String(MiniLAB3Seq::kPatternLabels[i]));
        patternObject->setProperty("data", i == patternIndex ? currentPatternData : MiniLAB3Seq::makeEmptyPatternDataVar());
        defaultPatterns.add(juce::var(patternObject.get()));
    }

    return juce::JSON::toString(wrapPatternsAsObject(defaultPatterns));
}

void MiniLAB3StepSequencerAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());

    auto* matrix = xml->createNewChildElement("Matrix");
    const auto& readMatrix = getActiveMatrix();

    for (int track = 0; track < MiniLAB3Seq::kNumTracks; ++track)
    {
        auto* trackElement = matrix->createNewChildElement("Track");
        trackElement->setAttribute("name", instrumentNames[track]);
        trackElement->setAttribute("length", trackLengths[track]);
        trackElement->setAttribute("midiChannel", trackMidiChannels[track].load());

        juce::String steps, velocities, gates, probabilities, repeats, shifts, swings;
        for (int step = 0; step < MiniLAB3Seq::kNumSteps; ++step)
        {
            const auto& sd = readMatrix[track][step];
            steps += sd.isActive ? "1" : "0";
            velocities += juce::String(sd.velocity) + ",";
            gates += juce::String(sd.gate) + ",";
            probabilities += juce::String(sd.probability) + ",";
            repeats += juce::String(sd.repeats) + ",";
            shifts += juce::String(sd.shift) + ",";
            swings += juce::String(sd.swing) + ",";
        }

        trackElement->setAttribute("steps", steps);
        trackElement->setAttribute("velocities", velocities);
        trackElement->setAttribute("gates", gates);
        trackElement->setAttribute("probabilities", probabilities);
        trackElement->setAttribute("repeats", repeats);
        trackElement->setAttribute("shifts", shifts);
        trackElement->setAttribute("swings", swings);
    }

    xml->setAttribute("activePatternIndex", activePatternIndex.load());

    auto* uiStateElement = xml->createNewChildElement("ReactUIState");
    uiStateElement->addTextElement(fullUiStateJson);

    copyXmlToBinary(*xml, destData);
}

void MiniLAB3StepSequencerAudioProcessor::setStateInformation(const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, size));
    if (xmlState == nullptr)
        return;

    if (xmlState->hasTagName(apvts.state.getType()))
    {
        // Deep-copy the XML so we can remove custom children before fromXml()
        auto apvtsXml = std::make_unique<juce::XmlElement>(*xmlState);

        for (auto* child = apvtsXml->getFirstChildElement(); child != nullptr;)
        {
            auto* next = child->getNextElement();

            if (child->hasTagName("Matrix") || child->hasTagName("ReactUIState"))
                apvtsXml->removeChildElement(child, true);

            child = next;
        }

        auto restoredTree = juce::ValueTree::fromXml(*apvtsXml);
        if (restoredTree.isValid())
            apvts.replaceState(restoredTree);
    }

    if (auto* matrix = xmlState->getChildByName("Matrix"))
    {
        auto* trackElement = matrix->getChildByName("Track");
        int track = 0;

        modifySequencerState([&](MatrixSnapshot& writeMatrix)
            {
                while (trackElement != nullptr && track < MiniLAB3Seq::kNumTracks)
                {
                    instrumentNames[track] = trackElement->getStringAttribute("name", instrumentNames[track]);
                    trackMidiChannels[track].store(trackElement->getIntAttribute("midiChannel", track + 1));

                    const auto steps = trackElement->getStringAttribute("steps");
                    juce::StringArray velocities, gates, probabilities, repeats, shifts, swings;
                    velocities.addTokens(trackElement->getStringAttribute("velocities"), ",", "");
                    gates.addTokens(trackElement->getStringAttribute("gates"), ",", "");
                    probabilities.addTokens(trackElement->getStringAttribute("probabilities"), ",", "");
                    repeats.addTokens(trackElement->getStringAttribute("repeats"), ",", "");
                    shifts.addTokens(trackElement->getStringAttribute("shifts"), ",", "");
                    swings.addTokens(trackElement->getStringAttribute("swings"), ",", "");

                    for (int step = 0; step < MiniLAB3Seq::kNumSteps; ++step)
                    {
                        writeMatrix[track][step].isActive = (step < steps.length() && steps[step] == '1');
                        if (step < velocities.size())    writeMatrix[track][step].velocity = velocities[step].getFloatValue();
                        if (step < gates.size())         writeMatrix[track][step].gate = gates[step].getFloatValue();
                        if (step < probabilities.size()) writeMatrix[track][step].probability = probabilities[step].getFloatValue();
                        if (step < repeats.size())       writeMatrix[track][step].repeats = repeats[step].getIntValue();
                        if (step < shifts.size())        writeMatrix[track][step].shift = shifts[step].getFloatValue();
                        if (step < swings.size())        writeMatrix[track][step].swing = swings[step].getFloatValue();
                    }

                    trackElement = trackElement->getNextElementWithTagName("Track");
                    ++track;
                }
            });
    }

    if (auto* uiState = xmlState->getChildByName("ReactUIState"))
        fullUiStateJson = uiState->getAllSubText();

    activePatternIndex.store(xmlState->getIntAttribute("activePatternIndex", 0));

    const auto savedUiState = juce::JSON::parse(fullUiStateJson);
    if (auto* savedUiObject = savedUiState.getDynamicObject())
    {
        if (savedUiObject->hasProperty("selectedTrack"))
            currentInstrument.store(
                juce::jlimit(0, MiniLAB3Seq::kNumTracks - 1,
                    static_cast<int>(savedUiObject->getProperty("selectedTrack"))));

        if (savedUiObject->hasProperty("currentPage"))
            currentPage.store(
                juce::jlimit(0, MiniLAB3Seq::kNumPages - 1,
                    static_cast<int>(savedUiObject->getProperty("currentPage"))));
    }

    for (int track = 0; track < MiniLAB3Seq::kNumTracks; ++track)
        updateTrackLength(track);

    requestLedRefresh();
    markUiStateDirty();
    initialising.store(false, std::memory_order_release);
}

