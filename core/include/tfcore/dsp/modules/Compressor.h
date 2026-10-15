#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "tfcore/dsp/AudioModuleBase.h"

namespace tfcore {

// Standard feedforward dynamics compressor: peak envelope follower -> a
// dB-domain gain computer (hard knee) -> attack/release-smoothed gain
// reduction -> makeup gain. The textbook topology real studio/pedal
// compressors (including the Cali76 this pedal list is standing in for)
// are variations on - no vari-mu/opto/FET-specific modeling here, that's
// real analog-circuit-modeling research and out of scope for this pass,
// same honesty boundary as Amp/Cab.
//
// Parameters:
//   threshold_db [-60, 0]    dB level above which gain reduction begins
//   ratio        [1, 20]     compression ratio (1 = no effect, 20 = near-limiting)
//   attack_ms    [0.1, 100]  time for gain reduction to engage
//   release_ms   [5, 1000]   time for gain reduction to release
//   makeup_db    [0, 24]     linear gain applied after compression
class Compressor final : public AudioModuleBase {
public:
    Compressor() {
        parameters_.add("threshold_db", "Threshold", ParameterRange{-60.0f, 0.0f, 0.0f, 1.0f}, -18.0f, "dB");
        parameters_.add("ratio", "Ratio", ParameterRange{1.0f, 20.0f, 0.0f, 1.0f}, 4.0f, ":1");
        parameters_.add("attack_ms", "Attack", ParameterRange{0.1f, 100.0f, 0.0f, 1.0f}, 5.0f, "ms");
        parameters_.add("release_ms", "Release", ParameterRange{5.0f, 1000.0f, 0.0f, 1.0f}, 80.0f, "ms");
        parameters_.add("makeup_db", "Makeup", ParameterRange{0.0f, 24.0f, 0.0f, 1.0f}, 0.0f, "dB");
        parameters_.seal();
    }

    void prepare(const ProcessSpec& spec) override {
        sampleRate_ = spec.sampleRate;
        const auto numChannels = static_cast<std::size_t>(spec.numChannels);
        envelope_.assign(numChannels, 0.0f);
        gainReductionDb_.assign(numChannels, 0.0f);
        reset();
    }

    void reset() noexcept override {
        for (auto& e : envelope_) e = 0.0f;
        for (auto& g : gainReductionDb_) g = 0.0f;
    }

    void process(AudioBuffer& buffer) noexcept override {
        if (isBypassed()) return;
        if (envelope_.empty()) return;

        const float thresholdDb = parameters_.find("threshold_db")->get();
        const float ratio = parameters_.find("ratio")->get();
        const float attackMs = parameters_.find("attack_ms")->get();
        const float releaseMs = parameters_.find("release_ms")->get();
        const float makeupLinear = dbToLinear(parameters_.find("makeup_db")->get());

        const float attackCoeff = timeToCoeff(attackMs);
        const float releaseCoeff = timeToCoeff(releaseMs);
        const float slope = 1.0f - 1.0f / ratio; // fraction of over-threshold dB to remove

        const int numChannels = buffer.numChannels();
        const int numSamples = buffer.numSamples();

        for (int ch = 0; ch < numChannels && ch < static_cast<int>(envelope_.size()); ++ch) {
            float* data = buffer.channel(ch);
            float envelope = envelope_[static_cast<std::size_t>(ch)];
            float gr = gainReductionDb_[static_cast<std::size_t>(ch)];

            for (int i = 0; i < numSamples; ++i) {
                const float rectified = std::fabs(data[i]);
                envelope += (rectified > envelope ? attackCoeff : releaseCoeff) * (rectified - envelope);

                const float levelDb = linearToDb(std::max(envelope, 1.0e-6f));
                const float overDb = levelDb - thresholdDb;
                const float targetGr = overDb > 0.0f ? overDb * slope : 0.0f;

                // Smooth the gain-reduction amount itself (not the raw
                // envelope) with attack/release - the standard way to
                // avoid the gain jumping instantly at the threshold
                // crossing, same "smooth the applied gain, not just the
                // detector" idea NoiseGate already uses.
                gr += (targetGr > gr ? attackCoeff : releaseCoeff) * (targetGr - gr);

                data[i] *= dbToLinear(-gr) * makeupLinear;
            }

            envelope_[static_cast<std::size_t>(ch)] = envelope;
            gainReductionDb_[static_cast<std::size_t>(ch)] = gr;
        }
    }

    [[nodiscard]] std::string_view moduleId() const noexcept override { return "compressor.v1"; }

private:
    [[nodiscard]] static float dbToLinear(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
    [[nodiscard]] static float linearToDb(float linear) noexcept { return 20.0f * std::log10(linear); }

    [[nodiscard]] float timeToCoeff(float milliseconds) const noexcept {
        const float timeSeconds = milliseconds / 1000.0f;
        if (timeSeconds <= 0.0f) return 1.0f;
        return 1.0f - std::exp(-1.0f / (timeSeconds * static_cast<float>(sampleRate_)));
    }

    double sampleRate_ = 44100.0;
    std::vector<float> envelope_;
    std::vector<float> gainReductionDb_;
};

} // namespace tfcore
