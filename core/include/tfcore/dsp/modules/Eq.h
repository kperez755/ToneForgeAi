#pragma once

#include <vector>

#include "tfcore/dsp/AudioModuleBase.h"
#include "tfcore/dsp/Biquad.h"

namespace tfcore {

// General-purpose 3-band EQ (low shelf / mid peak / high shelf), placed
// after Amp+Cab in the default chain for final tonal trim - the same role
// a "sculpt" EQ pedal plays after an amp in a real rig.
//
// Parameters map directly to dB gain (unlike Amp's 0..1 normalized tone
// knobs) since ai-backend/app/core/mapping.py already emits these as
// low_gain_db/mid_gain_db/high_gain_db in a +-N dB range.
class Eq final : public AudioModuleBase {
public:
    Eq() {
        parameters_.add("low_gain_db", "Low", ParameterRange{-12.0f, 12.0f, 0.0f, 1.0f}, 0.0f, "dB");
        parameters_.add("mid_gain_db", "Mid", ParameterRange{-12.0f, 12.0f, 0.0f, 1.0f}, 0.0f, "dB");
        parameters_.add("high_gain_db", "High", ParameterRange{-12.0f, 12.0f, 0.0f, 1.0f}, 0.0f, "dB");
        parameters_.seal();
    }

    void prepare(const ProcessSpec& spec) override {
        sampleRate_ = spec.sampleRate;
        const auto numChannels = static_cast<std::size_t>(spec.numChannels);
        lowShelf_.assign(numChannels, Biquad{});
        midPeak_.assign(numChannels, Biquad{});
        highShelf_.assign(numChannels, Biquad{});
        reset();
        updateCoefficients();
    }

    void reset() noexcept override {
        for (auto& f : lowShelf_) f.reset();
        for (auto& f : midPeak_) f.reset();
        for (auto& f : highShelf_) f.reset();
    }

    void process(AudioBuffer& buffer) noexcept override {
        if (isBypassed()) return;
        if (lowShelf_.empty()) return;

        updateCoefficients();

        const int numChannels = buffer.numChannels();
        const int numSamples = buffer.numSamples();

        for (int ch = 0; ch < numChannels && ch < static_cast<int>(lowShelf_.size()); ++ch) {
            float* data = buffer.channel(ch);
            for (int i = 0; i < numSamples; ++i) {
                float x = data[i];
                x = lowShelf_[static_cast<std::size_t>(ch)].process(x);
                x = midPeak_[static_cast<std::size_t>(ch)].process(x);
                x = highShelf_[static_cast<std::size_t>(ch)].process(x);
                data[i] = x;
            }
        }
    }

    [[nodiscard]] std::string_view moduleId() const noexcept override { return "eq.v1"; }

private:
    void updateCoefficients() noexcept {
        const float lowDb = parameters_.find("low_gain_db")->get();
        const float midDb = parameters_.find("mid_gain_db")->get();
        const float highDb = parameters_.find("high_gain_db")->get();

        for (auto& f : lowShelf_) f.setLowShelf(sampleRate_, 150.0, lowDb);
        for (auto& f : midPeak_) f.setPeaking(sampleRate_, 800.0, midDb, 0.9);
        for (auto& f : highShelf_) f.setHighShelf(sampleRate_, 4000.0, highDb);
    }

    double sampleRate_ = 44100.0;
    std::vector<Biquad> lowShelf_;
    std::vector<Biquad> midPeak_;
    std::vector<Biquad> highShelf_;
};

} // namespace tfcore
