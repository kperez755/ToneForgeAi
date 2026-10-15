// NOT YET COMPILED ANYWHERE - see docs/SETUP_WINDOWS.md.

#include "PluginEditor.h"

namespace toneforge {

SlotPanel::SlotPanel(int slotIndex, ToneForgeAudioProcessor& processor)
    : slotIndex_(slotIndex), processor_(processor) {
    group_.setText("Slot " + juce::String(slotIndex_ + 1));
    group_.setColour(juce::GroupComponent::textColourId, juce::Colours::lightgrey);
    group_.setColour(juce::GroupComponent::outlineColourId, juce::Colours::darkgrey);
    addAndMakeVisible(group_);

    auto& apvts = processor_.getApvts();

    const auto typeNames = ModuleRegistry::typeDisplayNames();
    for (int i = 0; i < typeNames.size(); ++i) {
        // ComboBox item IDs are 1-based; AudioProcessorValueTreeState::
        // ComboBoxAttachment maps item ID N to AudioParameterChoice index
        // N-1, so items must be added in exactly this order for slot type
        // index 0 ("None") to land on item ID 1, index 1 on item ID 2, etc.
        typeCombo_.addItem(typeNames[i], i + 1);
    }
    addAndMakeVisible(typeCombo_);
    typeAttachment_ = std::make_unique<ComboBoxAttachment>(
        apvts, SlotManager::slotTypeParamId(slotIndex_), typeCombo_);

    bypassButton_.setButtonText("Bypass");
    addAndMakeVisible(bypassButton_);
    bypassAttachment_ = std::make_unique<ButtonAttachment>(
        apvts, SlotManager::slotBypassParamId(slotIndex_), bypassButton_);

    for (int k = 0; k < kKnobsPerSlot; ++k) {
        auto control = std::make_unique<KnobControl>();
        control->label.setJustificationType(juce::Justification::centred);
        control->label.setColour(juce::Label::textColourId, juce::Colours::white);
        control->label.setFont(juce::Font(11.0f));
        addAndMakeVisible(control->label);

        control->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        control->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 16);
        addAndMakeVisible(control->slider);

        // Added but not shown yet - applyLabelsForType() below decides,
        // per knob, whether the slider or this combo box is the live
        // widget for whatever module currently occupies this slot.
        addChildComponent(control->comboBox);

        knobs_[static_cast<std::size_t>(k)] = std::move(control);
    }

    // Set initial labels/visibility to match whatever type this slot
    // already resolved to (SlotManager::start() ran synchronously before
    // the editor was ever created, so currentTypeIndex() is already
    // meaningful here, not a placeholder).
    applyLabelsForType(processor_.getSlotManager().currentTypeIndex(slotIndex_));
}

void SlotPanel::applyLabelsForType(int typeIndex) {
    lastSeenTypeIndex_ = typeIndex;

    struct KnobMeta {
        juce::String label;
        bool active = false;
        bool discrete = false;
        juce::StringArray choices; // only meaningful when discrete
    };
    std::array<KnobMeta, kKnobsPerSlot> meta{};

    ModuleRegistry::describeType(typeIndex, [&](const tfcore::Parameter& p, int knobIndex) {
        auto& m = meta[static_cast<std::size_t>(knobIndex)];
        m.active = true;
        m.label = juce::String(p.displayName());
        m.discrete = p.isDiscreteChoice();
        if (m.discrete) {
            for (const auto& choice : p.valueLabels()) m.choices.add(juce::String(choice));
        }
    });

    auto& apvts = processor_.getApvts();

    for (int k = 0; k < kKnobsPerSlot; ++k) {
        auto& control = *knobs_[static_cast<std::size_t>(k)];
        const auto& m = meta[static_cast<std::size_t>(k)];

        control.label.setText(m.active ? m.label : juce::String(), juce::dontSendNotification);
        control.label.setVisible(m.active);

        if (m.active && m.discrete) {
            // Tear down the slider's attachment (if any) first so there's
            // never a moment with two live attachments driving the same
            // underlying parameter.
            control.sliderAttachment.reset();
            control.slider.setVisible(false);

            control.comboBox.clear(juce::dontSendNotification);
            for (int i = 0; i < m.choices.size(); ++i) control.comboBox.addItem(m.choices[i], i + 1);
            control.comboBox.setVisible(true);
            control.comboBoxAttachment = std::make_unique<ComboBoxAttachment>(
                apvts, SlotManager::slotKnobParamId(slotIndex_, k), control.comboBox);
        } else {
            control.comboBoxAttachment.reset();
            control.comboBox.setVisible(false);

            control.slider.setVisible(m.active);
            if (m.active) {
                control.sliderAttachment = std::make_unique<SliderAttachment>(
                    apvts, SlotManager::slotKnobParamId(slotIndex_, k), control.slider);
            } else {
                control.sliderAttachment.reset();
            }
        }
    }

    resized(); // a widget swap may need different bounds than the previous one had
}

