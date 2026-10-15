#pragma once

// NOT YET COMPILED ANYWHERE - see docs/SETUP_WINDOWS.md. This is the
// intended shape of the JUCE <-> tfcore boundary (M3 in docs/ROADMAP.md),
// written to establish the integration pattern described in
// docs/ARCHITECTURE.md section 4, not verified against a real JUCE build.

#include <juce_audio_processors/juce_audio_processors.h>

#include "tfcore/chain/SignalChain.h"
#include "tfcore/preset/PresetSerializer.h"

#include "ai/AiClient.h"
#include "state/ModuleRegistry.h"
#include "state/ParameterBridge.h" // now hosts SlotManager - see that file's header comment

namespace toneforge {

// Owns the tfcore::SignalChain and bridges JUCE's processBlock/parameter
// model to it. Deliberately thin: almost all actual behavior lives in
// tfcore (DSP) and SlotManager (parameter/chain-topology bridging), which
// is what makes this class testable-by-inspection rather than needing a
// DAW to reason about correctness.
class ToneForgeAudioProcessor final : public juce::AudioProcessor {
public:
    ToneForgeAudioProcessor();
    ~ToneForgeAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    // RT-safe: syncs current knob/bypass values into whichever modules
    // currently occupy each pedalboard slot (SlotManager::syncToCore()),
    // then loads the current tfcore::ChainState via one atomic read and
    // delegates. Must not allocate - see docs/ARCHITECTURE.md section 1
    // and section 4. Converting between juce::AudioBuffer<float> and
    // tfcore::AudioBuffer here must not allocate either; the adapter should
    // wrap the host buffer's existing channel pointers rather than copying.
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ToneForge AI"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // Saves/restores APVTS parameter state (slot types, bypass states,
    // knob positions) for host session recall via ValueTree's binary
    // stream serialization. This is NOT the same thing as a tfcore::Preset
    // (name/tags/AI provenance/etc via PresetSerializer) - that JSON-based
    // preset system is still a docs/ROADMAP.md M4 item. Off audio thread
    // only (JUCE guarantees these are called on the message thread).
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Applies a newly loaded/generated preset by driving the pedalboard
    // slot APVTS parameters (slot type, bypass, knob values) exactly the
    // way the UI does - preset.chain[i] goes into slot i (extra chain
    // entries beyond kNumSlots are dropped; unknown/not-yet-implemented
    // module ids leave that slot as None rather than guessing). This goes
    // through the same SlotManager rebuild path a user clicking a slot's
    // type dropdown does, so there's no separate "construct modules and
    // hope the bindings stay in sync" code path to get wrong - which is
    // exactly what caused the dangling-pointer bug this replaces (see the
    // M3 milestone log in docs/ROADMAP.md for that history). Off audio
    // thread only. See docs/THREAD_MODEL.md "Sequence: user loads a
    // preset".
    void applyPreset(const tfcore::Preset& preset);

    // Reads the CURRENT live pedalboard state (every slot's type, bypass,
    // and real denormalized parameter values, read straight from APVTS)
    // into a tfcore::Preset value - the inverse of applyPreset(). This is
    // what "Modify Tone" and "Explain Tone" send to the AI backend as
    // context (docs/ARCHITECTURE.md section 5) - they operate on "what's
    // dialed in right now," not on a saved/named preset. The returned
    // Preset is a snapshot, not a live reference - it's copied on the
    // spot and safe to hand to AiClient's methods, which move it onto a
    // background thread. Off audio thread only (reads APVTS parameter
    // values, which is a message-thread operation like everything else
    // that touches apvts_ directly rather than through SlotManager's
    // RT-safe atomic snapshot).
    [[nodiscard]] tfcore::Preset captureCurrentPreset() const;

    juce::AudioProcessorValueTreeState& getApvts() noexcept { return apvts_; }
    SlotManager& getSlotManager() noexcept { return slotManager_; }
    AiClient& getAiClient() noexcept { return aiClient_; }

private:
    tfcore::SignalChain signalChain_;
    tfcore::ProcessSpec processSpec_;
    juce::AudioProcessorValueTreeState apvts_;
    SlotManager slotManager_;
    AiClient aiClient_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToneForgeAudioProcessor)
};

} // namespace toneforge
