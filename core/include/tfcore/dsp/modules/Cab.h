#pragma once

#include <vector>

#include "tfcore/dsp/AudioModuleBase.h"
#include "tfcore/dsp/Biquad.h"

namespace tfcore {

// Cabinet response APPROXIMATION - not impulse-response convolution.
//
// A real guitar cabinet sim needs convolution against a measured/licensed
// IR (docs/DSP.md flags this as real future work: partitioned/block
// convolution engine + an actual IR library, neither of which exist yet).
// What this module does instead, honestly, is apply a fixed filter chain
// shaped like a typical closed-back 4x12's frequency response: a low
// rolloff below ~90 Hz (cabs don't reproduce sub-bass), a broad presence
// bump around 2.5-3 kHz (where guitar speakers tend to beam/peak), and a
// steep rolloff above ~5.5 kHz (speakers don't reproduce true highs the
// way a full-range monitor does). It will sound "cab-like" in that it
// removes the fizzy, full-range harshness a raw Amp signal has - it will
// not sound like any specific real cabinet, and should never be marketed
// as such. `resourceRef` is carried through (e.g.
// "cab_ir.generic_4x12_v1") for forward compatibility with a future real
// IR-selection mechanism, but this version ignores its value - there is
// only one profile.
class Cab final : public AudioModuleBase {
public:
    Cab() {
        parameters_.add("mix", "Mix", ParameterRange{0.0f, 1.0f, 0.0f, 1.0f}, 1.0f);
        parameters_.seal();
    }

    void prepare(const ProcessSpec& spec) override {
        sampleRate_ = spec.sampleRate;
        const auto numChannels = static_cast<std::size_t>(spec.numChannels);
        lowCut_.assign(numChannels, Biquad{});
        presenceBump_.assign(numChannels, Biquad{});
        highCut_.assign(numChannels, Biquad{});
        reset();

        for (auto& f : lowCut_) f.setHighPass(sampleRate_, 90.0, 0.707);
        for (auto& f : presenceBump_) f.setPeaking(sampleRate_, 2800.0, 5.0, 1.1);
        for (auto& f : highCut_) f.setLowPass(sampleRate_, 5500.0, 0.707);
    }

    void reset() noexcept override {
        for (auto& f : lowCut_) f.reset();
        for (auto& f : presenceBump_) f.reset();
        for (auto& f : highCut_) f.reset();
    }

    void process(AudioBuffer& buffer) noexcept override {
        if (isBypassed()) return;
        if (lowCut_.empty()) return;

        const float mix = parameters_.find("mix")->get();

        const int numChannels = buffer.numChannels();
        const int numSamples = buffer.numSamples();

        for (int ch = 0; ch < numChannels && ch < static_cast<int>(lowCut_.size()); ++ch) {
            float* data = buffer.channel(ch);
            for (int i = 0; i < numSamples; ++i) {
                const float dry = data[i];
                float wet = lowCut_[static_cast<std::size_t>(ch)].process(dry);
                wet = presenceBump_[static_cast<std::size_t>(ch)].process(wet);
                wet = highCut_[static_cast<std::size_t>(ch)].process(wet);
                data[i] = dry * (1.0f - mix) + wet * mix;
            }
        }
    }

    [[nodiscard]] std::string_view moduleId() const noexcept override { return "cab.v1"; }

private:
    double sampleRate_ = 44100.0;
    std::vector<Biquad> lowCut_;
    std::vector<Biquad> presenceBump_;
    std::vector<Biquad> highCut_;
};

} // namespace tfcore
