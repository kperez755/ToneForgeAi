#include "ParameterBridge.h"

#include <array>

namespace toneforge {

namespace {

int defaultTypeIndexForSlot(int slotIndex) {
    // Preserves today's fixed chain (NoiseGate -> Drive -> Amp -> Cab ->
    // Eq) as the out-of-box sound for a fresh plugin instance; slots 5-7
    // default to None. See the M2/M3 milestone log entries in
    // docs/ROADMAP.md for why an amp-shaped default chain matters (a raw
    // passthrough was the very first real gap this project hit).
    switch (slotIndex) {
        case 0: return ModuleRegistry::indexForModuleId("noise_gate.v1");
        case 1: return ModuleRegistry::indexForModuleId("drive.v1");
        case 2: return ModuleRegistry::indexForModuleId("amp.v1");
        case 3: return ModuleRegistry::indexForModuleId("cab.v1");
        case 4: return ModuleRegistry::indexForModuleId("eq.v1");
        default: return 0; // None
    }
}

} // namespace

juce::String SlotManager::slotTypeParamId(int slotIndex) {
    return "slot" + juce::String(slotIndex) + "_type";
}

juce::String SlotManager::slotBypassParamId(int slotIndex) {
    return "slot" + juce::String(slotIndex) + "_bypass";
}

juce::String SlotManager::slotKnobParamId(int slotIndex, int knobIndex) {
    return "slot" + juce::String(slotIndex) + "_knob" + juce::String(knobIndex);
}

juce::AudioProcessorValueTreeState::ParameterLayout SlotManager::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto typeNames = ModuleRegistry::typeDisplayNames();

    for (int i = 0; i < kNumSlots; ++i) {
        const juce::String slotLabel = "Slot " + juce::String(i + 1);
        const int defaultType = defaultTypeIndexForSlot(i);

        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(slotTypeParamId(i), 1), slotLabel + " Type", typeNames, defaultType));

        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID(slotBypassParamId(i), 1), slotLabel + " Bypass", false));

        // Seed each knob with the default slot's own default value
        // (normalized into [0,1]) so a fresh plugin instance sounds like
        // today's fixed chain out of the box, not like every knob is at
        // its raw midpoint. Unused knobs (module has fewer than
        // kKnobsPerSlot parameters, or the slot defaults to None) stay at
        // 0.5 - there's nothing meaningful to seed them with.
        std::array<float, kKnobsPerSlot> knobDefaults;
        knobDefaults.fill(0.5f);
        ModuleRegistry::describeType(defaultType, [&](const tfcore::Parameter& p, int knobIndex) {
            knobDefaults[static_cast<std::size_t>(knobIndex)] = p.range().normalize(p.defaultValue());
        });

        for (int k = 0; k < kKnobsPerSlot; ++k) {
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID(slotKnobParamId(i, k), 1),
                slotLabel + " Knob " + juce::String(k + 1),
                juce::NormalisableRange<float>(0.0f, 1.0f),
                knobDefaults[static_cast<std::size_t>(k)]));
        }
    }

    return layout;
}

SlotManager::SlotManager(juce::AudioProcessorValueTreeState& apvts, tfcore::SignalChain& signalChain)
    : apvts_(apvts), signalChain_(signalChain) {}

SlotManager::~SlotManager() {
    stopTimer();
    if (started_) {
        for (int i = 0; i < kNumSlots; ++i) {
            apvts_.removeParameterListener(typeParamIds_[static_cast<std::size_t>(i)], this);
        }
    }
}

void SlotManager::start() {
    if (started_) return;

    for (int i = 0; i < kNumSlots; ++i) {
        typeParamIds_[static_cast<std::size_t>(i)] = slotTypeParamId(i);
        auto* param = apvts_.getParameter(typeParamIds_[static_cast<std::size_t>(i)]);
        slots_[static_cast<std::size_t>(i)].typeParam = dynamic_cast<juce::AudioParameterChoice*>(param);
        jassert(slots_[static_cast<std::size_t>(i)].typeParam != nullptr); // layout/lookup mismatch - createLayout() and slotTypeParamId() disagree

        apvts_.addParameterListener(typeParamIds_[static_cast<std::size_t>(i)], this);
    }

    started_ = true;

    // Force an immediate, synchronous build so the default chain exists
    // before the first processBlock() - the host can call prepareToPlay()/
    // processBlock() very soon after construction, potentially before the
    // 20 Hz timer's first tick fires.
    timerCallback();

    startTimerHz(20);
}

void SlotManager::prepare(const tfcore::ProcessSpec& spec) {
    processSpec_ = spec;
    for (auto& slot : slots_) {
        if (slot.module) slot.module->prepare(processSpec_);
    }
    publishChain();
}

