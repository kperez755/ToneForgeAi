#pragma once

#include <cmath>
#include <vector>

#include "tfcore/dsp/AudioModuleBase.h"
#include "tfcore/dsp/Biquad.h"

namespace tfcore {

// High-gain distortion (the "Friedman BE-OD" slot in a pedalboard, as
// opposed to Drive's transparent/dynamic "Blues Driver"-style overdrive
// role - see ModuleRegistry's "Overdrive" vs "Distortion" entries).
// Differentiated from Drive by more than just a bigger gain range:
//   - a SYMMETRIC clip that engages much earlier (threshold 0.35 vs
//     Drive's asymmetric 0.6/0.8) - clips harder for a given input level,
//     which is most of what separates "high gain" from "overdrive"
//     perceptually.
//   - the clip stage is cascaded twice (like Amp's two stages) rather
//     than once, for the extra compression/harmonic density
//     characteristic of high-gain pedals.
//   - a `presence` control (post-clip high-shelf via Biquad, up to +9 dB
//     around 3 kHz) for the "fizz"/brightness real high-gain pedals add,
//     which a single tone-tilt control (Drive's approach) doesn't
//     capture on its own.
//
// Still a single stompbox stage, not an amp simulation - Amp (amp.v1)
// remains the module for "the whole amp," same relationship a real
// distortion pedal has to the amp it's plugged into.
//
// Parameters:
//   gain_db    [0, 60]    pre-clip gain (wider range than Drive's 0-40,
//                         since a high-gain pedal needs headroom to
//                         drive its harder clip into real saturation)
//   tone       [0, 1]     0 = dark, 1 = bright (one-pole tilt, same
//                         technique as Drive's tone control)
//   presence   [0, 1]     post-clip high-shelf boost, 0-9 dB @ 3 kHz
//   level_db   [-24, 12]  post makeup gain
class Distortion final : public AudioModuleBase {
public:
    Distortion() {
        parameters_.add("gain_db", "Gain", ParameterRange{0.0f, 60.0f, 0.0f, 1.0f}, 24.0f, "dB");
        parameters_.add("tone", "Tone", ParameterRange{0.0f, 1.0f, 0.0f, 1.0f}, 0.5f);
        parameters_.add("presence", "Presence", ParameterRange{0.0f, 1.0f, 0.0f, 1.0f}, 0.4f);
        parameters_.add("level_db", "Level", ParameterRange{-24.0f, 12.0f, 0.0f, 1.0f}, -6.0f, "dB");
        parameters_.seal();
    }

    void prepare(const ProcessSpec& spec) override {
        sampleRate_ = spec.sampleRate;
        const auto numChannels = static_cast<std::size_t>(spec.numChannels);
        toneState_.assign(numChannels, 0.0f);
        presenceFilters_.assign(numChannels, Biquad{});
        reset();
        updatePresence();
    }

    void reset() noexcept override {
        for (auto& s : toneState_) s = 0.0f;
        for (auto& f : presenceFilters_) f.reset();
    }

    void process(AudioBuffer& buffer) noexcept override {
        if (isBypassed()) return;
        if (toneState_.empty()) return;

        updatePresence(); // cheap, recomputed once per block like Amp's tone stack

        const float gainLinear = dbToLinear(parameters_.find("gain_db")->get());
        const float tone = parameters_.find("tone")->get();
        const float levelLinear = dbToLinear(parameters_.find("level_db")->get());
        const float toneCoeff = 0.05f + tone * 0.95f;

        const int numChannels = buffer.numChannels();
        const int numSamples = buffer.numSamples();

        for (int ch = 0; ch < numChannels && ch < static_cast<int>(toneState_.size()); ++ch) {
            float* data = buffer.channel(ch);
            float state = toneState_[static_cast<std::size_t>(ch)];
            auto& presenceFilter = presenceFilters_[static_cast<std::size_t>(ch)];

            for (int i = 0; i < numSamples; ++i) {
                float x = data[i] * gainLinear;
                x = hardStageClip(x);
                x = hardStageClip(x * 1.6f); // second cascaded stage - extra density/compression

                state += toneCoeff * (x - state);
                float shaped = state * (1.0f - tone) + x * tone;

                shaped = presenceFilter.process(shaped);

                data[i] = shaped * levelLinear;
            }

            toneState_[static_cast<std::size_t>(ch)] = state;
        }
    }

    [[nodiscard]] std::string_view moduleId() const noexcept override { return "distortion.v1"; }

private:
    [[nodiscard]] static float dbToLinear(float db) noexcept { return std::pow(10.0f, db / 20.0f); }

    // Symmetric, earlier-engaging clip than Drive's asymmetric softClip -
    // see the class comment for why that's the main perceptual
    // differentiator from Drive/Overdrive.
    [[nodiscard]] static float hardStageClip(float x) noexcept {
        constexpr float threshold = 0.35f;
        if (x > threshold) return threshold + (1.0f - threshold) * std::tanh((x - threshold) / (1.0f - threshold));
        if (x < -threshold) return -(threshold + (1.0f - threshold) * std::tanh((-x - threshold) / (1.0f - threshold)));
        return x;
    }

    void updatePresence() noexcept {
        const float presence = parameters_.find("presence")->get();
        const double presenceDb = static_cast<double>(presence) * 9.0; // 0..1 -> 0..9 dB, always a boost (no cut) matching a real presence knob
        for (auto& f : presenceFilters_) f.setHighShelf(sampleRate_, 3000.0, presenceDb);
    }

    double sampleRate_ = 44100.0;
    std::vector<float> toneState_;
    std::vector<Biquad> presenceFilters_;
};

} // namespace tfcore
