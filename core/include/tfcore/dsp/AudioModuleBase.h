#pragma once

#include <atomic>

#include "tfcore/dsp/IAudioModule.h"

namespace tfcore {

// Shared plumbing every concrete module needs: bypass flag, parameter store
// access, and a default getState()/setState() implementation driven purely
// off whatever parameters the subclass registered. Subclasses only need to
// implement prepare(), process(), reset(), moduleId(), and register their
// parameters in the constructor - this is what keeps individual modules
// small instead of every one of them re-implementing serialization glue.
class AudioModuleBase : public IAudioModule {
public:
    [[nodiscard]] bool isBypassed() const noexcept override { return bypassed_.load(std::memory_order_relaxed); }

    void setBypassed(bool shouldBypass) noexcept override { bypassed_.store(shouldBypass, std::memory_order_relaxed); }

    [[nodiscard]] ParameterStore& parameters() noexcept override { return parameters_; }
    [[nodiscard]] const ParameterStore& parameters() const noexcept override { return parameters_; }

    [[nodiscard]] ModuleState getState() const override {
        ModuleState state;
        state.moduleId = std::string(moduleId());
        state.bypassed = isBypassed();
        parameters_.forEach([&state](const Parameter& p) { state.parameters[p.id()] = p.get(); });
        return state;
    }

    void setState(const ModuleState& state) override {
        setBypassed(state.bypassed);
        for (const auto& [id, value] : state.parameters) {
            if (auto* param = parameters_.find(id)) {
                param->set(value);
            }
            // Unknown parameter ids are ignored rather than throwing: this is
            // what lets older presets load into a newer build that may have
            // added parameters, and vice versa within reason.
        }
    }

protected:
    ParameterStore parameters_;

private:
    std::atomic<bool> bypassed_{false};
};

} // namespace tfcore
