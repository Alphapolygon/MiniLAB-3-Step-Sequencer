#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::Image loadNudgeKnobStripImage()
    {
        return juce::ImageCache::getFromMemory(BinaryData::nudge_knob_strip_png, BinaryData::nudge_knob_strip_pngSize);

    }
}

static juce::String getMidiNoteName(int note) {
    juce::StringArray notes = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int octave = (note / 12) - 2;
    return notes[note % 12] + juce::String(octave);
}

MiniLAB3StepSequencerAudioProcessorEditor::MiniLAB3StepSequencerAudioProcessorEditor(MiniLAB3StepSequencerAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::MainBackground_png, BinaryData::MainBackground_pngSize);
    inactiveBgA = juce::ImageCache::getFromMemory(BinaryData::bg_dark_png, BinaryData::bg_dark_pngSize);
    inactiveBgB = juce::ImageCache::getFromMemory(BinaryData::bg_light_png, BinaryData::bg_light_pngSize);

    activeKeys[0] = juce::ImageCache::getFromMemory(BinaryData::active_track_1_png, BinaryData::active_track_1_pngSize);
    activeKeys[1] = juce::ImageCache::getFromMemory(BinaryData::active_track_2_png, BinaryData::active_track_2_pngSize);
    activeKeys[2] = juce::ImageCache::getFromMemory(BinaryData::active_track_3_png, BinaryData::active_track_3_pngSize);
    activeKeys[3] = juce::ImageCache::getFromMemory(BinaryData::active_track_4_png, BinaryData::active_track_4_pngSize);
    activeKeys[4] = juce::ImageCache::getFromMemory(BinaryData::active_track_5_png, BinaryData::active_track_5_pngSize);
    activeKeys[5] = juce::ImageCache::getFromMemory(BinaryData::active_track_6_png, BinaryData::active_track_6_pngSize);
    activeKeys[6] = juce::ImageCache::getFromMemory(BinaryData::active_track_7_png, BinaryData::active_track_7_pngSize);
    activeKeys[7] = juce::ImageCache::getFromMemory(BinaryData::active_track_8_png, BinaryData::active_track_8_pngSize);
    activeKeys[8] = juce::ImageCache::getFromMemory(BinaryData::active_track_9_png, BinaryData::active_track_9_pngSize);
    activeKeys[9] = juce::ImageCache::getFromMemory(BinaryData::active_track_10_png, BinaryData::active_track_10_pngSize);
    activeKeys[10] = juce::ImageCache::getFromMemory(BinaryData::active_track_11_png, BinaryData::active_track_11_pngSize);
    activeKeys[11] = juce::ImageCache::getFromMemory(BinaryData::active_track_12_png, BinaryData::active_track_12_pngSize);
    activeKeys[12] = juce::ImageCache::getFromMemory(BinaryData::active_track_13_png, BinaryData::active_track_13_pngSize);
    activeKeys[13] = juce::ImageCache::getFromMemory(BinaryData::active_track_14_png, BinaryData::active_track_14_pngSize);
    activeKeys[14] = juce::ImageCache::getFromMemory(BinaryData::active_track_15_png, BinaryData::active_track_15_pngSize);
    activeKeys[15] = juce::ImageCache::getFromMemory(BinaryData::active_track_16_png, BinaryData::active_track_16_pngSize);

    nudgeKnobStrip = loadNudgeKnobStripImage();

    setSize(1050, 600);
    startTimerHz(60);
}

MiniLAB3StepSequencerAudioProcessorEditor::~MiniLAB3StepSequencerAudioProcessorEditor() { stopTimer(); }

juce::Rectangle<float> MiniLAB3StepSequencerAudioProcessorEditor::getNudgeKnobBounds(int trackIndex) const
{
    const int topY = 124;
    const int cellH = (getHeight() - topY - 20) / 16;
    const float y = static_cast<float>(topY + (trackIndex * cellH));
    const float size = 30.0f;
    return { 131.0f, y + (cellH - size) * 0.5f, size, size };
}

