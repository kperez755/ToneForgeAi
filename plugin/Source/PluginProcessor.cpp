// NOT YET COMPILED ANYWHERE - see docs/SETUP_WINDOWS.md and the header
// comment in PluginProcessor.h.

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace toneforge {

ToneForgeAudioProcessor::ToneForgeAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "PARAMETERS", SlotManager::createLayout()),
      slotManager_(apvts_, signalChain_) {
    // slotManager_.start() must run after apvts_'s parameter layout is
    // fully constructed - true here since member initializers (including
    // apvts_ and slotManager_'s constructor) have already run by the time
    // this constructor body executes. start() registers slot-type
    // parameter listeners and does an immediate synchronous build of the
    // default 8-slot chain (Noise Gate -> Drive -> Amp -> Cab -> Eq in
    // slots 1-5, empty slots 6-8) so a fresh instance has real tonal
    // character before the host ever calls processBlock() - see the M2
    // milestone log in docs/ROADMAP.md for why a raw passthrough default
    // was a real, user-noticed gap the first time around.
    slotManager_.start();
}

void ToneForgeAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    processSpec_ = tfcore::ProcessSpec{sampleRate, samplesPerBlock, getTotalNumInputChannels()};
    slotManager_.prepare(processSpec_); // re-prepares every currently-live slot module and republishes the chain
}

void ToneForgeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    // RT-safe: reads one atomically-published snapshot and does a bounded
    // number of atomic load/denormalize/store operations - see
    // SlotManager::syncToCore()'s own comment for the full reasoning on
    // why the actual module (re)construction never happens on this
    // thread.
    slotManager_.syncToCore();

    // Adapter: wraps the host's existing channel pointers, does not copy.
    // A real implementation needs tfcore::AudioBuffer to support wrapping
    // external storage rather than always owning its own std::vector - that
    // is a small, deliberate follow-up (tfcore::AudioBuffer currently
    // always allocates in resize()) tracked for the first real M3 PR, not
    // hidden here.
    tfcore::AudioBuffer tfBuffer(buffer.getNumChannels(), buffer.getNumSamples());
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        std::copy_n(buffer.getReadPointer(ch), buffer.getNumSamples(), tfBuffer.channel(ch));
    }

    signalChain_.process(tfBuffer);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        std::copy_n(tfBuffer.channel(ch), buffer.getNumSamples(), buffer.getWritePointer(ch));
    }
}

juce::AudioProcessorEditor* ToneForgeAudioProcessor::createEditor() {
    return new ToneForgeAudioProcessorEditor(*this);
}

void ToneForgeAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    // Standard JUCE APVTS save: slot types + bypass states + knob
    // positions round-trip through the host's session save/recall. This
    // is deliberately NOT the same thing as a tfcore::Preset - it has no
    // name, tags, or AI provenance, and doesn't go through
    // PresetSerializer at all. Full Preset JSON save/load (needed for the
    // preset browser, export/import, "Save Preset" in the AI panel) is
    // still docs/ROADMAP.md M4.
    // Uses ValueTree's own binary stream (de)serialization rather than the
    // XML-based copyXmlToBinary/getXmlFromBinary helpers - those turned out
    // not to be free functions in the `juce` namespace in this JUCE version
    // (a real build error caught on the first Windows attempt, not
    // guessed-and-hoped-for). ValueTree::writeToStream/readFromData are
    // core, stable ValueTree methods with no such ambiguity.
    auto state = apvts_.copyState();
    juce::MemoryOutputStream stream(destData, false);
    state.writeToStream(stream);
}

void ToneForgeAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto tree = juce::ValueTree::readFromData(data, static_cast<size_t>(sizeInBytes));
    if (tree.isValid() && tree.hasType(apvts_.state.getType())) {
        apvts_.replaceState(tree);
        // replaceState() fires the same parameter-changed notifications a
        // user dragging the UI would, so SlotManager's dirty flags for
        // every slot whose type differs from its current live state get
        // set the normal way - no separate rebuild call needed here, the
        // next ~50ms timer tick (or the next processBlock(), whichever a
        // host triggers first) picks it up. See the "why a timer, not the
        // callback itself" note in state/ParameterBridge.h.
    }
}

