#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <pluginterfaces/base/ftypes.h>

namespace
{
    static juce::Colour withAlpha(const juce::Colour& colour, float alpha)
    {
        return colour.withAlpha(alpha);
    }
}

const std::array<MiniLAB3StepSequencerAudioProcessorEditor::Theme, 6>& MiniLAB3StepSequencerAudioProcessorEditor::getThemes()
{
    static const std::array<Theme, 6> themes
    {{
        { "Alpha",      juce::Colour::fromRGB(  6,  8, 11), juce::Colour::fromRGB(18, 21, 28), juce::Colour::fromRGB(13, 16, 21), juce::Colour::fromRGBA(255,255,255,18), juce::Colour::fromRGB(148,163,184), juce::Colour::fromRGB(251,146, 60), { juce::Colour::fromRGB(232,121,249), juce::Colour::fromRGB(251,146, 60), juce::Colour::fromRGB(250,204, 21), juce::Colour::fromRGB( 74,222,128), juce::Colour::fromRGB( 45,212,191) } },
        { "Cyberpunk",  juce::Colour::fromRGB( 13,  2, 33), juce::Colour::fromRGB(25, 14, 47), juce::Colour::fromRGB(18, 10, 36), juce::Colour::fromRGBA(  0,255,255,26), juce::Colour::fromRGB(  0,255,255), juce::Colour::fromRGB(255,  0,255), { juce::Colour::fromRGB(255,  0,255), juce::Colour::fromRGB(  0,255,255), juce::Colour::fromRGB(255,255,  0), juce::Colour::fromRGB(124,252,  0), juce::Colour::fromRGB(255, 69,  0) } },
        { "Midnight",   juce::Colour::fromRGB(  2,  6, 23), juce::Colour::fromRGB(15, 23, 42), juce::Colour::fromRGB( 8, 12, 26), juce::Colour::fromRGBA( 99,102,241,36), juce::Colour::fromRGB(148,163,184), juce::Colour::fromRGB( 99,102,241), { juce::Colour::fromRGB( 99,102,241), juce::Colour::fromRGB(168, 85,247), juce::Colour::fromRGB(217, 70,239), juce::Colour::fromRGB(236, 72,153), juce::Colour::fromRGB(139, 92,246) } },
        { "Solar",      juce::Colour::fromRGB( 28, 10,  0), juce::Colour::fromRGB(45, 20,  0), juce::Colour::fromRGB(34, 15,  0), juce::Colour::fromRGBA(251,146, 60,26), juce::Colour::fromRGB(253,186,116), juce::Colour::fromRGB(249,115, 22), { juce::Colour::fromRGB(252,211, 77), juce::Colour::fromRGB(251,146, 60), juce::Colour::fromRGB(248,113,113), juce::Colour::fromRGB(239, 68, 68), juce::Colour::fromRGB(220, 38, 38) } },
        { "Forest",     juce::Colour::fromRGB(  2, 13,  8), juce::Colour::fromRGB( 6, 31, 19), juce::Colour::fromRGB( 4, 21, 13), juce::Colour::fromRGBA( 74,222,128,26), juce::Colour::fromRGB(134,239,172), juce::Colour::fromRGB( 34,197, 94), { juce::Colour::fromRGB( 74,222,128), juce::Colour::fromRGB( 34,197, 94), juce::Colour::fromRGB( 22,163, 74), juce::Colour::fromRGB( 21,128, 61), juce::Colour::fromRGB( 52,211,153) } },
        { "Mono",       juce::Colour::fromRGB( 15, 23, 42), juce::Colour::fromRGB(30, 41, 59), juce::Colour::fromRGB(17, 24, 39), juce::Colour::fromRGBA(255,255,255,26), juce::Colour::fromRGB(203,213,225), juce::Colour::fromRGB(255,255,255), { juce::Colour::fromRGB(248,250,252), juce::Colour::fromRGB(203,213,225), juce::Colour::fromRGB(148,163,184), juce::Colour::fromRGB(100,116,139), juce::Colour::fromRGB( 51, 65, 85) } }
    }};

    return themes;
}

const MiniLAB3StepSequencerAudioProcessorEditor::Theme& MiniLAB3StepSequencerAudioProcessorEditor::getTheme() const
{
    return getThemes()[(size_t) juce::jlimit(0, (int) getThemes().size() - 1, themeIndex)];
}

