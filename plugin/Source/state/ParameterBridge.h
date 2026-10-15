#pragma once

// NOT YET COMPILED ANYWHERE - see docs/SETUP_WINDOWS.md.
//
// This file used to hold a simpler `ParameterBridge` class that bound a
// fixed five-module chain (NoiseGate -> Drive -> Amp -> Cab -> Eq) to APVTS
// once at construction. It has been rewritten in place - filename kept
// on purpose, since files in the OneDrive-synced project folder can't be
// deleted/renamed from here, and leaving a dead ParameterBridge.h/.cpp
// pair alongside a new SlotManager.h/.cpp would just be confusing dead
// weight - to become `SlotManager`, the pedalboard-slot equivalent: 8
// fixed slots, each independently choosing which module occupies it,
// rebinding its parameters live when that choice changes. See
// docs/ARCHITECTURE.md "Pedalboard slots" for the full design writeup and
// docs/ROADMAP.md's milestone log for why this replaced the fixed chain.
//
// ---------------------------------------------------------------------
// Why a slot's module type can change at runtime without breaking APVTS
// ---------------------------------------------------------------------
// juce::AudioProcessorValueTreeState needs its full parameter layout fixed
// at construction time - it can't grow/shrink/retype parameters later.
// So instead of "one parameter per tfcore::Parameter", each slot exposes a
// FIXED set of generic, always-present parameters to APVTS:
//   - "slot{N}_type"    : AudioParameterChoice, which module occupies this
//                         slot (see ModuleRegistry::types())
//   - "slot{N}_bypass"  : AudioParameterBool
//   - "slot{N}_knob{K}" : AudioParameterFloat, 0..1, K in [0, kKnobsPerSlot)
// The knobs are host-facing normalized floats, not the module's real
// units - when slot N holds Amp, knob 0 might mean "gain" in [0,1]; when
// it's switched to Eq, that same host parameter (same automation lane,
// same ID, same host-visible identity) instead means "low_gain_db" and
// gets denormalized into Eq's real [-12,12] range via
// tfcore::ParameterRange::denormalize(). The UI relabels the knob; the
// host doesn't need to know or care that the meaning changed.
//
// ---------------------------------------------------------------------
// Why rebuilding a slot happens on a timer, not directly in the parameter
// callback
// ---------------------------------------------------------------------
// juce::AudioProcessorValueTreeState::Listener::parameterChanged() can be
// invoked on the AUDIO THREAD - hosts are free to deliver automation for
// an AudioParameterChoice from inside processBlock(). Changing a slot's
// module type means constructing a new tfcore::IAudioModule (allocates),
// calling prepare() on it (may allocate), and rebuilding parameter
// bindings (allocates a std::vector) - none of that is legal on the audio
// thread. So parameterChanged() here does the absolute minimum possible:
// sets a lock-free atomic<bool> dirty flag and returns. A juce::Timer
// polling at a modest rate (20 Hz - more than fast enough for a human
// clicking a dropdown, nowhere near audio-rate) does the actual rebuild
// on the message thread, then publishes the result to the audio thread
// via the same atomic shared_ptr swap pattern tfcore::SignalChain already
// uses (see below) - never mutates anything the audio thread might be
// mid-read on.

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "tfcore/chain/SignalChain.h"
#include "tfcore/dsp/IAudioModule.h"
#include "tfcore/dsp/ParameterStore.h"

#include "state/ModuleRegistry.h"