void ToneForgeAudioProcessor::applyPreset(const tfcore::Preset& preset) {
    for (int slotIndex = 0; slotIndex < toneforge::kNumSlots; ++slotIndex) {
        if (slotIndex >= static_cast<int>(preset.chain.size())) {
            // Preset is shorter than kNumSlots - clear any stale module a
            // previously loaded (longer) preset or manual editing left
            // behind, rather than leaving it in place.
            if (auto* typeParam = dynamic_cast<juce::AudioParameterChoice*>(
                    apvts_.getParameter(SlotManager::slotTypeParamId(slotIndex)))) {
                *typeParam = 0; // None
            }
            continue;
        }

        const auto& moduleState = preset.chain[static_cast<std::size_t>(slotIndex)];
        const int typeIndex = ModuleRegistry::indexForModuleId(moduleState.moduleId);

        if (auto* typeParam = dynamic_cast<juce::AudioParameterChoice*>(
                apvts_.getParameter(SlotManager::slotTypeParamId(slotIndex)))) {
            *typeParam = typeIndex;
        }

        if (typeIndex == 0) continue; // unknown/not-yet-implemented module id - slot left empty, not silently wrong

        if (auto* bypassParam = dynamic_cast<juce::AudioParameterBool*>(
                apvts_.getParameter(SlotManager::slotBypassParamId(slotIndex)))) {
            *bypassParam = moduleState.bypassed;
        }

        // Walk this module type's parameters in the same order
        // ModuleRegistry/SlotManager bind knobs in, and for each one that
        // the preset actually specifies a value for, push it into the
        // matching generic knob parameter (normalized into [0,1], since
        // that's what the knob parameters store - see
        // state/ParameterBridge.h's "why a slot's module type can change"
        // note). Parameters the preset doesn't mention keep whatever
        // that knob's current value already is.
        ModuleRegistry::describeType(typeIndex, [&](const tfcore::Parameter& templateParam, int knobIndex) {
            auto it = moduleState.parameters.find(templateParam.id());
            if (it == moduleState.parameters.end()) return;

            if (auto* knobParam = dynamic_cast<juce::AudioParameterFloat*>(
                    apvts_.getParameter(SlotManager::slotKnobParamId(slotIndex, knobIndex)))) {
                *knobParam = templateParam.range().normalize(it->second);
            }
        });
    }

    // The parameter writes above only set dirty flags (via
    // SlotManager::parameterChanged() for the type parameters); force the
    // actual module rebuild/rebind/publish now rather than waiting up to
    // ~50ms for the next timer tick, so a preset load takes effect
    // immediately rather than with a barely-perceptible lag.
    slotManager_.forceRebuildNow();
}

tfcore::Preset ToneForgeAudioProcessor::captureCurrentPreset() const {
    tfcore::Preset preset;
    preset.id = "current-session";
    preset.name = "Current Tone";
    preset.description = "Snapshot of the pedalboard's current state, sent as context for Modify/Explain.";
    preset.schemaVersion = tfcore::kCurrentPresetSchemaVersion;

    const auto& types = ModuleRegistry::types();

    for (int slotIndex = 0; slotIndex < toneforge::kNumSlots; ++slotIndex) {
        auto* typeParam = dynamic_cast<juce::AudioParameterChoice*>(
            apvts_.getParameter(SlotManager::slotTypeParamId(slotIndex)));
        const int typeIndex = typeParam ? typeParam->getIndex() : 0;
        if (typeIndex <= 0 || typeIndex >= static_cast<int>(types.size())) continue; // empty slot

        tfcore::ModuleState state;
        state.moduleId = types[static_cast<std::size_t>(typeIndex)].moduleId;

        if (auto* bypassRaw = apvts_.getRawParameterValue(SlotManager::slotBypassParamId(slotIndex))) {
            state.bypassed = bypassRaw->load() > 0.5f;
        }

        ModuleRegistry::describeType(typeIndex, [&](const tfcore::Parameter& templateParam, int knobIndex) {
            if (auto* knobRaw = apvts_.getRawParameterValue(SlotManager::slotKnobParamId(slotIndex, knobIndex))) {
                state.parameters[templateParam.id()] = templateParam.range().denormalize(knobRaw->load());
            }
        });

        preset.chain.push_back(std::move(state));
    }

    return preset;
}

} // namespace toneforge

// This create() function is required by every JUCE plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new toneforge::ToneForgeAudioProcessor();
}