juce::String MiniLAB3StepSequencerAudioProcessorEditor::getMidiNoteName(int note)
{
    static const juce::StringArray notes { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int octave = (note / 12) - 2;
    return notes[note % 12] + juce::String(octave);
}

juce::String MiniLAB3StepSequencerAudioProcessorEditor::getFooterLaneName(FooterLane lane)
{
    switch (lane)
    {
        case FooterLane::velocity:    return "Velocity";
        case FooterLane::gate:        return "Gate";
        case FooterLane::probability: return "Probability";
        case FooterLane::shift:       return "Shift";
        case FooterLane::swing:       return "Swing";
    }

    return "Velocity";
}

MiniLAB3StepSequencerAudioProcessorEditor::MiniLAB3StepSequencerAudioProcessorEditor(MiniLAB3StepSequencerAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p)
{
    setSize(1460, 860);
    startTimerHz(60);
}

MiniLAB3StepSequencerAudioProcessorEditor::~MiniLAB3StepSequencerAudioProcessorEditor()
{
    stopTimer();
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getHeaderBounds() const
{
    return { 0, 0, getWidth(), 76 };
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getFooterBounds() const
{
    return { 0, getHeight() - 240, getWidth(), 240 };
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getSidebarBounds() const
{
    const auto contentTop = getHeaderBounds().getBottom();
    const auto footerTop = getFooterBounds().getY();
    return { getWidth() - 300, contentTop, 300, footerTop - contentTop };
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getMainBounds() const
{
    const auto contentTop = getHeaderBounds().getBottom();
    const auto footerTop = getFooterBounds().getY();
    return { 0, contentTop, getSidebarBounds().getX(), footerTop - contentTop };
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getGridBounds() const
{
    auto area = getMainBounds().reduced(12, 10);
    return area;
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getRowBounds(int track) const
{
    auto grid = getGridBounds();
    const int rowH = grid.getHeight() / MiniLAB3StepSequencerAudioProcessor::numTracks;
    return { grid.getX(), grid.getY() + (track * rowH), grid.getWidth(), rowH };
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getTrackLabelBounds(int track) const
{
    auto area = getRowBounds(track);
    return area.removeFromLeft(160).reduced(2, 4);
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getNoteBounds(int track) const
{
    auto area = getRowBounds(track);
    area.removeFromLeft(164);
    return area.removeFromLeft(52).reduced(4, 7);
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getMuteBounds(int track) const
{
    auto area = getRowBounds(track);
    area.removeFromLeft(220);
    return area.removeFromLeft(32).reduced(2, 7);
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getSoloBounds(int track) const
{
    auto area = getRowBounds(track);
    area.removeFromLeft(252);
    return area.removeFromLeft(32).reduced(2, 7);
}


juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getStepBounds(int track, int step) const
{
    auto area = getRowBounds(track);
    area.removeFromLeft(286);

    const int width = area.getWidth();
    const int cellW = width / MiniLAB3StepSequencerAudioProcessor::numSteps;
    const int x = area.getX() + (step * cellW);
    return { x + 1, area.getY() + 6, juce::jmax(8, cellW - 2), area.getHeight() - 12 };
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getTransportPlayBounds() const
{
    return { 320, 22, 34, 28 };
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getTransportStopBounds() const
{
    return { 360, 22, 34, 28 };
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getTempoBounds() const
{
    return { 420, 16, 96, 42 };
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getLoopButtonBounds(int section) const
{
    const int allWidth = 74;
    const int sectionWidth = 96;
    const int y = 18;
    const int h = 36;
    int x = 650;
    if (section < 0)
        return { x, y, allWidth, h };

    x += allWidth + 8 + section * (sectionWidth + 8);
    return { x, y, sectionWidth, h };
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getPatternButtonBounds(int index) const
{
    auto area = getSidebarBounds().reduced(18, 16);
    area.removeFromTop(34);
    const int cols = 5;
    const int gap = 8;
    const int cell = (area.getWidth() - (gap * (cols - 1))) / cols;
    const int row = index / cols;
    const int col = index % cols;
    return { area.getX() + col * (cell + gap), area.getY() + row * (cell + gap), cell, cell };
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getThemeButtonBounds(int index) const
{
    auto area = getSidebarBounds().reduced(18, 16);
    area.removeFromTop(150);
    const int h = 30;
    return { area.getX(), area.getY() + index * (h + 8), area.getWidth(), h };
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getExportButtonBounds() const
{
    auto area = getSidebarBounds().reduced(18, 16);
    return { area.getX(), area.getBottom() - 46, area.getWidth(), 34 };
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getFooterTabButtonBounds(int index) const
{
    auto footer = getFooterBounds().reduced(14, 14);
    footer.removeFromRight(14);
    const auto leftCol = footer.removeFromLeft(180);
    return { leftCol.getX(), leftCol.getY() + index * 38, leftCol.getWidth(), 30 };
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getFooterGraphBounds() const
{
    auto footer = getFooterBounds().reduced(14, 14);
    footer.removeFromLeft(210);
    return { footer.getX(), footer.getY(), footer.getWidth(), 150 };
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getFooterRepeatGridBounds() const
{
    auto footer = getFooterBounds().reduced(14, 14);
    footer.removeFromLeft(210);
    return { footer.getX(), footer.getBottom() - 56, footer.getWidth(), 56 };
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getFooterBarBounds(int step) const
{
    auto graph = getFooterGraphBounds();
    const int cellW = graph.getWidth() / MiniLAB3StepSequencerAudioProcessor::numSteps;
    return { graph.getX() + step * cellW + 1, graph.getY() + 4, juce::jmax(8, cellW - 2), graph.getHeight() - 8 };
}

juce::Rectangle<int> MiniLAB3StepSequencerAudioProcessorEditor::getFooterRepeatButtonBounds(int step, int repeatValue) const
{
    auto repeatGrid = getFooterRepeatGridBounds();
    const int cellW = repeatGrid.getWidth() / MiniLAB3StepSequencerAudioProcessor::numSteps;
    const int cellH = repeatGrid.getHeight() / 4;
    return {
        repeatGrid.getX() + step * cellW + 1,
        repeatGrid.getY() + (repeatValue - 1) * cellH + 1,
        juce::jmax(8, cellW - 2),
        juce::jmax(10, cellH - 2)
    };
}

int MiniLAB3StepSequencerAudioProcessorEditor::getDisplayStepForTrack(int track) const
{
    const int host16th = audioProcessor.global16thNote.load();
    const int loopSection = audioProcessor.getLoopSection();
    const int trackLen = juce::jmax(1, audioProcessor.getTrackLength(track));

    if (host16th >= 0)
    {
        if (loopSection >= 0)
            return (loopSection * MiniLAB3StepSequencerAudioProcessor::stepsPerPage) + (host16th % MiniLAB3StepSequencerAudioProcessor::stepsPerPage);
        return host16th % trackLen;
    }

    if (previewRunning)
    {
        if (loopSection >= 0)
            return (loopSection * MiniLAB3StepSequencerAudioProcessor::stepsPerPage) + (previewStep % MiniLAB3StepSequencerAudioProcessor::stepsPerPage);
        return previewStep % trackLen;
    }

    return -1;
}

int MiniLAB3StepSequencerAudioProcessorEditor::getCurrentTrackPageLength(int track) const
{
    return audioProcessor.getTrackLength(track);
}

float MiniLAB3StepSequencerAudioProcessorEditor::getFooterLaneValueAsNormalized(const StepData& step) const
{
    switch (footerLane)
    {
        case FooterLane::velocity:    return step.velocity;
        case FooterLane::gate:        return step.gate;
        case FooterLane::probability: return step.probability;
        case FooterLane::shift:       return juce::jlimit(0.0f, 1.0f, (step.shift + 1.0f) * 0.5f);
        case FooterLane::swing:       return step.swing;
    }

    return step.velocity;
}

float MiniLAB3StepSequencerAudioProcessorEditor::getFooterLaneValueFromPoint(const juce::Point<float>& position) const
{
    auto graph = getFooterGraphBounds().toFloat();
    const float norm = juce::jlimit(0.0f, 1.0f, 1.0f - ((position.y - graph.getY()) / juce::jmax(1.0f, graph.getHeight())));
    return norm;
}

void MiniLAB3StepSequencerAudioProcessorEditor::applyFooterLaneValueAtPoint(const juce::Point<float>& position)
{
    auto graph = getFooterGraphBounds();
    if (!graph.toFloat().contains(position))
        return;

    const int cellW = graph.getWidth() / MiniLAB3StepSequencerAudioProcessor::numSteps;
    const int stepIndex = juce::jlimit(0, MiniLAB3StepSequencerAudioProcessor::numSteps - 1,
                                       (int) ((position.x - graph.getX()) / juce::jmax(1, cellW)));

    auto step = audioProcessor.getStepData(selectedTrack, stepIndex);
    const float norm = getFooterLaneValueFromPoint(position);

    switch (footerLane)
    {
        case FooterLane::velocity:    step.velocity = norm; break;
        case FooterLane::gate:        step.gate = juce::jlimit(0.05f, 1.0f, norm); break;
        case FooterLane::probability: step.probability = norm; break;
        case FooterLane::shift:       step.shift = juce::jlimit(-1.0f, 1.0f, (norm * 2.0f) - 1.0f); break;
        case FooterLane::swing:       step.swing = norm; break;
    }

    if (step.isActive == false && footerLane == FooterLane::velocity)
        step.isActive = true;

    audioProcessor.setStepData(selectedTrack, stepIndex, step);
    footerDragStep = stepIndex;
}


void MiniLAB3StepSequencerAudioProcessorEditor::drawPillButton(juce::Graphics& g,
                                                               juce::Rectangle<int> bounds,
                                                               const juce::String& text,
                                                               bool active,
                                                               juce::Colour activeColour,
                                                               juce::Colour inactiveColour,
                                                               juce::Colour textColour) const
{
    g.setColour(active ? activeColour : inactiveColour);
    g.fillRoundedRectangle(bounds.toFloat(), 8.0f);
    g.setColour(active ? activeColour.brighter(0.2f) : getTheme().border);
    g.drawRoundedRectangle(bounds.toFloat(), 8.0f, 1.0f);
    g.setColour(textColour);
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    g.drawFittedText(text, bounds, juce::Justification::centred, 1);
}

void MiniLAB3StepSequencerAudioProcessorEditor::exportCurrentPatternToMidi(int trackIndex)
{
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(480);

    juce::MidiMessageSequence tempoTrack;
    const int microsecondsPerQuarterNote = (int) std::round(60000000.0 / juce::jmax(30.0, audioProcessor.lastKnownBpm.load()));
    tempoTrack.addEvent(juce::MidiMessage::tempoMetaEvent(microsecondsPerQuarterNote));
    tempoTrack.addEvent(juce::MidiMessage::endOfTrack());
    midiFile.addTrack(tempoTrack);

    auto buildTrack = [&](int track)
    {
        juce::MidiMessageSequence sequence;
        const int note = (int) audioProcessor.noteParams[track]->load();
        const float trackNudgeMs = audioProcessor.nudgeParams[track]->load();
        const double bpm = juce::jmax(30.0, audioProcessor.lastKnownBpm.load());
        const double ppqPerMs = bpm / 60000.0;
        const int trackLength = juce::jmax(1, audioProcessor.getTrackLength(track));

        for (int step = 0; step < trackLength; ++step)
        {
            const auto stepData = audioProcessor.getStepData(track, step);
            if (!stepData.isActive)
                continue;

            const int repeats = juce::jlimit(1, 4, stepData.repeats);
            const double stepPpq = 0.25;
            const double repeatPpq = stepPpq / (double) repeats;
            const double globalSwingPpq = audioProcessor.swingParam->load() * (stepPpq * 0.5);
            const double perStepSwing = (step % 2 != 0) ? (stepData.swing * (stepPpq * 0.5)) : 0.0;
            const double stepShiftPpq = stepData.shift * (stepPpq * 0.5);
            const double trackNudgePpq = (trackNudgeMs * ppqPerMs);

            for (int repeat = 0; repeat < repeats; ++repeat)
            {
                double ppq = (step * stepPpq) + (repeat * repeatPpq) + stepShiftPpq + trackNudgePpq;
                if ((step % 2) != 0)
                    ppq += globalSwingPpq + perStepSwing;

                const double offPpq = ppq + (repeatPpq * juce::jlimit(0.05f, 1.0f, stepData.gate));
                const juce::uint8 velocity = (juce::uint8) juce::jlimit(1, 127, (int) std::round(stepData.velocity * 127.0f));
                sequence.addEvent(juce::MidiMessage::noteOn(1, note, velocity), ppq);
                sequence.addEvent(juce::MidiMessage::noteOff(1, note), offPpq);
            }
        }

        sequence.addEvent(juce::MidiMessage::endOfTrack(), juce::jmax(0.0, trackLength * 0.25));
        midiFile.addTrack(sequence);
    };

    if (trackIndex >= 0)
        buildTrack(trackIndex);
    else
        for (int track = 0; track < MiniLAB3StepSequencerAudioProcessor::numTracks; ++track)
            buildTrack(track);

    auto defaultFolder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    midiExportChooser = std::make_unique<juce::FileChooser>("Export MIDI", defaultFolder, "*.mid");

    const auto flags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;

    auto safeThis = juce::Component::SafePointer<MiniLAB3StepSequencerAudioProcessorEditor>(this);
    midiExportChooser->launchAsync(flags, [safeThis, midiFile](const juce::FileChooser& chooser) mutable
    {
        if (safeThis == nullptr)
            return;

        auto file = chooser.getResult();
        if (file == juce::File())
        {
            safeThis->midiExportChooser.reset();
            return;
        }

        if (!file.hasFileExtension("mid"))
            file = file.withFileExtension(".mid");

        if (auto stream = file.createOutputStream())
            midiFile.writeTo(*stream, 1);

        safeThis->midiExportChooser.reset();
    });
}

void MiniLAB3StepSequencerAudioProcessorEditor::drawKnob(juce::Graphics& g,
                                                         juce::Rectangle<float> bounds,
                                                         float normalized,
                                                         juce::Colour colour) const
{
    normalized = juce::jlimit(0.0f, 1.0f, normalized);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
    const float angle = juce::jmap(normalized, 0.0f, 1.0f,
                                   -juce::MathConstants<float>::pi * 0.75f,
                                    juce::MathConstants<float>::pi * 0.75f);

    g.setColour(juce::Colour::fromRGB(28, 32, 38));
    g.fillEllipse(bounds);
    g.setColour(withAlpha(colour, 0.95f));
    g.drawEllipse(bounds, 1.0f);
    g.drawLine(centre.x, centre.y,
               centre.x + std::cos(angle) * radius,
               centre.y + std::sin(angle) * radius,
               2.4f);
}

void MiniLAB3StepSequencerAudioProcessorEditor::paint(juce::Graphics& g)
{
    const auto& theme = getTheme();
    g.fillAll(theme.bg);

    drawHeader(g);
    drawGrid(g);
    drawSidebar(g);
    drawFooter(g);
}

void MiniLAB3StepSequencerAudioProcessorEditor::drawHeader(juce::Graphics& g)
{
    const auto& theme = getTheme();
    auto header = getHeaderBounds();

    g.setColour(theme.panel);
    g.fillRect(header);
    g.setColour(theme.border);
    g.drawLine((float) header.getX(), (float) header.getBottom() - 0.5f, (float) header.getRight(), (float) header.getBottom() - 0.5f);

    g.setFont(juce::Font(28.0f, juce::Font::bold));
    g.setColour(theme.accent);
    g.drawText("ALPHA", 18, 14, 110, 30, juce::Justification::centredLeft);

    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.setColour(theme.text.brighter(0.1f));
    g.drawText("SEQUENCER", 126, 18, 120, 24, juce::Justification::centredLeft);
    g.setFont(juce::Font(10.0f, juce::Font::plain));
    g.drawText("MiniLAB 3 UI port", 126, 38, 150, 18, juce::Justification::centredLeft);

    const bool hostRunning = audioProcessor.global16thNote.load() >= 0;
    drawPillButton(g, getTransportPlayBounds(), ">", previewRunning || hostRunning,
                   theme.accent, juce::Colour::fromRGB(20, 23, 30), juce::Colours::black);
    drawPillButton(g, getTransportStopBounds(), "[]", !previewRunning && !hostRunning,
                   juce::Colour::fromRGB(148, 163, 184), juce::Colour::fromRGB(20, 23, 30), theme.text);

    const auto tempo = getTempoBounds();
    g.setColour(juce::Colour::fromRGB(12, 14, 18));
    g.fillRoundedRectangle(tempo.toFloat(), 10.0f);
    g.setColour(theme.border);
    g.drawRoundedRectangle(tempo.toFloat(), 10.0f, 1.0f);
    g.setColour(theme.text.withAlpha(0.6f));
    g.setFont(juce::Font(9.0f, juce::Font::bold));
    g.drawText("TEMPO", tempo.getX() + 10, tempo.getY() + 4, tempo.getWidth() - 20, 12, juce::Justification::centredLeft);
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(18.0f, juce::Font::bold));
    g.drawText(juce::String(audioProcessor.lastKnownBpm.load(), 1), tempo.getX() + 10, tempo.getY() + 14, tempo.getWidth() - 20, 20, juce::Justification::centredLeft);

    for (int section = -1; section < MiniLAB3StepSequencerAudioProcessor::numPages; ++section)
    {
        const bool active = (audioProcessor.getLoopSection() == section);
        const auto bounds = getLoopButtonBounds(section);
        const juce::String label = section < 0
            ? "All"
            : juce::String(section * MiniLAB3StepSequencerAudioProcessor::stepsPerPage + 1)
              + "-" + juce::String(section * MiniLAB3StepSequencerAudioProcessor::stepsPerPage + MiniLAB3StepSequencerAudioProcessor::stepsPerPage);

        drawPillButton(g, bounds, label, active,
                       theme.accent, juce::Colour::fromRGB(20, 23, 30), active ? juce::Colours::black : theme.text);
    }

    const int page = audioProcessor.currentPage.load();
    g.setColour(theme.text.withAlpha(0.5f));
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.drawText("HW PAGE", getWidth() - 180, 16, 70, 14, juce::Justification::centredLeft);
    g.setColour(theme.accent);
    g.setFont(juce::Font(24.0f, juce::Font::bold));
    g.drawText(juce::String(page + 1), getWidth() - 110, 10, 60, 34, juce::Justification::centredRight);
    g.setColour(theme.text.withAlpha(0.8f));
    g.setFont(juce::Font(12.0f));
    g.drawText(audioProcessor.getPatternName(audioProcessor.getCurrentPattern()), getWidth() - 220, 40, 180, 18, juce::Justification::centredRight);
}

void MiniLAB3StepSequencerAudioProcessorEditor::drawGrid(juce::Graphics& g)
{
    const auto& theme = getTheme();
    auto grid = getGridBounds();

    g.setColour(withAlpha(theme.panel, 0.65f));
    g.fillRoundedRectangle(grid.toFloat(), 12.0f);
    g.setColour(theme.border);
    g.drawRoundedRectangle(grid.toFloat(), 12.0f, 1.0f);

    if (pageFlashAlpha > 0.0f)
    {
        const int page = audioProcessor.currentPage.load();
        const auto flashBounds = getStepBounds(0, page * MiniLAB3StepSequencerAudioProcessor::stepsPerPage)
                                    .withY(grid.getY())
                                    .withHeight(grid.getHeight())
                                    .withWidth((getStepBounds(0, page * MiniLAB3StepSequencerAudioProcessor::stepsPerPage + 7).getRight()
                                                - getStepBounds(0, page * MiniLAB3StepSequencerAudioProcessor::stepsPerPage).getX()));
        g.setColour(juce::Colours::white.withAlpha(pageFlashAlpha * 0.10f));
        g.fillRoundedRectangle(flashBounds.toFloat().expanded(4.0f, -6.0f), 8.0f);
    }

    for (int track = 0; track < MiniLAB3StepSequencerAudioProcessor::numTracks; ++track)
    {
        const auto row = getRowBounds(track);
        const bool selected = (track == selectedTrack);
        const auto accent = theme.laneColours[(size_t) (track % (int) theme.laneColours.size())];
        const auto stepPlaying = getDisplayStepForTrack(track);
        const int trackLen = getCurrentTrackPageLength(track);
        const bool muted = audioProcessor.muteParams[track]->load() > 0.5f;
        const bool soloed = audioProcessor.soloParams[track]->load() > 0.5f;
        const int note = (int) audioProcessor.noteParams[track]->load();

        g.setColour(selected ? withAlpha(juce::Colours::white, 0.06f) : juce::Colours::transparentBlack);
        g.fillRoundedRectangle(row.reduced(4, 2).toFloat(), 8.0f);

        auto labelBounds = getTrackLabelBounds(track);
        g.setColour(accent.withAlpha((stepPlaying >= 0 && audioProcessor.getStepData(track, stepPlaying).isActive) ? 1.0f : 0.35f));
        g.fillEllipse((float) labelBounds.getX() + 6.0f, (float) labelBounds.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setColour(theme.text.withAlpha(0.35f));
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText(juce::String(track + 1).paddedLeft('0', 2), labelBounds.getX() + 20, labelBounds.getY(), 26, labelBounds.getHeight(), juce::Justification::centredLeft);
        g.setColour(selected ? juce::Colours::white : theme.text);
        g.setFont(juce::Font(12.0f, juce::Font::bold));
        g.drawText(audioProcessor.instrumentNames[track], labelBounds.getX() + 48, labelBounds.getY(), 80, labelBounds.getHeight(), juce::Justification::centredLeft);
        g.setColour(theme.text.withAlpha(0.55f));
        g.setFont(juce::Font(10.0f));
        g.drawText("LEN " + juce::String(trackLen), labelBounds.getRight() - 48, labelBounds.getY(), 42, labelBounds.getHeight(), juce::Justification::centredRight);

        const auto noteBounds = getNoteBounds(track);
        drawPillButton(g, noteBounds, getMidiNoteName(note), false, theme.accent,
                       juce::Colour::fromRGB(44, 48, 54), juce::Colours::white);

        drawPillButton(g, getMuteBounds(track), "M", muted,
                       juce::Colour::fromRGB(239, 68, 68), juce::Colour::fromRGB(36, 40, 46), muted ? juce::Colours::black : theme.text);
        drawPillButton(g, getSoloBounds(track), "S", soloed,
                       juce::Colour::fromRGB(251, 191, 36), juce::Colour::fromRGB(36, 40, 46), soloed ? juce::Colours::black : theme.text);


        for (int step = 0; step < MiniLAB3StepSequencerAudioProcessor::numSteps; ++step)
        {
            const auto bounds = getStepBounds(track, step);
            const auto stepData = audioProcessor.getStepData(track, step);
            const int section = step / MiniLAB3StepSequencerAudioProcessor::stepsPerPage;
            const bool loopDimmed = (audioProcessor.getLoopSection() >= 0 && audioProcessor.getLoopSection() != section);
            const bool playing = (step == stepPlaying);
            const bool insideTrack = step < trackLen;

            juce::Colour bg = juce::Colour::fromRGB((section % 2 == 0) ? 18 : 26,
                                                    (section % 2 == 0) ? 22 : 30,
                                                    (section % 2 == 0) ? 30 : 38);
            if (!insideTrack)
                bg = bg.darker(0.8f);

            g.setColour(loopDimmed ? bg.withAlpha(0.25f) : bg.withAlpha(0.9f));
            g.fillRoundedRectangle(bounds.toFloat(), 4.0f);

            g.setColour(stepData.isActive ? accent.withAlpha(loopDimmed ? 0.35f : (0.25f + stepData.velocity * 0.75f))
                                          : juce::Colours::transparentBlack);
            g.fillRoundedRectangle(bounds.toFloat().reduced(2.0f), 3.0f);

            g.setColour(playing ? withAlpha(theme.accent, 0.9f) : withAlpha(theme.border, 0.8f));
            g.drawRoundedRectangle(bounds.toFloat(), 4.0f, playing ? 1.6f : 1.0f);
        }
    }
}

void MiniLAB3StepSequencerAudioProcessorEditor::drawSidebar(juce::Graphics& g)
{
    const auto& theme = getTheme();
    auto area = getSidebarBounds();

    g.setColour(theme.sidebar);
    g.fillRect(area);
    g.setColour(theme.border);
    g.drawLine((float) area.getX() + 0.5f, (float) area.getY(), (float) area.getX() + 0.5f, (float) area.getBottom());

    auto inner = area.reduced(18, 16);
    g.setColour(theme.accent);
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText("PATTERN MATRIX", inner.removeFromTop(22), juce::Justification::centredLeft);

    const int currentPattern = audioProcessor.getCurrentPattern();
    for (int i = 0; i < MiniLAB3StepSequencerAudioProcessor::numPatterns; ++i)
    {
        const auto bounds = getPatternButtonBounds(i);
        const bool active = (i == currentPattern);
        drawPillButton(g, bounds, juce::String::charToString((juce::juce_wchar) ('A' + i)), active,
                       theme.accent, juce::Colour::fromRGB(20, 24, 30), active ? juce::Colours::black : theme.text);
    }

    g.setColour(theme.text.withAlpha(0.55f));
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText("THEME", area.getX() + 18, area.getY() + 160, area.getWidth() - 36, 16, juce::Justification::centredLeft);

    for (int i = 0; i < (int) getThemes().size(); ++i)
    {
        const auto bounds = getThemeButtonBounds(i);
        const bool active = (i == themeIndex);
        drawPillButton(g, bounds, getThemes()[(size_t) i].name, active,
                       getThemes()[(size_t) i].accent, juce::Colour::fromRGB(20, 24, 30), active ? juce::Colours::black : theme.text);
    }

    drawPillButton(g, getExportButtonBounds(), "FULL MIDI EXPORT", true,
                   theme.accent, theme.accent, juce::Colours::black);

    g.setColour(theme.text.withAlpha(0.28f));
    g.setFont(juce::Font(10.0f, juce::Font::bold));
    g.drawText("Hardware page, pads, encoder track select, mute/solo and note select are preserved.",
               area.getX() + 18, area.getBottom() - 54, area.getWidth() - 36, 42,
               juce::Justification::topLeft, true);
}

void MiniLAB3StepSequencerAudioProcessorEditor::drawFooter(juce::Graphics& g)
{
    const auto& theme = getTheme();
    auto footer = getFooterBounds();

    g.setColour(theme.panel);
    g.fillRect(footer);
    g.setColour(theme.border);
    g.drawLine((float) footer.getX(), (float) footer.getY() + 0.5f, (float) footer.getRight(), (float) footer.getY() + 0.5f);

    for (int i = 0; i < 5; ++i)
    {
        const auto lane = static_cast<FooterLane>(i);
        const bool active = (lane == footerLane);
        drawPillButton(g, getFooterTabButtonBounds(i), getFooterLaneName(lane), active,
                       theme.accent, juce::Colour::fromRGB(18, 22, 28), active ? juce::Colours::black : theme.text);
    }

    g.setColour(theme.accent);
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText("CH. " + juce::String(selectedTrack + 1) + " AUTOMATION",
               18, footer.getBottom() - 28, 180, 14, juce::Justification::centred);

    const auto graph = getFooterGraphBounds();
    g.setColour(juce::Colour::fromRGB(14, 16, 20));
    g.fillRoundedRectangle(graph.toFloat(), 12.0f);
    g.setColour(theme.border);
    g.drawRoundedRectangle(graph.toFloat(), 12.0f, 1.0f);

    if (footerLane == FooterLane::shift)
    {
        g.setColour(theme.text.withAlpha(0.15f));
        g.drawLine((float) graph.getX(), graph.getCentreY(), (float) graph.getRight(), graph.getCentreY(), 1.0f);
    }

    const auto displayStep = getDisplayStepForTrack(selectedTrack);
    const auto laneColour = theme.laneColours[(size_t) (selectedTrack % (int) theme.laneColours.size())];

    for (int step = 0; step < MiniLAB3StepSequencerAudioProcessor::numSteps; ++step)
    {
        const auto barBounds = getFooterBarBounds(step);
        const auto stepData = audioProcessor.getStepData(selectedTrack, step);
        const float norm = getFooterLaneValueAsNormalized(stepData);
        const int filledHeight = juce::jmax(2, (int) std::round(barBounds.getHeight() * norm));
        const bool playing = (step == displayStep);
        const bool activeStep = stepData.isActive;
        const int section = step / MiniLAB3StepSequencerAudioProcessor::stepsPerPage;
        const bool loopDimmed = (audioProcessor.getLoopSection() >= 0 && audioProcessor.getLoopSection() != section);

        g.setColour(withAlpha(juce::Colours::white, 0.04f));
        g.fillRoundedRectangle(barBounds.toFloat(), 3.0f);

        auto fill = juce::Rectangle<int>(barBounds.getX(), barBounds.getBottom() - filledHeight, barBounds.getWidth(), filledHeight);
        g.setColour(activeStep ? laneColour.withAlpha(loopDimmed ? 0.25f : 0.95f)
                               : withAlpha(theme.text, 0.12f));
        g.fillRoundedRectangle(fill.toFloat(), 3.0f);

        if (playing)
        {
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            g.drawRoundedRectangle(barBounds.toFloat(), 3.0f, 1.4f);
        }
    }

    const auto repeatGrid = getFooterRepeatGridBounds();
    g.setColour(juce::Colour::fromRGB(14, 16, 20));
    g.fillRoundedRectangle(repeatGrid.toFloat(), 10.0f);
    g.setColour(theme.border);
    g.drawRoundedRectangle(repeatGrid.toFloat(), 10.0f, 1.0f);

    for (int step = 0; step < MiniLAB3StepSequencerAudioProcessor::numSteps; ++step)
    {
        const auto stepData = audioProcessor.getStepData(selectedTrack, step);
        const int section = step / MiniLAB3StepSequencerAudioProcessor::stepsPerPage;
        const bool loopDimmed = (audioProcessor.getLoopSection() >= 0 && audioProcessor.getLoopSection() != section);

        for (int repeat = 1; repeat <= 4; ++repeat)
        {
            const auto bounds = getFooterRepeatButtonBounds(step, repeat);
            const bool active = (stepData.repeats == repeat);
            drawPillButton(g, bounds, juce::String(repeat), active,
                           theme.accent,
                           juce::Colour::fromRGB(20, 24, 30).withAlpha(loopDimmed ? 0.25f : 1.0f),
                           active ? juce::Colours::black : theme.text.withAlpha(loopDimmed ? 0.2f : 0.5f));
        }
    }
}

void MiniLAB3StepSequencerAudioProcessorEditor::resized() {}

void MiniLAB3StepSequencerAudioProcessorEditor::timerCallback()
{
    if (!audioProcessor.isHardwareConnected())
    {
        static int connectionRetry = 0;
        if (++connectionRetry >= 30)
        {
            audioProcessor.openHardwareOutput();
            connectionRetry = 0;
        }
    }

    if (audioProcessor.pageChangedTrigger.exchange(false))
        pageFlashAlpha = 1.0f;

    if (pageFlashAlpha > 0.0f)
        pageFlashAlpha *= 0.86f;

    if (audioProcessor.global16thNote.load() < 0 && previewRunning)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const double bpm = juce::jmax(30.0, audioProcessor.lastKnownBpm.load());
        const double msPer16th = 60000.0 / bpm / 4.0;
        const int trackLen = juce::jmax(1, audioProcessor.getTrackLength(selectedTrack));
        const int loopSection = audioProcessor.getLoopSection();
        const int loopLen = (loopSection >= 0) ? MiniLAB3StepSequencerAudioProcessor::stepsPerPage : trackLen;

        if (lastPreviewAdvanceMs == 0.0)
            lastPreviewAdvanceMs = now;

        while ((now - lastPreviewAdvanceMs) >= msPer16th)
        {
            previewStep = (previewStep + 1) % juce::jmax(1, loopLen);
            lastPreviewAdvanceMs += msPer16th;
        }
    }
    else
    {
        lastPreviewAdvanceMs = juce::Time::getMillisecondCounterHiRes();
    }

    repaint();
}

void MiniLAB3StepSequencerAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    const auto position = e.position;

    if (getTransportPlayBounds().contains((int) position.x, (int) position.y))
    {
        previewRunning = !previewRunning;
        if (previewRunning)
            previewStep = 0;
        return;
    }

    if (getTransportStopBounds().contains((int) position.x, (int) position.y))
    {
        previewRunning = false;
        previewStep = 0;
        return;
    }

    for (int section = -1; section < MiniLAB3StepSequencerAudioProcessor::numPages; ++section)
    {
        if (getLoopButtonBounds(section).contains((int) position.x, (int) position.y))
        {
            audioProcessor.setLoopSection(section);
            previewStep = 0;
            return;
        }
    }

    for (int i = 0; i < MiniLAB3StepSequencerAudioProcessor::numPatterns; ++i)
    {
        if (getPatternButtonBounds(i).contains((int) position.x, (int) position.y))
        {
            audioProcessor.setCurrentPattern(i);
            return;
        }
    }

    for (int i = 0; i < (int) getThemes().size(); ++i)
    {
        if (getThemeButtonBounds(i).contains((int) position.x, (int) position.y))
        {
            themeIndex = i;
            return;
        }
    }

    if (getExportButtonBounds().contains((int) position.x, (int) position.y))
    {
        exportCurrentPatternToMidi(-1);
        return;
    }

    for (int i = 0; i < 5; ++i)
    {
        if (getFooterTabButtonBounds(i).contains((int) position.x, (int) position.y))
        {
            footerLane = static_cast<FooterLane>(i);
            return;
        }
    }

    if (getFooterGraphBounds().contains((int) position.x, (int) position.y))
    {
        footerDrawing = true;
        applyFooterLaneValueAtPoint(position);
        return;
    }

    for (int step = 0; step < MiniLAB3StepSequencerAudioProcessor::numSteps; ++step)
    {
        for (int repeat = 1; repeat <= 4; ++repeat)
        {
            if (getFooterRepeatButtonBounds(step, repeat).contains((int) position.x, (int) position.y))
            {
                auto stepData = audioProcessor.getStepData(selectedTrack, step);
                stepData.repeats = repeat;
                audioProcessor.setStepData(selectedTrack, step, stepData);
                return;
            }
        }
    }

    auto notifyHostParameterChange = [&](const juce::String& paramID, float normalizedValue)
    {
        if (auto* p = audioProcessor.apvts.getParameter(paramID))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(normalizedValue);
            p->endChangeGesture();
        }
    };

    for (int track = 0; track < MiniLAB3StepSequencerAudioProcessor::numTracks; ++track)
    {
        if (getRowBounds(track).contains((int) position.x, (int) position.y))
            selectedTrack = track;


        if (getNoteBounds(track).contains((int) position.x, (int) position.y))
        {
            const int currentNote = (int) audioProcessor.noteParams[track]->load();
            int newNote = e.mods.isRightButtonDown() ? currentNote - 1 : currentNote + 1;
            if (newNote > 127) newNote = 0;
            if (newNote < 0) newNote = 127;
            notifyHostParameterChange("note_" + juce::String(track + 1), newNote / 127.0f);
            return;
        }

        if (getMuteBounds(track).contains((int) position.x, (int) position.y))
        {
            const bool currentValue = audioProcessor.muteParams[track]->load() > 0.5f;
            notifyHostParameterChange("mute_" + juce::String(track + 1), currentValue ? 0.0f : 1.0f);
            return;
        }

        if (getSoloBounds(track).contains((int) position.x, (int) position.y))
        {
            const bool currentValue = audioProcessor.soloParams[track]->load() > 0.5f;
            notifyHostParameterChange("solo_" + juce::String(track + 1), currentValue ? 0.0f : 1.0f);
            return;
        }

        for (int step = 0; step < MiniLAB3StepSequencerAudioProcessor::numSteps; ++step)
        {
            if (getStepBounds(track, step).contains((int) position.x, (int) position.y))
            {
                audioProcessor.toggleStepActive(track, step, 0.8f);
                return;
            }
        }
    }
}

void MiniLAB3StepSequencerAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (footerDrawing)
    {
        applyFooterLaneValueAtPoint(e.position);
        return;
    }
}

void MiniLAB3StepSequencerAudioProcessorEditor::mouseUp(const juce::MouseEvent&)
{
    footerDrawing = false;
    footerDragStep = -1;
}
