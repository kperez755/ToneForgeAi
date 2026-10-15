#pragma once

#include <memory>
#include <vector>

#include "tfcore/dsp/IAudioModule.h"

namespace tfcore {

// An ordered, immutable list of modules. Once constructed it never changes -
// "changing the chain" means building a new ChainState and swapping the
// pointer (see SignalChain below), never mutating an existing ChainState's
// module list from any thread.
//
// Modules are held by shared_ptr, not unique_ptr. This used to be
// unique_ptr (exclusive ownership) until the pedalboard slot system
// (docs/ARCHITECTURE.md "Pedalboard slots") needed a module instance to
// outlive any single ChainState: a slot's module has to stay alive and
// keep its internal state (filter memory, envelope followers, etc.) across
// chain rebuilds that only change OTHER slots, while also being handed to
// a fresh ChainState every time the active module set changes. shared_ptr
// lets the slot-owning layer and the audio-thread-facing ChainState share
// that ownership safely - the module is destroyed only once nothing
// references it anymore, exactly the same released-when-unreferenced
// property ChainState itself already relies on via SignalChain's atomic
// shared_ptr swap below.
class ChainState {
public:
    explicit ChainState(std::vector<std::shared_ptr<IAudioModule>> modules)
        : modules_(std::move(modules)) {}

    void process(AudioBuffer& buffer) noexcept {
        for (auto& module : modules_) {
            module->process(buffer);
        }
    }

    void prepareAll(const ProcessSpec& spec) {
        for (auto& module : modules_) module->prepare(spec);
    }

    [[nodiscard]] std::size_t size() const noexcept { return modules_.size(); }
    [[nodiscard]] IAudioModule* moduleAt(std::size_t index) noexcept { return index < modules_.size() ? modules_[index].get() : nullptr; }
    [[nodiscard]] const IAudioModule* moduleAt(std::size_t index) const noexcept { return index < modules_.size() ? modules_[index].get() : nullptr; }

private:
    std::vector<std::shared_ptr<IAudioModule>> modules_;
};

// Owns the "currently active" ChainState via an atomic shared_ptr swap.
//
// Audio thread: calls process(), which does one atomic load of the current
// ChainState and processes it. Never allocates, never blocks.
//
// UI/background thread: calls setChain() with a brand-new, fully-prepared
// ChainState (built off-thread) whenever the topology changes (module
// added/removed/reordered, preset loaded, AI-generated chain applied). The
// old ChainState is released only once no thread holds a reference to it -
// shared_ptr refcounting handles that for us; we never explicitly delete
// a ChainState the audio thread might still be mid-block on.
//
// Caveat documented in docs/ARCHITECTURE.md #3.2: std::atomic<shared_ptr<T>>
// is only guaranteed lock-free on some standard library implementations.
// CoreThreadSafetyTests checks is_always_lock_free at build time and this is
// re-verified per target platform before shipping (see ROADMAP.md risks).
class SignalChain {
public:
    void setChain(std::shared_ptr<ChainState> newChain) {
        std::atomic_store_explicit(&current_, std::move(newChain), std::memory_order_release);
    }

    [[nodiscard]] std::shared_ptr<ChainState> currentChain() const noexcept {
        return std::atomic_load_explicit(&current_, std::memory_order_acquire);
    }

    // Real-time safe: one atomic load, then delegates to ChainState::process.
    void process(AudioBuffer& buffer) noexcept {
        auto chain = std::atomic_load_explicit(&current_, std::memory_order_acquire);
        if (chain) chain->process(buffer);
    }

private:
    std::shared_ptr<ChainState> current_;
};

} // namespace tfcore