void MiniLAB3StepSequencerAudioProcessorEditor::setTrackNudgeFromMs(int trackIndex, float newValueMs)
{
    if (trackIndex < 0 || trackIndex >= 16)
        return;

    newValueMs = juce::jlimit(MiniLAB3StepSequencerAudioProcessor::minMicroTimingMs,
                              MiniLAB3StepSequencerAudioProcessor::maxMicroTimingMs,
                              newValueMs);

    const juce::String paramID = "nudge_" + juce::String(trackIndex + 1);
    if (auto* p = audioProcessor.apvts.getParameter(paramID)) {
        const float normalized = (newValueMs - MiniLAB3StepSequencerAudioProcessor::minMicroTimingMs)
            / (MiniLAB3StepSequencerAudioProcessor::maxMicroTimingMs - MiniLAB3StepSequencerAudioProcessor::minMicroTimingMs);
        p->setValueNotifyingHost(normalized);
    }
}

void MiniLAB3StepSequencerAudioProcessorEditor::timerCallback() {
    if (!audioProcessor.isHardwareConnected()) {
        static int connectionRetry = 0;
        if (++connectionRetry >= 30) {
            audioProcessor.openHardwareOutput();
            connectionRetry = 0;
        }
    }
    if (audioProcessor.pageChangedTrigger.exchange(false)) pageFlashAlpha = 1.0f;
    if (pageFlashAlpha > 0.0f) pageFlashAlpha *= 0.85f;
    repaint();
}

