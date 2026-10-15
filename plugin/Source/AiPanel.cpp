// NOT YET COMPILED ANYWHERE - see docs/SETUP_WINDOWS.md.

#include "AiPanel.h"

namespace toneforge {

AiPanel::AiPanel(ToneForgeAudioProcessor& processor) : processor_(processor) {
    promptBox_.setMultiLine(false);
    promptBox_.setReturnKeyStartsNewLine(false);
    promptBox_.setTextToShowWhenEmpty(
        "Describe a tone (Generate/Modify) or type a song title (Reference Song), e.g. \"Cliffs of Dover\"...",
        juce::Colours::grey);
    promptBox_.onReturnKey = [this] { onGenerateClicked(); };
    addAndMakeVisible(promptBox_);

    generateButton_.setButtonText("Generate Tone");
    generateButton_.onClick = [this] { onGenerateClicked(); };
    addAndMakeVisible(generateButton_);

    modifyButton_.setButtonText("Modify Tone");
    modifyButton_.onClick = [this] { onModifyClicked(); };
    addAndMakeVisible(modifyButton_);

    explainButton_.setButtonText("Explain Tone");
    explainButton_.onClick = [this] { onExplainClicked(); };
    addAndMakeVisible(explainButton_);

    referenceSongButton_.setButtonText("Reference Song");
    referenceSongButton_.onClick = [this] { onReferenceSongClicked(); };
    addAndMakeVisible(referenceSongButton_);

    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setFont(juce::Font(12.0f));
    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(statusLabel_);

    outputDisplay_.setMultiLine(true);
    outputDisplay_.setReadOnly(true);
    outputDisplay_.setScrollbarsShown(true);
    outputDisplay_.setCaretVisible(false);
    outputDisplay_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff222226));
    outputDisplay_.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
    outputDisplay_.setColour(juce::TextEditor::outlineColourId, juce::Colours::darkgrey);
    outputDisplay_.setFont(juce::Font(12.0f));
    addAndMakeVisible(outputDisplay_);

    checkHealthNow();
    startTimer(15000); // periodic reachability check - see timerCallback()
}

AiPanel::~AiPanel() {
    stopTimer();
}

void AiPanel::timerCallback() {
    checkHealthNow();
}

void AiPanel::checkHealthNow() {
    juce::Component::SafePointer<AiPanel> safeThis(this);
    processor_.getAiClient().checkHealth([safeThis](HealthResult result) {
        if (safeThis == nullptr) return; // panel/editor closed before this completed
        if (safeThis->requestInFlight_) return; // don't stomp an in-progress status message
        safeThis->showStatus(result.statusSummary, !result.reachable);
    });
}

void AiPanel::onGenerateClicked() {
    if (requestInFlight_) return;

    const auto prompt = promptBox_.getText().trim();
    if (prompt.isEmpty()) {
        showStatus("Type a description first.", true);
        return;
    }

    setBusy(true);
    showStatus("Generating...", false);

    juce::Component::SafePointer<AiPanel> safeThis(this);
    processor_.getAiClient().generate(prompt, [safeThis](GenerateResult result) {
        if (safeThis == nullptr) return;
        safeThis->handleGenerateResult(result);
    });
}

void AiPanel::onModifyClicked() {
    if (requestInFlight_) return;

    const auto instruction = promptBox_.getText().trim();
    if (instruction.isEmpty()) {
        showStatus("Type an instruction first, e.g. \"make it darker\".", true);
        return;
    }

    setBusy(true);
    showStatus("Modifying...", false);

    juce::Component::SafePointer<AiPanel> safeThis(this);
    processor_.getAiClient().modify(processor_.captureCurrentPreset(), instruction, [safeThis](GenerateResult result) {
        if (safeThis == nullptr) return;
        safeThis->handleGenerateResult(result);
    });
}

void AiPanel::onExplainClicked() {
    if (requestInFlight_) return;

    setBusy(true);
    showStatus("Explaining...", false);

    juce::Component::SafePointer<AiPanel> safeThis(this);
    processor_.getAiClient().explain(processor_.captureCurrentPreset(), [safeThis](ExplainResult result) {
        if (safeThis == nullptr) return;
        safeThis->setBusy(false);
        if (result.success) {
            safeThis->showStatus("Explanation ready.", false);
            safeThis->showOutput(result.text);
        } else {
            safeThis->showStatus(result.text, true);
        }
    });
}

void AiPanel::onReferenceSongClicked() {
    if (requestInFlight_) return;

    const auto query = promptBox_.getText().trim();
    if (query.isEmpty()) {
        showStatus("Type a song title (optionally with artist) first, e.g. \"Cliffs of Dover\".", true);
        return;
    }

    setBusy(true);
    showStatus("Looking up song reference...", false);

    juce::Component::SafePointer<AiPanel> safeThis(this);
    processor_.getAiClient().songReference(query, [safeThis](GenerateResult result) {
        if (safeThis == nullptr) return;
        safeThis->handleGenerateResult(result);
    });
}

void AiPanel::handleGenerateResult(GenerateResult result) {
    setBusy(false);

    if (!result.success) {
        showStatus(result.errorMessage, true);
        return;
    }

    // Drives the pedalboard slot APVTS parameters exactly like the UI
    // does - see PluginProcessor.h's applyPreset() comment for why that
    // matters (no separate "construct modules directly" path to desync).
    processor_.applyPreset(result.preset);

    showStatus("Applied \"" + result.preset.name + "\"", false);

    // The disclaimer is not optional decoration - docs/ARCHITECTURE.md
    // section 5.2 requires any AI-touched preset to always surface it, so
    // a user can never mistake an AI-generated starting point for an
    // exact recreation of whatever they described or referenced.
    juce::String output = result.preset.name;
    // result.preset.description is a plain std::string (tfcore has no JUCE
    // dependency - see docs/ARCHITECTURE.md section 3) - .empty(), not
    // .isEmpty() (that's juce::String's own, unrelated, method). Caught by
    // a real MSVC build (C2039: 'isEmpty' is not a member of std::string),
    // not guessed in advance.
    if (!result.preset.description.empty()) output << "\n" << result.preset.description;
    if (result.preset.aiProvenance.has_value()) {
        output << "\n\n" << juce::String(result.preset.aiProvenance->disclaimer);
    }
    showOutput(output);
}

void AiPanel::setBusy(bool busy) {
    requestInFlight_ = busy;
    generateButton_.setEnabled(!busy);
    modifyButton_.setEnabled(!busy);
    explainButton_.setEnabled(!busy);
    referenceSongButton_.setEnabled(!busy);
}

void AiPanel::showStatus(const juce::String& text, bool isError) {
    statusLabel_.setText(text, juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId, isError ? juce::Colours::orangered : juce::Colours::lightgrey);
}

void AiPanel::showOutput(const juce::String& text) {
    outputDisplay_.setText(text, juce::dontSendNotification);
}

void AiPanel::resized() {
    auto bounds = getLocalBounds().reduced(4);

    auto row = bounds.removeFromTop(26);
    generateButton_.setBounds(row.removeFromRight(110));
    row.removeFromRight(4);
    modifyButton_.setBounds(row.removeFromRight(110));
    row.removeFromRight(4);
    explainButton_.setBounds(row.removeFromRight(110));
    row.removeFromRight(4);
    referenceSongButton_.setBounds(row.removeFromRight(120));
    row.removeFromRight(6);
    promptBox_.setBounds(row);

    bounds.removeFromTop(4);
    statusLabel_.setBounds(bounds.removeFromTop(16));

    bounds.removeFromTop(2);
    outputDisplay_.setBounds(bounds);
}

} // namespace toneforge
