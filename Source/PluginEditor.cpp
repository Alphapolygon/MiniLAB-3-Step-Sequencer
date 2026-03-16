#include "PluginProcessor.h"
#include "PluginEditor.h"

// ==============================================================================
// LIFECYCLE
// ==============================================================================

MiniLAB3StepSequencerAudioProcessorEditor::MiniLAB3StepSequencerAudioProcessorEditor(MiniLAB3StepSequencerAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // Load static Photoshop UI background
    backgroundImage = juce::ImageCache::getFromMemory(BinaryData::MainBackground_png, BinaryData::MainBackground_pngSize);

    // Load Grid Assets
    inactiveBgA = juce::ImageCache::getFromMemory(BinaryData::bg_dark_png, BinaryData::bg_dark_pngSize);
    inactiveBgB = juce::ImageCache::getFromMemory(BinaryData::bg_light_png, BinaryData::bg_light_pngSize);

    // Load Unique Track Colors
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

    setSize(1050, 600);
    startTimerHz(60); // Repaint at 60 FPS
}

MiniLAB3StepSequencerAudioProcessorEditor::~MiniLAB3StepSequencerAudioProcessorEditor() {
    stopTimer();
}

// ==============================================================================
// UPDATES & PAINTING
// ==============================================================================

void MiniLAB3StepSequencerAudioProcessorEditor::timerCallback()
{
    // Auto-connect to hardware if unplugged/replugged
    if (!audioProcessor.isHardwareConnected()) {
        static int connectionRetry = 0;
        if (++connectionRetry >= 30) {
            audioProcessor.openHardwareOutput();
            connectionRetry = 0;
        }
    }

    // Handle the page-change glow animation
    if (audioProcessor.pageChangedTrigger.exchange(false))
        pageFlashAlpha = 1.0f;

    if (pageFlashAlpha > 0.0f)
        pageFlashAlpha *= 0.85f;

    repaint();
}