void MiniLAB3StepSequencerAudioProcessorEditor::paint(juce::Graphics& g)
{
    if (backgroundImage.isValid()) g.drawImageAt(backgroundImage, 0, 0);
    else g.fillAll(juce::Colour::fromRGB(15, 15, 18));

    const int currentPage = audioProcessor.currentPage.load();
    const int currentInstrument = audioProcessor.currentInstrument.load();
    const int global16thNote = audioProcessor.global16thNote.load();

    const int startX = 170;
    const int topY = 124;
    const int gridAreaW = getWidth() - startX - 25;
    const int cellW = gridAreaW / 32;
    const int cellH = (getHeight() - topY - 20) / 16;

    if (pageFlashAlpha > 0.0f) {
        g.setColour(juce::Colours::white.withAlpha(pageFlashAlpha * 0.12f));
        g.fillRect(startX + (currentPage * 8 * cellW), topY, cellW * 8, cellH * 16);
    }

    const juce::ScopedLock sl(audioProcessor.stateLock);

    for (int t = 0; t < 16; ++t) {
        int y = topY + (t * cellH);
        const int len = juce::jmax(1, audioProcessor.trackLengths[t]);
        const bool isCurrent = (t == currentInstrument);

        juce::String name = audioProcessor.instrumentNames[t];
        g.setColour(isCurrent ? juce::Colours::cyan : juce::Colours::white.withAlpha(0.6f));
        g.setFont(juce::FontOptions(11.0f, isCurrent ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText(name, 5, y, 55, cellH, juce::Justification::centredLeft, 1);

        int trackNote = static_cast<int>(audioProcessor.noteParams[t]->load());
        bool isMuted = audioProcessor.muteParams[t]->load() > 0.5f;
        bool isSoloed = audioProcessor.soloParams[t]->load() > 0.5f;
        float nudgeMs = audioProcessor.nudgeParams[t]->load();

        juce::String noteStr = getMidiNoteName(trackNote);
        g.setColour(juce::Colours::grey);
        g.fillRoundedRectangle(60, y + 4, 25, cellH - 8, 3.0f);
        g.setColour(juce::Colours::white);
        g.drawFittedText(noteStr, 60, y, 25, cellH, juce::Justification::centred, 1);

        g.setColour(isMuted ? juce::Colours::orange : juce::Colour::fromRGB(35, 35, 40));
        g.fillRoundedRectangle(88, y + 4, 18, cellH - 8, 3.0f);
        g.setColour(isMuted ? juce::Colours::black : juce::Colours::grey);
        g.drawFittedText("M", 88, y, 18, cellH, juce::Justification::centred, 1);

        g.setColour(isSoloed ? juce::Colours::yellow : juce::Colour::fromRGB(35, 35, 40));
        g.fillRoundedRectangle(109, y + 4, 18, cellH - 8, 3.0f);
        g.setColour(isSoloed ? juce::Colours::black : juce::Colours::grey);
        g.drawFittedText("S", 109, y, 18, cellH, juce::Justification::centred, 1);

        const float knobNudgeMs = audioProcessor.nudgeParams[t]->load();
        const float knobNormalized = juce::jmap(
            knobNudgeMs,
            MiniLAB3StepSequencerAudioProcessor::minMicroTimingMs,
            MiniLAB3StepSequencerAudioProcessor::maxMicroTimingMs,
            0.0f,
            1.0f
        );

        auto knobBounds = getNudgeKnobBounds(t);

        if (nudgeKnobStrip.isValid())
        {
            const int frameW = 30;
            const int frameH = 30;
            const int numFrames = 15;

            const int frameIndex = juce::jlimit(
                0, numFrames - 1,
                (int)std::round(knobNormalized * (float)(numFrames - 1))
            );

            g.drawImage(nudgeKnobStrip,
                knobBounds.getX(), knobBounds.getY(),
                knobBounds.getWidth(), knobBounds.getHeight(),
                0, frameIndex * frameH,
                frameW, frameH);
        }
        else
        {
            auto knobCentre = knobBounds.getCentre();
            const float radius = juce::jmin(knobBounds.getWidth(), knobBounds.getHeight()) * 0.5f - 2.0f;
            const float angle = juce::jmap(knobNormalized, 0.0f, 1.0f,
                -juce::MathConstants<float>::pi * 0.75f,
                juce::MathConstants<float>::pi * 0.75f);

            g.setColour(juce::Colour::fromRGB(35, 35, 40));
            g.fillEllipse(knobBounds);
            g.setColour(isCurrent ? juce::Colours::cyan.withAlpha(0.85f)
                : juce::Colours::grey.withAlpha(0.9f));
            g.drawEllipse(knobBounds, 1.0f);
            g.drawLine(knobCentre.x,
                knobCentre.y,
                knobCentre.x + std::cos(angle) * radius,
                knobCentre.y + std::sin(angle) * radius,
                2.0f);
        }

        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.setFont(10.0f);
        g.drawText(juce::String(knobNudgeMs, 1) + "ms",
            knobBounds.withY(knobBounds.getBottom() - 2.0f).translated(36.0f, -8.0f),
            juce::Justification::centredLeft,
            false);

        for (int s = 0; s < 32; ++s) {
            int x = startX + (s * cellW);
            float assetSize = 24.0f;
            float drawX = x + (cellW - assetSize) / 2.0f;
            float drawY = y + (cellH - assetSize) / 2.0f;
            juce::Rectangle<float> assetRect(drawX, drawY, assetSize, assetSize);

            int stepPage = s / 8;
            bool isFocusPage = (stepPage == currentPage);
            juce::Image* bgImage = (stepPage % 2 == 0) ? &inactiveBgA : &inactiveBgB;

            if (s < len && bgImage->isValid()) {
                g.setOpacity(isFocusPage ? 1.0f : 0.6f);
                g.drawImage(*bgImage, assetRect);
                g.setOpacity(1.0f);
            }

            if (s < len && audioProcessor.sequencerMatrix[t][s].isActive) {
                float vel = audioProcessor.sequencerMatrix[t][s].velocity;
                float alpha = isFocusPage ? 1.0f : 0.4f;
                alpha *= (0.2f + (vel * 0.8f));

                if (activeKeys[t].isValid()) {
                    g.setOpacity(alpha);
                    g.drawImage(activeKeys[t], assetRect);
                    g.setOpacity(1.0f);
                }
            }

            if (global16thNote >= 0 && s == (global16thNote % len)) {
                g.setColour(juce::Colours::white.withAlpha(0.7f));
                g.drawRect(assetRect, 1.5f);
            }
        }
    }

    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("PAGE " + juce::String(currentPage + 1), getWidth() - 150, 20, 130, 40, juce::Justification::centredRight);
}

void MiniLAB3StepSequencerAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    const int startX = 170;
    const int topY = 124;
    const int gridAreaW = getWidth() - startX - 25;
    const int cellW = gridAreaW / 32;
    const int cellH = (getHeight() - topY - 20) / 16;

    const int row = (e.y - topY) / cellH;

    auto notifyHostParameterChange = [&](juce::String paramID, float normalizedValue) {
        if (auto* p = audioProcessor.apvts.getParameter(paramID)) {
            p->beginChangeGesture();
            p->setValueNotifyingHost(normalizedValue);
            p->endChangeGesture();
        }
    };

    if (row >= 0 && row < 16) {
        if (getNudgeKnobBounds(row).contains(e.position)) {
            nudgeDragTrack = row;
            nudgeDragStartX = e.x;
            nudgeDragStartValue = audioProcessor.nudgeParams[row]->load();

            if (auto* p = audioProcessor.apvts.getParameter("nudge_" + juce::String(row + 1)))
                p->beginChangeGesture();

            return;
        }

        juce::String trkStr = juce::String(row + 1);

        if (e.x >= 60 && e.x <= 85) {
            int currentNote = static_cast<int>(audioProcessor.noteParams[row]->load());
            int newNote = e.mods.isRightButtonDown() ? currentNote - 1 : currentNote + 1;
            if (newNote > 127) newNote = 0;
            if (newNote < 0) newNote = 127;
            notifyHostParameterChange("note_" + trkStr, newNote / 127.0f);
        }
        else if (e.x >= 88 && e.x <= 106) {
            bool currentVal = audioProcessor.muteParams[row]->load() > 0.5f;
            notifyHostParameterChange("mute_" + trkStr, currentVal ? 0.0f : 1.0f);
        }
        else if (e.x >= 109 && e.x <= 127) {
            bool currentVal = audioProcessor.soloParams[row]->load() > 0.5f;
            notifyHostParameterChange("solo_" + trkStr, currentVal ? 0.0f : 1.0f);
        }
    }

    const int col = (e.x - startX) / cellW;
    if (col >= 0 && col < 32 && row >= 0 && row < 16 && e.x >= startX) {
        const juce::ScopedLock sl(audioProcessor.stateLock);
        audioProcessor.sequencerMatrix[row][col].isActive = !audioProcessor.sequencerMatrix[row][col].isActive;
        audioProcessor.sequencerMatrix[row][col].velocity = 0.8f;
        audioProcessor.updateTrackLength(row);
        audioProcessor.requestLedRefresh();
    }

    repaint();
}

void MiniLAB3StepSequencerAudioProcessorEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (nudgeDragTrack < 0)
        return;

    const float deltaX = static_cast<float>(e.x - nudgeDragStartX);
    const float valueRange = MiniLAB3StepSequencerAudioProcessor::maxMicroTimingMs
                           - MiniLAB3StepSequencerAudioProcessor::minMicroTimingMs;
    const float deltaMs = (deltaX / nudgeDragPixelsForFullRange) * valueRange;
    setTrackNudgeFromMs(nudgeDragTrack, nudgeDragStartValue + deltaMs);
    repaint();
}

void MiniLAB3StepSequencerAudioProcessorEditor::mouseUp(const juce::MouseEvent&)
{
    if (nudgeDragTrack >= 0)
    {
        if (auto* p = audioProcessor.apvts.getParameter("nudge_" + juce::String(nudgeDragTrack + 1)))
            p->endChangeGesture();
    }

    nudgeDragTrack = -1;
}

void MiniLAB3StepSequencerAudioProcessorEditor::resized() {}
