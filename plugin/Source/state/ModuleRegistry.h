#pragma once

// NOT YET COMPILED ANYWHERE - see docs/SETUP_WINDOWS.md.
//
// Central registry of "what module types exist" for the pedalboard slot
// system (docs/ARCHITECTURE.md "Pedalboard slots"). This is the one place
// that has to change when a new tfcore module lands - createLayout(),
// the editor's type dropdown, PluginProcessor::createModule()'s old
// per-module-id switch, and SlotManager's rebuild logic all used to need
// their own copy of "which module ids exist"; now they all go through
// ModuleRegistry::types()/create() instead.

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "tfcore/dsp/IAudioModule.h"
#include "tfcore/dsp/ParameterStore.h"

namespace toneforge {

// A pedalboard has a fixed number of slots, and each slot exposes a fixed
// number of generic host-automatable knob parameters. Both numbers are
// deliberately generous headroom over today's modules (Amp, the largest,
// has 5 parameters) so new module types can land without another APVTS
// layout break. If a future module ever needs more than kKnobsPerSlot
// parameters, ModuleRegistry::describeType() below will skip the overflow
// and assert in debug builds - that's a real constraint to design new
// modules against, not a silent truncation.
inline constexpr int kNumSlots = 8;
inline constexpr int kKnobsPerSlot = 8;

struct ModuleTypeInfo {
    juce::String displayName;
    std::string moduleId; // empty string == "None" (slot is empty/bypassed)
};

class ModuleRegistry {
public:
    // Index 0 is always "None". Order here is what the slot type dropdown
    // shows and what AudioParameterChoice indices mean everywhere else in
    // the pedalboard code - never reorder existing entries (that would
    // silently reinterpret every saved session/preset's slot type
    // indices as different module types); only ever append new ones.
    [[nodiscard]] static const std::vector<ModuleTypeInfo>& types();

    [[nodiscard]] static juce::StringArray typeDisplayNames();

    // Returns nullptr for index 0 (None) or an out-of-range index.
    [[nodiscard]] static std::unique_ptr<tfcore::IAudioModule> create(int typeIndex);

    // Looks up a type's index by tfcore module id, for default-chain setup
    // and (later) preset loading. Returns 0 (None) if not found.
    [[nodiscard]] static int indexForModuleId(const std::string& moduleId);

    // Default-constructs a temporary instance of the given type and calls
    // fn once per parameter, in the same order createLayout() and
    // SlotManager's rebind logic use, together with the knob index (0..
    // kKnobsPerSlot-1) that parameter is bound to. No-op for index 0.
    static void describeType(int typeIndex, const std::function<void(const tfcore::Parameter&, int knobIndex)>& fn);
};

} // namespace toneforge