namespace toneforge {

class SlotManager final : private juce::Timer,
                           private juce::AudioProcessorValueTreeState::Listener {
public:
    SlotManager(juce::AudioProcessorValueTreeState& apvts, tfcore::SignalChain& signalChain);
    ~SlotManager() override;

    // Builds the full 8-slots x (type + bypass + kKnobsPerSlot) parameter
    // layout. Static/parameterless like the old ParameterBridge's
    // createLayout() - must exist before the processor's APVTS is
    // constructed (member initializer list), so it can't depend on any
    // runtime SlotManager instance. Slots 0-4 default to
    // NoiseGate/Drive/Amp/Cab/Eq (today's fixed chain, preserved as the
    // out-of-box sound); slots 5-7 default to None.
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Call once, after the owning processor's apvts_ is fully constructed
    // (i.e. from the processor's constructor body, not its initializer
    // list). Registers parameter listeners, does an immediate synchronous
    // build of all 8 slots (so the default chain exists before the first
    // processBlock(), without waiting on the first timer tick), and
    // starts the rebuild-poll timer. Message thread only.
    void start();

    // Re-prepares every currently-live module for a new sample
    // rate/block size and republishes the chain. Call from
    // prepareToPlay(). Message thread only (JUCE guarantees this).
    void prepare(const tfcore::ProcessSpec& spec);

    // Immediately performs whatever rebuild the 20 Hz timer would
    // otherwise get to on its next tick (scans all slots' dirty flags,
    // rebuilds/rebinds any that changed, republishes). Message thread
    // only. Exists so callers that just drove several slot parameters
    // programmatically (PluginProcessor::applyPreset()) can make the
    // change take effect immediately instead of waiting up to ~50ms.
    void forceRebuildNow() { timerCallback(); }

    // RT-safe: reads one atomically-published, immutable snapshot built by
    // the message thread, then does one atomic load + denormalize + store
    // per bound knob and one atomic load + setBypassed() per slot. No
    // allocation, no locks. Call once per block from processBlock(),
    // before signalChain_.process().
    void syncToCore() noexcept;

    // Message-thread-safe read of a slot's current module type index
    // (matches ModuleRegistry::types() indices). Used by the editor's own
    // polling timer to know when to relabel a slot's knobs.
    [[nodiscard]] int currentTypeIndex(int slotIndex) const noexcept;

    static juce::String slotTypeParamId(int slotIndex);
    static juce::String slotBypassParamId(int slotIndex);
    static juce::String slotKnobParamId(int slotIndex, int knobIndex);

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void timerCallback() override;

    void rebuildSlot(int slotIndex); // message thread only
    void publishChain();             // message thread only

    struct KnobLink {
        std::atomic<float>* juceValue = nullptr;
        tfcore::Parameter* coreParam = nullptr;
        tfcore::ParameterRange range;
    };

    // Published together, atomically, for the audio thread to read via
    // syncToCore(). Once published, a given SlotBinding vector is never
    // mutated - a slot-type change builds an entirely new vector and
    // swaps the pointer, exactly like tfcore::ChainState/SignalChain.
    struct SlotBinding {
        std::shared_ptr<tfcore::IAudioModule> module;
        std::atomic<float>* bypassRaw = nullptr;
        std::vector<KnobLink> knobLinks;
    };

    // Message-thread-only bookkeeping - the audio thread never touches
    // this array, only the atomically-published activeBindings_ below.
    struct Slot {
        juce::AudioParameterChoice* typeParam = nullptr;
        std::atomic<bool> dirty{true}; // starts dirty so start()'s forced build constructs every slot
        int liveTypeIndex = -1;
        std::shared_ptr<tfcore::IAudioModule> module; // nullptr while type == None
    };

    juce::AudioProcessorValueTreeState& apvts_;
    tfcore::SignalChain& signalChain_;
    tfcore::ProcessSpec processSpec_{};

    std::array<Slot, kNumSlots> slots_{};
    std::array<juce::String, kNumSlots> typeParamIds_; // precomputed for allocation-free comparison in parameterChanged()

    // Audio-thread-readable snapshot. shared_ptr<const ...> so the audio
    // thread can hold a reference across its read without anyone mutating
    // the vector out from under it; publishChain() always builds a brand
    // new vector rather than editing this one in place.
    std::shared_ptr<const std::vector<SlotBinding>> activeBindings_;

    bool started_ = false;
};

} // namespace toneforge
