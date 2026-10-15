#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "tfcore/dsp/AudioModuleBase.h"
#include "tfcore/dsp/Biquad.h"
#include "tfcore/dsp/ReverbNetwork.h" // SimpleDelayLine

namespace tfcore {

// Feedback delay: dry signal is mixed with a delayed, regenerating copy
// of itself, darkened a little more on each repeat by a one-pole lowpass
// living in the feedback path (the classic "tape delay" characteristic -
// each successive echo is a bit duller than the last, not just quieter).
// Reuses `SimpleDelayLine` from `ReverbNetwork.h` (originally built for
// Reverb's predelay stage) rather than a second bespoke circular buffer.
//
// Parameters:
//   time_ms   [1, 2000]  delay time
//   feedback  [0, 0.95]  how much of the (tone-filtered) delayed signal
//                        feeds back in - capped below 1.0 so the loop is
//                        always stable and can't run away
//   tone      [0, 1]     one-pole lowpass cutoff in the feedback path
//                        only (the first echo is full-bandwidth; only
//                        the *regenerating* copies get progressively
//                        darker) - 0 = dark (~800 Hz), 1 = bright (10 kHz)
//   mix       [0, 1]     dry/wet
class Delay final : public AudioModuleBase {
public:
    Delay() {
        parameters_.add("time_ms", "Time", ParameterRange{1.0f, 2000.0f, 0.0f, 1.0f}, 350.0f, "ms");
        parameters_.add("feedback", "Feedback", ParameterRange{0.0f, 0.95f, 0.0f, 1.0f}, 0.35f);
        parameters_.add("tone", "Tone", ParameterRange{0.0f, 1.0f, 0.0f, 1.0f}, 0.6f);
        parameters_.add("mix", "Mix", ParameterRange{0.0f, 1.0f, 0.0f, 1.0f}, 0.35f);
        parameters_.seal();
    }

    void prepare(const ProcessSpec& spec) override {
        sampleRate_ = spec.sampleRate;
        const auto numChannels = static_cast<std::size_t>(spec.numChannels);
        const int maxDelaySamples = static_cast<int>(std::ceil(2000.0 * sampleRate_ / 1000.0)) + 16;

        delayLines_.assign(numChannels, SimpleDelayLine{});
        toneFilters_.assign(numChannels, Biquad{});
        feedbackState_.assign(numChannels, 0.0f);

        for (auto& d : delayLines_) d.prepare(maxDelaySamples);

        reset();
        updateCoefficients();
    }

    void reset() noexcept override {
        for (auto& d : delayLines_) d.reset();
        for (auto& f : toneFilters_) f.reset();
        for (auto& s : feedbackState_) s = 0.0f;
    }

    void process(AudioBuffer& buffer) noexcept override {
        if (isBypassed()) return;
        if (delayLines_.empty()) return;

        updateCoefficients();

        const float feedbackGain = parameters_.find("feedback")->get();
        const float mix = parameters_.find("mix")->get();

        const int numChannels = buffer.numChannels();
        const int numSamples = buffer.numSamples();

        for (int ch = 0; ch < numChannels && ch < static_cast<int>(delayLines_.size()); ++ch) {
            float* data = buffer.channel(ch);
            auto& delayLine = delayLines_[static_cast<std::size_t>(ch)];
            auto& toneFilter = toneFilters_[static_cast<std::size_t>(ch)];
            float feedbackSample = feedbackState_[static_cast<std::size_t>(ch)];

            for (int i = 0; i < numSamples; ++i) {
                const float dry = data[i];
                const float delayedOut = delayLine.process(dry + feedbackSample);
                const float filtered = toneFilter.process(delayedOut);
                feedbackSample = filtered * feedbackGain;

                data[i] = dry * (1.0f - mix) + delayedOut * mix;
            }

            feedbackState_[static_cast<std::size_t>(ch)] = feedbackSample;
        }
    }

    [[nodiscard]] std::string_view moduleId() const noexcept override { return "delay.v1"; }

private:
    void updateCoefficients() noexcept {
        const float timeMs = parameters_.find("time_ms")->get();
        const float tone = parameters_.find("tone")->get();

        const int delaySamples = static_cast<int>(std::lround(timeMs * sampleRate_ / 1000.0));
        const double cutoffHz = 800.0 + static_cast<double>(tone) * 9200.0;

        for (auto& d : delayLines_) d.setDelaySamples(delaySamples);
        for (auto& f : toneFilters_) f.setLowPass(sampleRate_, cutoffHz, 0.707);
    }

    double sampleRate_ = 44100.0;
    std::vector<SimpleDelayLine> delayLines_;
    std::vector<Biquad> toneFilters_;
    std::vector<float> feedbackState_;
};

} // namespace tfcore
