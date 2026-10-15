#pragma once

#include <vector>

#include "tfcore/dsp/AudioModuleBase.h"

namespace tfcore {

// Classic threshold/attack/hold/release noise gate with a per-channel
// envelope follower. First module in the signal chain in the default
// preset topology (Input -> NoiseGate -> Compressor -> Drive -> Amp -> ...).
//
// Parameters:
//   threshold_db  [-80, 0]    dB level below which the gate closes
//   attack_ms     [0.1, 50]   time to open once signal crosses threshold
//   hold_ms       [0, 500]    time to stay open after signal drops below threshold
//   release_ms    [1, 1000]   time to fade closed after hold expires
class NoiseGate final : public AudioModuleBase {
public:
    NoiseGate() {
        parameters_.add("threshold_db", "Threshold", ParameterRange{-80.0f, 0.0f, 0.0f, 1.0f}, -50.0f, "dB");
        parameters_.add("attack_ms", "Attack", ParameterRange{0.1f, 50.0f, 0.0f, 1.0f}, 2.0f, "ms");
        parameters_.add("hold_ms", "Hold", ParameterRange{0.0f, 500.0f, 0.0f, 1.0f}, 50.0f, "ms");
        parameters_.add("release_ms", "Release", ParameterRange{1.0f, 1000.0f, 0.0f, 1.0f}, 100.0f, "ms");
        parameters_.seal();
    }

    void prepare(const ProcessSpec& spec) override {
        sampleRate_ = spec.sampleRate;
        envelope_.assign(static_cast<std::size_t>(spec.numChannels), 0.0f);
        gain_.assign(static_cast<std::size_t>(spec.numChannels), 0.0f);
        holdCounter_.assign(static_cast<std::size_t>(spec.numChannels), 0);
        reset();
    }

    void reset() noexcept override {
        for (auto& e : envelope_) e = 0.0f;
        for (auto& g : gain_) g = 0.0f;
        for (auto& h : holdCounter_) h = 0;
    }

    void process(AudioBuffer& buffer) noexcept override {
        if (isBypassed()) return;
        if (envelope_.empty()) return; // prepare() was never called - nothing to do safely

        const float thresholdDb = parameters_.find("threshold_db")->get();
        const float attackMs = parameters_.find("attack_ms")->get();
        const float holdMs = parameters_.find("hold_ms")->get();
        const float releaseMs = parameters_.find("release_ms")->get();

        const float thresholdLinear = dbToLinear(thresholdDb);
        const float attackCoeff = timeToCoeff(attackMs);
        const float releaseCoeff = timeToCoeff(releaseMs);
        const int holdSamples = static_cast<int>((holdMs / 1000.0) * sampleRate_);

        const int numChannels = buffer.numChannels();
        const int numSamples = buffer.numSamples();

        for (int ch = 0; ch < numChannels && ch < static_cast<int>(envelope_.size()); ++ch) {
            float* data = buffer.channel(ch);
            float envelope = envelope_[static_cast<std::size_t>(ch)];
            float gain = gain_[static_cast<std::size_t>(ch)];
            int holdCounter = holdCounter_[static_cast<std::size_t>(ch)];

            for (int i = 0; i < numSamples; ++i) {
                const float rectified = data[i] < 0.0f ? -data[i] : data[i];
                // One-pole envelope follower, always tracks with the release
                // coefficient except when the input is louder (fast attack).
                envelope = rectified > envelope ? rectified : envelope + releaseCoeff * (rectified - envelope);

                const bool open = envelope >= thresholdLinear;
                if (open) {
                    holdCounter = holdSamples;
                } else if (holdCounter > 0) {
                    --holdCounter;
                }

                const float targetGain = (open || holdCounter > 0) ? 1.0f : 0.0f;
                const float coeff = targetGain > gain ? attackCoeff : releaseCoeff;
                gain += coeff * (targetGain - gain);

                data[i] *= gain;
            }

            envelope_[static_cast<std::size_t>(ch)] = envelope;
            gain_[static_cast<std::size_t>(ch)] = gain;
            holdCounter_[static_cast<std::size_t>(ch)] = holdCounter;
        }
    }

    [[nodiscard]] std::string_view moduleId() const noexcept override { return "noise_gate.v1"; }

private:
    [[nodiscard]] static float dbToLinear(float db) noexcept;
    [[nodiscard]] float timeToCoeff(float milliseconds) const noexcept;

    double sampleRate_ = 44100.0;
    std::vector<float> envelope_;
    std::vector<float> gain_;
    std::vector<int> holdCounter_;
};

} // namespace tfcore
