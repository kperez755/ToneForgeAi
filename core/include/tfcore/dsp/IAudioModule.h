#pragma once

#include <string_view>

#include "tfcore/dsp/AudioBuffer.h"
#include "tfcore/dsp/ParameterStore.h"
#include "tfcore/preset/ModuleState.h"

namespace tfcore {

// Contract every DSP effect (noise gate, compressor, drive, amp, cab IR, eq,
// chorus, phaser, flanger, delay, reverb, limiter, ...) implements.
//
// process() is the one method that runs on the real-time audio thread. It is
// noexcept and operates only on data already owned by the module and the
// buffer passed in - by construction it cannot allocate, lock, or throw
// without violating the interface contract. Everything else (prepare,
// getState/setState, bypass toggling) is expected to run on the UI or a
// background thread.
class IAudioModule {
public:
    virtual ~IAudioModule() = default;

    // Allocates scratch space, resets internal DSP state for the given
    // sample rate / block size. May allocate. Never called from the audio
    // thread.
    virtual void prepare(const ProcessSpec& spec) = 0;

    // Real-time safe. Processes `buffer` in place. Must not allocate, lock,
    // perform IO, or throw.
    virtual void process(AudioBuffer& buffer) noexcept = 0;

    // Clears internal state (filter memory, delay lines, envelope followers)
    // without releasing/reallocating buffers. Safe to call from the audio
    // thread only if it too avoids allocation (implementations must honor
    // this - see per-module docs).
    virtual void reset() noexcept = 0;

    [[nodiscard]] virtual bool isBypassed() const noexcept = 0;
    virtual void setBypassed(bool shouldBypass) noexcept = 0;

    [[nodiscard]] virtual ParameterStore& parameters() noexcept = 0;
    [[nodiscard]] virtual const ParameterStore& parameters() const noexcept = 0;

    // Off-thread only: used by PresetSerializer.
    [[nodiscard]] virtual ModuleState getState() const = 0;
    virtual void setState(const ModuleState& state) = 0;

    // Stable, versioned identifier used in serialized presets, e.g.
    // "drive.v1". Changing behavior in a way that breaks old presets should
    // bump the version suffix, not silently reinterpret the same id.
    [[nodiscard]] virtual std::string_view moduleId() const noexcept = 0;
};

} // namespace tfcore
