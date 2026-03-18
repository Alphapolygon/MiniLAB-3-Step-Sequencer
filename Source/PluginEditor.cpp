#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <vector>

MiniLAB3StepSequencerAudioProcessorEditor::MiniLAB3StepSequencerAudioProcessorEditor(MiniLAB3StepSequencerAudioProcessor& p)
    : AudioProcessorEditor(&p),
    audioProcessor(p),
    webComponent(
        juce::WebBrowserComponent::Options{}
        .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withWinWebView2Options(
            juce::WebBrowserComponent::Options::WinWebView2{}
            .withUserDataFolder(
                juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                .getChildFile("MiniLAB3Sequencer")
                .getChildFile("WebView2Cache")
            )
        )
        .withNativeFunction("updateCPlusPlusState",
            [this](const auto& args, auto completion) {
                if (args.size() > 0) {
                    juce::String jsonStr = args[0].toString();
                    audioProcessor.setStepDataFromJson(jsonStr);
                }
                completion(juce::var());
            }
        )
        .withNativeFunction("saveFullUiState",
            [this](const auto& args, auto completion) {
                if (args.size() > 0) {
                    audioProcessor.fullUiStateJson = args[0].toString();
                }
                completion(juce::var());
            }
        )
        // Allows React to pull the data back when the window opens
        .withNativeFunction("requestInitialState",
            [this](const auto& args, auto completion) {
                completion(juce::var(audioProcessor.fullUiStateJson));
            }
        )
#if JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
        .withResourceProvider(
            [](const juce::String& url) -> std::optional<juce::WebBrowserComponent::Resource> {
                if (url.isEmpty() || url == "/" || url.contains("index.html")) {
                    const auto* data = reinterpret_cast<const std::byte*>(BinaryData::index_html);
                    std::vector<std::byte> htmlVector(data, data + BinaryData::index_htmlSize);

                    return juce::WebBrowserComponent::Resource{
                        std::move(htmlVector),
                        juce::String("text/html")
                    };
                }
                return std::nullopt;
            }
        )
#endif
    )
{
    addAndMakeVisible(webComponent);
    setSize(1460, 1024);

#if JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
    juce::String rootUrl = webComponent.getResourceProviderRoot();
    if (!rootUrl.endsWithChar('/')) rootUrl += "/";
    webComponent.goToURL(rootUrl + "index.html");
#else
    webComponent.goToURL("about:blank");
#endif

    startTimerHz(30);
}

MiniLAB3StepSequencerAudioProcessorEditor::~MiniLAB3StepSequencerAudioProcessorEditor() {
    stopTimer();
}

void MiniLAB3StepSequencerAudioProcessorEditor::resized() {
    webComponent.setBounds(getLocalBounds());
}

void MiniLAB3StepSequencerAudioProcessorEditor::timerCallback() {
    double currentBpm = audioProcessor.currentBpm.load();
    bool isPlaying = audioProcessor.isPlaying.load();

    int absoluteStep = audioProcessor.global16thNote.load();
    int currentGridStep = (absoluteStep >= 0) ? (absoluteStep % 32) : -1;

    if (currentBpm != lastBpm || isPlaying != lastIsPlaying || currentGridStep != lastStep) {
        lastBpm = currentBpm;
        lastIsPlaying = isPlaying;
        lastStep = currentGridStep;

        juce::DynamicObject::Ptr stateObj = new juce::DynamicObject();
        stateObj->setProperty("bpm", currentBpm);
        stateObj->setProperty("isPlaying", isPlaying);
        stateObj->setProperty("currentStep", currentGridStep);

        webComponent.emitEventIfBrowserIsVisible("playbackState", juce::var(stateObj.get()));
    }
}