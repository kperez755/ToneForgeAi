#pragma once

#include <cmath>
#include <vector>

#include "tfcore/dsp/AudioModuleBase.h"

namespace tfcore {

// Analog-style overdrive: pre-gain -> asymmetric soft-clip waveshaper ->
// single-pole tone tilt filter -> makeup/level. This is intentionally a
// simple, well-understood topology for the MVP; it is the module most
// likely to be replaced/extended (multiple clipping models, oversampling
// for alias reduction) in M2 without touching the interface.
//
// Parameters:
//   gain_db   [0, 40]     pre-clip gain
//   tone      [0, 1]      0 = dark, 1 = bright (tilts a one-pole filter)
//   level_db  [-24, 12]   post makeup gain
class Drive final : public AudioModuleBase {
public:
    Drive() {
        parameters_.add("gain_db", "Gain", ParameterRange{0.0f, 40.0f, 0.0f, 1.0f}, 12.0f, "dB");
        parameters_.add("tone", "Tone", ParameterRange{0.0f, 1.0f, 0.0f, 1.0f}, 0.5f, "");
        parameters_.add("level_db", "Level", ParameterRange{-24.0f, 12.0f, 0.0f, 1.0f}, 0.0f, "dB");
        parameters_.seal();
    }

    void prepare(const ProcessSpec& spec) override {
        sampleRate_ = spec.sampleRate;
        toneState_.assign(static_cast<std::size_t>(spec.numChannels), 0.0f);
        reset();
    }

    void reset() noexcept override {
        for (auto& s : toneState_) s = 0.0f;
    }

    void process(AudioBuffer& buffer) noexcept override {
        if (isBypassed()) return;
        if (toneState_.empty()) return;

        const float gainLinear = dbToLinear(parameters_.find("gain_db")->get());
        const float tone = parameters_.find("tone")->get();
        const float levelLinear = dbToLinear(parameters_.find("level_db")->get());

        // Map tone [0,1] to a one-pole low-pass coefficient: low tone value =
        // heavier smoothing = darker; high tone value = coefficient near 1 =
        // effectively bypasses the smoothing = brighter.
        const float toneCoeff = 0.05f + tone * 0.95f;

        const int numChannels = buffer.numChannels();
        const int numSamples = buffer.numSamples();

        for (int ch = 0; ch < numChannels && ch < static_cast<int>(toneState_.size()); ++ch) {
            float* data = buffer.channel(ch);
            float state = toneState_[static_cast<std::size_t>(ch)];

            for (int i = 0; i < numSamples; ++i) {
                float x = data[i] * gainLinear;
                x = softClip(x);

                // One-pole tilt: blend the clipped signal with a smoothed
                // (darker) version of itself.
                state += toneCoeff * (x - state);
                const float shaped = state * (1.0f - tone) + x * tone;

                data[i] = shaped * levelLinear;
            }

            toneState_[static_cast<std::size_t>(ch)] = state;
        }
    }

    [[nodiscard]] std::string_view moduleId() const noexcept override { return "drive.v1"; }

private:
    [[nodiscard]] static float dbToLinear(float db) noexcept;

    // Asymmetric soft clipper: cheap, alias-prone at high gain (documented
    // limitation - M2 adds oversampling), but gives a musically useful
    // "tube-ish" asymmetric harmonic profile for the MVP.
    [[nodiscard]] static float softClip(float x) noexcept {
        constexpr float positiveThreshold = 0.6f;
        constexpr float negativeThreshold = 0.8f;
        if (x > positiveThreshold) {
            return positiveThreshold + (1.0f - positiveThreshold) * std::tanh((x - positiveThreshold) / (1.0f - positiveThreshold));
        }
        if (x < -negativeThreshold) {
            return -negativeThreshold + (1.0f - negativeThreshold) * std::tanh((x + negativeThreshold) / (1.0f - negativeThreshold));
        }
        return x;
    }

    double sampleRate_ = 44100.0;
    std::vector<float> toneState_;
};

} // namespace tfcore
