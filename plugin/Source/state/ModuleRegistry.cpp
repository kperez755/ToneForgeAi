#include "ModuleRegistry.h"

#include "tfcore/dsp/modules/Amp.h"
#include "tfcore/dsp/modules/Cab.h"
#include "tfcore/dsp/modules/Compressor.h"
#include "tfcore/dsp/modules/Delay.h"
#include "tfcore/dsp/modules/Distortion.h"
#include "tfcore/dsp/modules/Drive.h"
#include "tfcore/dsp/modules/Eq.h"
#include "tfcore/dsp/modules/Modulation.h"
#include "tfcore/dsp/modules/NoiseGate.h"
#include "tfcore/dsp/modules/Reverb.h"

namespace toneforge {

const std::vector<ModuleTypeInfo>& ModuleRegistry::types() {
    // Limiter is the one module from docs/DSP.md's original list still
    // missing - see docs/ROADMAP.md for why the rest of the list was
    // deliberately scoped down to "the pedals that cover most genres"
    // rather than building out every possible pedal type. A preset or
    // session referencing a type index beyond what this build knows about
    // simply can't happen yet since there's nothing to reference it with;
    // once presets can travel between builds with different module sets,
    // an unknown index should fall back to None rather than crash - noted
    // here for whoever wires that up.
    //
    // Existing entries are never reordered/removed once shipped - see the
    // warning on ModuleRegistry::types() in the header - only ever
    // appended, which is why later additions land at the end instead of
    // in signal-chain order. "Drive" is displayed as "Overdrive" (its
    // moduleId stays "drive.v1" - only the display label changed) to
    // match the user's pedal list once "Distortion" existed alongside it
    // as a genuinely different pedal, not just a bigger-gain version of
    // the same one - see Distortion.h for how the two differ.
    static const std::vector<ModuleTypeInfo> kTypes = {
        {"None", ""},
        {"Noise Gate", "noise_gate.v1"},
        {"Overdrive", "drive.v1"},
        {"Amp", "amp.v1"},
        {"Cab", "cab.v1"},
        {"Eq", "eq.v1"},
        {"Reverb", "reverb.v1"},
        {"Compressor", "compressor.v1"},
        {"Distortion", "distortion.v1"},
        {"Delay", "delay.v1"},
        {"Modulation", "modulation.v1"},
    };
    return kTypes;
}

juce::StringArray ModuleRegistry::typeDisplayNames() {
    juce::StringArray names;
    for (const auto& t : types()) names.add(t.displayName);
    return names;
}

std::unique_ptr<tfcore::IAudioModule> ModuleRegistry::create(int typeIndex) {
    const auto& all = types();
    if (typeIndex <= 0 || typeIndex >= static_cast<int>(all.size())) return nullptr; // 0 = None

    const std::string& id = all[static_cast<std::size_t>(typeIndex)].moduleId;
    if (id == "noise_gate.v1") return std::make_unique<tfcore::NoiseGate>();
    if (id == "drive.v1") return std::make_unique<tfcore::Drive>();
    if (id == "amp.v1") return std::make_unique<tfcore::Amp>();
    if (id == "cab.v1") return std::make_unique<tfcore::Cab>();
    if (id == "eq.v1") return std::make_unique<tfcore::Eq>();
    if (id == "reverb.v1") return std::make_unique<tfcore::Reverb>();
    if (id == "compressor.v1") return std::make_unique<tfcore::Compressor>();
    if (id == "distortion.v1") return std::make_unique<tfcore::Distortion>();
    if (id == "delay.v1") return std::make_unique<tfcore::Delay>();
    if (id == "modulation.v1") return std::make_unique<tfcore::Modulation>();
    return nullptr;
}

int ModuleRegistry::indexForModuleId(const std::string& moduleId) {
    const auto& all = types();
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].moduleId == moduleId) return static_cast<int>(i);
    }
    return 0; // None
}

void ModuleRegistry::describeType(int typeIndex, const std::function<void(const tfcore::Parameter&, int knobIndex)>& fn) {
    auto instance = create(typeIndex);
    if (!instance) return;

    int knobIndex = 0;
    instance->parameters().forEach([&](const tfcore::Parameter& p) {
        if (knobIndex >= kKnobsPerSlot) {
            jassertfalse; // a module has more parameters than the fixed per-slot knob budget - see ModuleRegistry.h
            return;
        }
        fn(p, knobIndex);
        ++knobIndex;
    });
}

} // namespace toneforge