void SlotManager::parameterChanged(const juce::String& parameterID, float) {
    // May be called on the AUDIO thread (host automation of an
    // AudioParameterChoice can deliver its listener callback from inside
    // processBlock() - this is documented JUCE behavior, not an edge
    // case). This body is intentionally the bare minimum: an
    // allocation-free, lock-free array scan (kNumSlots == 8, bounded) and
    // a relaxed atomic store. No module construction, no vector mutation,
    // no juce::String allocation - all of that happens later, off this
    // thread, in timerCallback().
    for (int i = 0; i < kNumSlots; ++i) {
        if (typeParamIds_[static_cast<std::size_t>(i)] == parameterID) {
            slots_[static_cast<std::size_t>(i)].dirty.store(true, std::memory_order_relaxed);
            return;
        }
    }
}

void SlotManager::timerCallback() {
    bool anyChanged = false;
    for (int i = 0; i < kNumSlots; ++i) {
        if (slots_[static_cast<std::size_t>(i)].dirty.exchange(false, std::memory_order_relaxed)) {
            rebuildSlot(i);
            anyChanged = true;
        }
    }
    if (anyChanged) publishChain();
}

void SlotManager::rebuildSlot(int slotIndex) {
    // Message thread only - constructs a new module instance (allocates)
    // and calls prepare() on it (may allocate). Only touches
    // slots_[slotIndex], which the audio thread never reads directly (it
    // only ever sees activeBindings_, published wholesale by
    // publishChain() below) - so this is safe even while the audio thread
    // is concurrently calling syncToCore().
    Slot& slot = slots_[static_cast<std::size_t>(slotIndex)];
    if (!slot.typeParam) return;

    const int newTypeIndex = slot.typeParam->getIndex();
    if (newTypeIndex == slot.liveTypeIndex) return; // spurious dirty flag (e.g. re-set to the same value) - nothing to do

    slot.liveTypeIndex = newTypeIndex;
    slot.module = ModuleRegistry::create(newTypeIndex); // nullptr for None - slot becomes empty
    if (slot.module) {
        slot.module->prepare(processSpec_);
    }
}

void SlotManager::publishChain() {
    // Message thread only. Builds two brand-new immutable structures from
    // the current slots_ state - the knob/bypass binding snapshot the
    // audio thread reads in syncToCore(), and the tfcore::ChainState the
    // audio thread processes - and publishes both via atomic pointer
    // swap. Never mutates a structure either thread might already be
    // reading; the old snapshot/chain simply stay valid (shared_ptr
    // refcounted) until the audio thread is done with them and drops its
    // reference.
    auto bindings = std::make_shared<std::vector<SlotBinding>>();
    std::vector<std::shared_ptr<tfcore::IAudioModule>> chainModules;
    bindings->reserve(kNumSlots);
    chainModules.reserve(kNumSlots);

    for (int i = 0; i < kNumSlots; ++i) {
        Slot& slot = slots_[static_cast<std::size_t>(i)];
        if (!slot.module) continue; // empty slot (None) - contributes nothing to the chain

        SlotBinding binding;
        binding.module = slot.module;
        binding.bypassRaw = apvts_.getRawParameterValue(slotBypassParamId(i));

        int knobIndex = 0;
        slot.module->parameters().forEach([&](tfcore::Parameter& p) {
            if (knobIndex >= kKnobsPerSlot) {
                jassertfalse; // module has more parameters than the fixed per-slot knob budget - see ModuleRegistry.h
                return;
            }
            if (auto* raw = apvts_.getRawParameterValue(slotKnobParamId(i, knobIndex))) {
                binding.knobLinks.push_back(KnobLink{raw, &p, p.range()});
            } else {
                jassertfalse; // layout/bind mismatch
            }
            ++knobIndex;
        });

        chainModules.push_back(slot.module);
        bindings->push_back(std::move(binding));
    }

    std::atomic_store_explicit(&activeBindings_,
                                std::shared_ptr<const std::vector<SlotBinding>>(std::move(bindings)),
                                std::memory_order_release);
    signalChain_.setChain(std::make_shared<tfcore::ChainState>(std::move(chainModules)));
}

void SlotManager::syncToCore() noexcept {
    // RT-safe: one atomic load of the whole snapshot, then plain
    // (relaxed-atomic-backed) reads/writes per bound knob/bypass. The
    // snapshot itself is never mutated once published - see
    // publishChain() - so iterating it here while the message thread may
    // be building a *different*, not-yet-published snapshot concurrently
    // is safe.
    auto bindings = std::atomic_load_explicit(&activeBindings_, std::memory_order_acquire);
    if (!bindings) return;

    for (const auto& binding : *bindings) {
        if (!binding.module) continue;
        if (binding.bypassRaw) {
            binding.module->setBypassed(binding.bypassRaw->load(std::memory_order_relaxed) > 0.5f);
        }
        for (const auto& link : binding.knobLinks) {
            link.coreParam->set(link.range.denormalize(link.juceValue->load(std::memory_order_relaxed)));
        }
    }
}

int SlotManager::currentTypeIndex(int slotIndex) const noexcept {
    if (slotIndex < 0 || slotIndex >= kNumSlots) return 0;
    const auto* param = slots_[static_cast<std::size_t>(slotIndex)].typeParam;
    return param ? param->getIndex() : 0;
}

} // namespace toneforge
