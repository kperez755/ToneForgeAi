#pragma once

#include <array>
#include <cmath>
#include <vector>

#include "tfcore/dsp/AudioModuleBase.h"
#include "tfcore/dsp/Biquad.h"

namespace tfcore {

// Simplified tube-amp model: two cascaded soft-clip gain stages (each
// progressively saturating harder, approximating multiple preamp tube
// stages rather than Drive's single stage) followed by a passive-style
// tone stack (bass/mid/treble) and a presence shelf that sits after the
// tone stack the way a real amp's presence control does (in the power amp
// feedback loop, post tone-stack).
//
// This is a starting point, not a claim of modeling any specific amp - see
// docs/DSP.md for the honest scope note (getting this to sound genuinely
// competitive is real DSP research, tracked as ongoing work, not a solved
// problem as of this module's first version).
//
// Parameters (0..1 normalized, matching what ai-backend/app/core/mapping.py
// already emits for "amp.v1" so the AI generation path needs no changes):
//   gain      overall drive into the two saturation stages
//   bass      low-shelf gain around 100 Hz
//   mid       peaking gain around 500 Hz
//   treble    high-shelf gain around 3 kHz
//   presence  high-shelf gain around 6 kHz, applied post tone-stack
class Amp final : public AudioModuleBase {
public:
    Amp() {
        parameters_.add("gain", "Gain", ParameterRange{0.0f, 1.0f, 0.0f, 1.0f}, 0.5f);
        parameters_.add("bass", "Bass", ParameterRange{0.0f, 1.0f, 0.0f, 1.0f}, 0.5f);
        parameters_.add("mid", "Mid", ParameterRange{0.0f, 1.0f, 0.0f, 1.0f}, 0.5f);
        parameters_.add("treble", "Treble", ParameterRange{0.0f, 1.0f, 0.0f, 1.0f}, 0.5f);
        parameters_.add("presence", "Presence", ParameterRange{0.0f, 1.0f, 0.0f, 1.0f}, 0.5f);
        parameters_.seal();
    }

    void prepare(const ProcessSpec& spec) override {
        sampleRate_ = spec.sampleRate;
        const auto numChannels = static_cast<std::size_t>(spec.numChannels);
        bassFilters_.assign(numChannels, Biquad{});
        midFilters_.assign(numChannels, Biquad{});
        trebleFilters_.assign(numChannels, Biquad{});
        presenceFilters_.assign(numChannels, Biquad{});
        reset();
        updateToneStackCoefficients();
    }

    void reset() noexcept override {
        for (auto& f : bassFilters_) f.reset();
        for (auto& f : midFilters_) f.reset();
        for (auto& f : trebleFilters_) f.reset();
        for (auto& f : presenceFilters_) f.reset();
    }

    void process(AudioBuffer& buffer) noexcept override {
        if (isBypassed()) return;
        if (bassFilters_.empty()) return;

        // Tone stack coefficients only need recomputing when a knob moves;
        // recomputing once per block (not per sample) is cheap enough and
        // avoids a torn read of five independent atomics mid-block.
        updateToneStackCoefficients();

        const float gainParam = parameters_.find("gain")->get();
        const float driveLinear = 1.0f + gainParam * 11.0f; // 0..1 -> roughly 1x..12x pre-gain into the stages

        const int numChannels = buffer.numChannels();
        const int numSamples = buffer.numSamples();

        for (int ch = 0; ch < numChannels && ch < static_cast<int>(bassFilters_.size()); ++ch) {
            float* data = buffer.channel(ch);
            for (int i = 0; i < numSamples; ++i) {
                float x = data[i] * driveLinear;
                x = stageClip(x, 0.7f);
                x = stageClip(x * 1.5f, 0.85f);

                x = bassFilters_[static_cast<std::size_t>(ch)].process(x);
                x = midFilters_[static_cast<std::size_t>(ch)].process(x);
                x = trebleFilters_[static_cast<std::size_t>(ch)].process(x);
                x = presenceFilters_[static_cast<std::size_t>(ch)].process(x);

                data[i] = x;
            }
        }
    }

    [[nodiscard]] std::string_view moduleId() const noexcept override { return "amp.v1"; }

private:
    // Symmetric tanh soft clip - simpler than Drive's asymmetric shaper on
    // purpose, since amp character here comes mostly from cascading two of
    // these plus the tone stack, not from a single elaborate curve.
    [[nodiscard]] static float stageClip(float x, float threshold) noexcept {
        if (x > threshold) return threshold + (1.0f - threshold) * std::tanh((x - threshold) / (1.0f - threshold));
        if (x < -threshold) return -threshold - (1.0f - threshold) * std::tanh((-x - threshold) / (1.0f - threshold));
        return x;
    }

    void updateToneStackCoefficients() noexcept {
        const float bass = parameters_.find("bass")->get();
        const float mid = parameters_.find("mid")->get();
        const float treble = parameters_.find("treble")->get();
        const float presence = parameters_.find("presence")->get();

        // Map 0..1 -> +-12 dB around each band, centered at the 0.5 default
        // (matching the defaults ai-backend's mapping.py assumes).
        const double bassDb = (static_cast<double>(bass) - 0.5) * 24.0;
        const double midDb = (static_cast<double>(mid) - 0.5) * 24.0;
        const double trebleDb = (static_cast<double>(treble) - 0.5) * 24.0;
        const double presenceDb = (static_cast<double>(presence) - 0.5) * 24.0;

        for (auto& f : bassFilters_) f.setLowShelf(sampleRate_, 100.0, bassDb);
        for (auto& f : midFilters_) f.setPeaking(sampleRate_, 500.0, midDb, 0.8);
        for (auto& f : trebleFilters_) f.setHighShelf(sampleRate_, 3000.0, trebleDb);
        for (auto& f : presenceFilters_) f.setHighShelf(sampleRate_, 6000.0, presenceDb);
    }

    double sampleRate_ = 44100.0;
    std::vector<Biquad> bassFilters_;
    std::vector<Biquad> midFilters_;
    std::vector<Biquad> trebleFilters_;
    std::vector<Biquad> presenceFilters_;
};

} // namespace tfcore