void MiniLAB3StepSequencerAudioProcessorEditor::paint(juce::Graphics& g)
{
    // 1. Draw Static Photoshop Background
    if (backgroundImage.isValid()) {
        g.drawImageAt(backgroundImage, 0, 0);
    }
    else {
        // Fallback if image is missing
        g.fillAll(juce::Colour::fromRGB(15, 15, 18));
    }

    // --- State Fetching ---
    const int currentPage = audioProcessor.currentPage.load();
    const int currentInstrument = audioProcessor.currentInstrument.load();
    const int global16thNote = audioProcessor.global16thNote.load();

    // --- Grid Coordinates ---
    // These coordinates correspond directly to the layout drawn in `MainBackground.png`
    int startX = 136;
    int topY = 124;

    int gridAreaW = getWidth() - startX - 25;       // Total area width for the 32 steps
    int cellW = gridAreaW / 32;                     // Width of a single cell bounding box
    int cellH = (getHeight() - topY - 20) / 16;     // Height of a single track row

    // Draw Page Change Flash Overlay
    if (pageFlashAlpha > 0.0f) {
        g.setColour(juce::Colours::white.withAlpha(pageFlashAlpha * 0.12f));
        g.fillRect(startX + (currentPage * 8 * cellW), topY, cellW * 8, cellH * 16);
    }

    const juce::ScopedLock sl(audioProcessor.stateLock);

    // Initialization check
    if (audioProcessor.instrumentNames[0].isEmpty()) {
        g.setColour(juce::Colours::white);
        g.drawText("Initializing Sequencer...", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // --- Draw the 16 Tracks ---
    for (int t = 0; t < 16; ++t) {
        int y = topY + (t * cellH);
        const int len = juce::jmax(1, audioProcessor.trackLengths[t]);
        const bool isCurrent = (t == currentInstrument);

        // Track Name & Selection Hilight
        juce::String name = audioProcessor.instrumentNames[t].isNotEmpty() ? audioProcessor.instrumentNames[t] : "Track";
        g.setColour(isCurrent ? juce::Colours::cyan : juce::Colours::white.withAlpha(0.5f));
        g.setFont(juce::FontOptions(13.0f, isCurrent ? juce::Font::bold : juce::Font::plain));
        g.drawFittedText(name, 10, y, 80, cellH, juce::Justification::centredLeft, 1);

        // UI 'Clear Track' Hitbox Visualization
        g.setColour(juce::Colours::red.withAlpha(0.2f));
        g.fillRoundedRectangle(95, y + 4, 22, cellH - 8, 4.0f);
        g.setColour(juce::Colours::red.withAlpha(0.7f));
        g.drawText("X", 95, y, 22, cellH, juce::Justification::centred);

        // --- Draw the 32 Steps ---
        for (int s = 0; s < 32; ++s) {
            int x = startX + (s * cellW);

            // Center the 24x24 asset squarely inside the dynamically calculated cell
            float assetSize = 24.0f;
            float drawX = x + (cellW - assetSize) / 2.0f;
            float drawY = y + (cellH - assetSize) / 2.0f;
            juce::Rectangle<float> assetRect(drawX, drawY, assetSize, assetSize);

            int stepPage = s / 8;
            bool isFocusPage = (stepPage == currentPage);

            // Alternate background images every 8 steps
            juce::Image* bgImage = (stepPage % 2 == 0) ? &inactiveBgA : &inactiveBgB;

            // 1. Inactive Background
            if (s < len && bgImage->isValid()) {
                // Dim notes slightly if they aren't on the page currently mapped to the hardware pads
                float bgAlpha = isFocusPage ? 1.0f : 0.6f;
                g.setOpacity(bgAlpha);
                g.drawImage(*bgImage, assetRect);
                g.setOpacity(1.0f); // Reset for next draw call
            }

            // 2. Active Note
            if (s < len && audioProcessor.sequencerMatrix[t][s].isActive) {
                float vel = audioProcessor.sequencerMatrix[t][s].velocity;

                // Math: Notes range from 20% to 100% opacity based on velocity
                float alpha = isFocusPage ? 1.0f : 0.4f;
                alpha *= (0.2f + (vel * 0.8f));

                if (activeKeys[t].isValid()) {
                    g.setOpacity(alpha);
                    g.drawImage(activeKeys[t], assetRect);
                    g.setOpacity(1.0f);
                }
            }

            // 3. Playhead Visual
            if (global16thNote >= 0 && s == (global16thNote % len)) {
                g.setColour(juce::Colours::white.withAlpha(0.7f));
                g.drawRect(assetRect, 1.5f);
            }
        }
    }

    // Main UI Page Indicator
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("PAGE " + juce::String(currentPage + 1), getWidth() - 150, 20, 130, 40, juce::Justification::centredRight);
}

// ==============================================================================
// USER INPUT
// ==============================================================================

void MiniLAB3StepSequencerAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    // --- MATCH THE HARDCODED POSITIONS FROM PAINT() ---
    int startX = 136;
    int topY = 124;

    int gridAreaW = getWidth() - startX - 25;
    int cellW = gridAreaW / 32;
    int cellH = (getHeight() - topY - 20) / 16;

    const int row = (e.y - topY) / cellH;

    const juce::ScopedLock sl(audioProcessor.stateLock);

    // X (Clear Track) Button clicked
    if (e.x > 95 && e.x < 117) {
        if (row >= 0 && row < 16) {
            for (int s = 0; s < 32; ++s) {
                audioProcessor.sequencerMatrix[row][s].isActive = false;
            }
            audioProcessor.updateTrackLength(row);
            audioProcessor.requestLedRefresh();
        }
    }

    // Grid clicked
    const int col = (e.x - startX) / cellW;
    if (col >= 0 && col < 32 && row >= 0 && row < 16) {
        // Toggle step
        audioProcessor.sequencerMatrix[row][col].isActive = !audioProcessor.sequencerMatrix[row][col].isActive;
        audioProcessor.sequencerMatrix[row][col].velocity = 0.8f; // Reset to default velocity on mouse-click

        audioProcessor.updateTrackLength(row);
        audioProcessor.requestLedRefresh();
    }

    repaint();
}

void MiniLAB3StepSequencerAudioProcessorEditor::resized() {}