void SlotPanel::refreshLabelsIfNeeded() {
    const int current = processor_.getSlotManager().currentTypeIndex(slotIndex_);
    if (current != lastSeenTypeIndex_) applyLabelsForType(current);
}

void SlotPanel::resized() {
    auto bounds = getLocalBounds();
    group_.setBounds(bounds);

    auto content = bounds.reduced(6, 20); // leave room for the group's border/title
    typeCombo_.setBounds(content.removeFromTop(22));
    content.removeFromTop(4);
    bypassButton_.setBounds(content.removeFromTop(20));
    content.removeFromTop(4);

    constexpr int cols = 2;
    constexpr int rows = kKnobsPerSlot / cols;
    const int cellWidth = content.getWidth() / cols;
    const int cellHeight = content.getHeight() / rows;

    for (int k = 0; k < kKnobsPerSlot; ++k) {
        auto& control = *knobs_[static_cast<std::size_t>(k)];
        const int row = k / cols;
        const int col = k % cols;
        juce::Rectangle<int> cell(content.getX() + col * cellWidth,
                                   content.getY() + row * cellHeight,
                                   cellWidth,
                                   cellHeight);
        control.label.setBounds(cell.removeFromTop(14));
        auto widgetArea = cell.reduced(2);
        control.slider.setBounds(widgetArea);
        // A combo box reads better as a short dropdown than stretched to
        // fill a knob-sized cell - centre a normal-height one in the same
        // space the slider would have used.
        control.comboBox.setBounds(widgetArea.withSizeKeepingCentre(widgetArea.getWidth(), 22));
    }
}

ToneForgeAudioProcessorEditor::ToneForgeAudioProcessorEditor(ToneForgeAudioProcessor& processor)
    : AudioProcessorEditor(&processor), processor_(processor), aiPanel_(processor_) {
    titleLabel_.setText("ToneForge AI", juce::dontSendNotification);
    titleLabel_.setJustificationType(juce::Justification::centred);
    titleLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    titleLabel_.setFont(juce::Font(20.0f, juce::Font::bold));
    addAndMakeVisible(titleLabel_);

    for (int i = 0; i < kNumSlots; ++i) {
        auto panel = std::make_unique<SlotPanel>(i, processor_);
        addAndMakeVisible(*panel);
        slotPanels_[static_cast<std::size_t>(i)] = std::move(panel);
    }

    addAndMakeVisible(aiPanel_);

    setSize(1360, 700);

    // 10 Hz is plenty responsive for "I just changed a dropdown" while
    // being negligible message-thread cost the rest of the time - this is
    // a UI-only concern, entirely separate from SlotManager's own 20 Hz
    // rebuild timer (see state/ParameterBridge.h for why that one exists).
    startTimerHz(10);
}

ToneForgeAudioProcessorEditor::~ToneForgeAudioProcessorEditor() {
    stopTimer();
}

void ToneForgeAudioProcessorEditor::timerCallback() {
    for (auto& panel : slotPanels_) {
        if (panel) panel->refreshLabelsIfNeeded();
    }
}

void ToneForgeAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1a1a1e)); // dark theme base, per product brief
}

void ToneForgeAudioProcessorEditor::resized() {
    auto bounds = getLocalBounds();
    titleLabel_.setBounds(bounds.removeFromTop(32));

    // AI panel gets a fixed-height strip at the bottom (docs/ARCHITECTURE.md
    // section 5's Generate/Modify/Explain controls); the pedalboard slots
    // take whatever's left above it, same left-to-right layout as before.
    aiPanel_.setBounds(bounds.removeFromBottom(180).reduced(4));

    const int columnWidth = bounds.getWidth() / kNumSlots;
    for (auto& panel : slotPanels_) {
        if (panel) panel->setBounds(bounds.removeFromLeft(columnWidth).reduced(4));
    }
}

} // namespace toneforge
