#pragma once

// NOT YET COMPILED ANYWHERE - see docs/SETUP_WINDOWS.md.
//
// The AI panel: a prompt box, Generate/Modify/Explain/Reference Song
// buttons, a status line, and a read-only output area for explanations and
// the mandatory "Inspired by - approximation" style disclaimer
// (docs/ARCHITECTURE.md section 5.2 - a preset an AI touched must never be
// presented as an exact recreation, and that's enforced by always
// surfacing aiProvenance.disclaimer here, not just carrying it silently in
// data). "Reference Song" reuses the same prompt box as a song/artist
// query (e.g. "Cliffs of Dover") and hits POST /song-reference instead of
// /generate - a curated match is deterministic and never touches an AI
// provider at all, so it's a good way to check the actual DSP chain sounds
// right independent of how good the AI's own reasoning is.

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ai/AiClient.h"

namespace toneforge {

// Owned by ToneForgeAudioProcessorEditor, which can be closed and
// destroyed while an AI request it started is still in flight (a local
// LLM response can take many seconds - see AiClient.h). Every completion
// callback registered here captures a juce::Component::SafePointer<AiPanel>
// rather than a raw `this`, and checks it for null before touching any
// member state - the standard JUCE idiom for "a background operation may
// outlive the UI that requested it," and the reason this class is safe to
// close mid-request instead of needing to block the message thread
// waiting for the request to finish first.
class AiPanel final : public juce::Component, private juce::Timer {
public:
    explicit AiPanel(ToneForgeAudioProcessor& processor);
    ~AiPanel() override;

    void resized() override;

private:
    void timerCallback() override; // periodic backend-reachability check

    void onGenerateClicked();
    void onModifyClicked();
    void onExplainClicked();
    void onReferenceSongClicked();

    void checkHealthNow();
    void handleGenerateResult(GenerateResult result);

    void setBusy(bool busy);
    void showStatus(const juce::String& text, bool isError);
    void showOutput(const juce::String& text);

    ToneForgeAudioProcessor& processor_;
    bool requestInFlight_ = false;

    juce::TextEditor promptBox_;
    juce::TextButton generateButton_;
    juce::TextButton modifyButton_;
    juce::TextButton explainButton_;
    juce::TextButton referenceSongButton_;
    juce::Label statusLabel_;
    juce::TextEditor outputDisplay_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AiPanel)
};

} // namespace toneforge
