#pragma once

// NOT YET COMPILED ANYWHERE - see docs/SETUP_WINDOWS.md.
//
// Pedalboard UI: 8 fixed slot panels, each with a "Module Type" dropdown,
// a bypass toggle, and up to 8 rotary knobs that relabel (and enable/
// disable) themselves to match whatever module the dropdown currently
// selects. Replaces the earlier fixed five-column layout - see
// docs/ARCHITECTURE.md "Pedalboard slots" and the ROADMAP.md milestone
// log entry for this change.
//
// Note on ordering: slots are processed left-to-right as the actual
// signal chain order (slot 0 first). There is no drag-to-reorder yet -
// "any pedal, any position" today means picking module types in the
// slot order you want them processed in, not dragging panels around.
// That's a real, deliberately deferred UI polish item (tracked in
// docs/ROADMAP.md), not a limitation of the underlying architecture -
// the slot data model already supports reordering; only the UI gesture
// for it doesn't exist yet.

#include <array>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "AiPanel.h"
#include "PluginProcessor.h"
#include "state/ModuleRegistry.h"
#include "state/ParameterBridge.h"

namespace toneforge {

// One pedalboard slot: type dropdown + bypass + a fixed pool of
// kKnobsPerSlot rotary knobs. The knobs' labels/visibility are NOT bound
// via APVTS (there's nothing to bind - a label isn't a parameter); they're
// pushed by refreshLabelsIfNeeded(), which the owning editor calls from
// its own polling timer whenever it notices (via
// SlotManager::currentTypeIndex()) that this slot's type changed.
class SlotPanel final : public juce::Component {
public:
    SlotPanel(int slotIndex, ToneForgeAudioProcessor& processor);

    void resized() override;

    // Message thread only. Cheap (one int comparison) when nothing has
    // changed; only touches labels/visibility (no parameter writes) when
    // it has.
    void refreshLabelsIfNeeded();

private:
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    // Every generic knob parameter is, underneath, a continuous [0,1]
    // AudioParameterFloat (see state/ParameterBridge.h - the pedalboard's
    // knob pool has to be uniform so APVTS's layout stays fixed while the
    // module bound to it changes). Most of the time that's presented as a
    // rotary knob; but when the currently-bound module says this
    // parameter is really a small discrete choice (tfcore::Parameter::
    // isDiscreteChoice(), e.g. Reverb's "character" - Hall/Plate/Spring/
    // Room), a plain numeric knob position is a bad way to present four
    // named options, so applyLabelsForType() swaps in a labeled
    // juce::ComboBox instead. Both widgets bind to the exact same
    // parameter ID via JUCE's generic attachment machinery (ComboBox item
    // index N maps to normalized position N/(numItems-1), same as it
    // would for an AudioParameterChoice) - only one is visible/attached
    // at a time; the other's attachment is torn down so there's never two
    // live listeners fighting over the same parameter.
    struct KnobControl {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<SliderAttachment> sliderAttachment;
        juce::ComboBox comboBox;
        std::unique_ptr<ComboBoxAttachment> comboBoxAttachment;
    };

    void applyLabelsForType(int typeIndex);

    int slotIndex_;
    ToneForgeAudioProcessor& processor_;
    int lastSeenTypeIndex_ = -1;

    juce::GroupComponent group_;
    juce::ComboBox typeCombo_;
    std::unique_ptr<ComboBoxAttachment> typeAttachment_;
    juce::ToggleButton bypassButton_;
    std::unique_ptr<ButtonAttachment> bypassAttachment_;
    std::array<std::unique_ptr<KnobControl>, kKnobsPerSlot> knobs_;
};

class ToneForgeAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit ToneForgeAudioProcessorEditor(ToneForgeAudioProcessor& processor);
    ~ToneForgeAudioProcessorEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override; // message thread: polls each slot for a type change and relabels its knobs

    ToneForgeAudioProcessor& processor_;
    juce::Label titleLabel_;
    std::array<std::unique_ptr<SlotPanel>, kNumSlots> slotPanels_;

    // Sits in a fixed strip below the pedalboard slots - see AiPanel.h/.cpp
    // for the Generate/Modify/Explain wiring itself. processor_ is declared
    // above this member so it's already fully constructed by the time
    // aiPanel_'s constructor (which stores a reference to it) runs.
    AiPanel aiPanel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneForgeAudioProcessorEditor)
};

} // namespace toneforge
