#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "tfcore/dsp/AudioModuleBase.h"
#include "tfcore/dsp/ModulationNetwork.h"

namespace tfcore {

// One "multi-modulation" pedal covering four related effects (the
// "Strymon Mobius" slot in a pedalboard) rather than four separate
// modules - a single shared LFO drives whichever algorithm `type`
// selects, using the same discrete-choice-knob-as-dropdown UI pattern
// built for Reverb's `character` (see docs/ARCHITECTURE.md "Pedalboard
// slots"). This is a deliberate scope choice: a pedalboard needs "a
// modulation pedal," not four independently-tunable ones, to cover most
// genres.
//
//   0 = Chorus  - short modulated delay (~11-23 ms), no feedback. Classic
//                 doubling/shimmer.
//   1 = Flanger - much shorter modulated delay (~1-7 ms) WITH feedback -
//                 the resonance is what gives flanging its "jet swoosh"
//                 comb-filter character, distinct from chorus.
//   2 = Phaser  - a cascade of 4 first-order allpass stages
//                 (ModulationNetwork.h) with a shared, LFO-swept
//                 coefficient, summed with dry to produce the moving
//                 notches. Real analog phasers often stagger each
//                 stage's frequency independently; sharing one swept
//                 frequency across all 4 stages here is a standard
//                 digital-phaser simplification, not a missing feature.
//   3 = Tremolo - plain LFO amplitude modulation, the simplest of the
//                 four.
//
// `rate_hz` and `depth` are shared across all four types rather than
// having per-type-idiomatic ranges (a real tremolo often runs faster
// than a real chorus) - one control surface, reasonable defaults, and a
// wide-enough rate range (0.05-8 Hz) to be usable for all four. `feedback`
// only does anything for Flanger and Phaser (Chorus and Tremolo ignore
// it) - documented here rather than silently doing nothing with no
// explanation.
//
// The LFO phase advances every SAMPLE, not once per block like the rest
// of this codebase's coefficient-recompute convention (Amp's tone stack,
// Reverb's comb lengths) - see ModulationNetwork.h's header comment for
// why that's a deliberate, necessary departure for anything that needs
// to sound like continuous modulation rather than a stair-stepped
// approximation of it.
//
// Parameters:
//   type      [0, 3]     Chorus / Flanger / Phaser / Tremolo
//   rate_hz   [0.05, 8]  LFO speed
//   depth     [0, 1]     modulation intensity (meaning varies by type)
//   feedback  [0, 0.9]   regeneration amount - Flanger/Phaser only
//   mix       [0, 1]     dry/wet
class Modulation final : public AudioModuleBase {
public:
    Modulation() {
        parameters_.add("type", "Type", ParameterRange{0.0f, 3.0f, 1.0f, 1.0f}, 0.0f, "",
                         std::vector<std::string>{"Chorus", "Flanger", "Phaser", "Tremolo"});
        parameters_.add("rate_hz", "Rate", ParameterRange{0.05f, 8.0f, 0.0f, 1.0f}, 1.0f, "Hz");
        parameters_.add("depth", "Depth", ParameterRange{0.0f, 1.0f, 0.0f, 1.0f}, 0.5f);
        parameters_.add("feedback", "Feedback", ParameterRange{0.0f, 0.9f, 0.0f, 1.0f}, 0.0f);
        parameters_.add("mix", "Mix", ParameterRange{0.0f, 1.0f, 0.0f, 1.0f}, 0.5f);
        parameters_.seal();
    }

    void prepare(const ProcessSpec& spec) override {
        sampleRate_ = spec.sampleRate;
        const auto numChannels = static_cast<std::size_t>(spec.numChannels);
        // Sized for the largest range either Chorus (up to ~23 ms) or
        // Flanger (up to ~7 ms) ever asks for, with margin - switching
        // `type` at runtime never needs a reallocation.
        const int maxDelaySamples = static_cast<int>(std::ceil(30.0 * sampleRate_ / 1000.0)) + 16;

        perChannel_.assign(numChannels, ChannelState{});
        for (std::size_t ch = 0; ch < numChannels; ++ch) {
            perChannel_[ch].delay.prepare(maxDelaySamples);
            // Small per-channel stereo phase offset so chorus/flanger/
            // tremolo have a sense of width instead of both channels
            // modulating in perfect lockstep. Deliberately NOT reset in
            // reset() below - see that method's comment.
            perChannel_[ch].lfoPhase = static_cast<double>(ch) * (kPi / 2.0);
        }
        reset();
    }

    void reset() noexcept override {
        for (auto& state : perChannel_) {
            state.delay.reset();
            for (auto& stage : state.allpassStages) stage.reset();
            state.feedbackState = 0.0f;
            // lfoPhase is intentionally untouched here: prepare() seeds it
            // with a per-channel stereo offset immediately before calling
            // reset(), and resetting it to 0 here would erase that offset
            // for every channel but the first. An oscillator's phase
            // doesn't need to return to a specific value for reset() to
            // have done its job (clearing audible filter/delay state).
        }
    }

    void process(AudioBuffer& buffer) noexcept override {
        if (isBypassed()) return;
        if (perChannel_.empty()) return;

        const int type = std::clamp(static_cast<int>(std::lround(parameters_.find("type")->get())), 0, 3);
        const float rateHz = parameters_.find("rate_hz")->get();
        const float depth = parameters_.find("depth")->get();
        const float feedbackGain = parameters_.find("feedback")->get();
        const float mix = parameters_.find("mix")->get();

        const double phaseIncrement = 2.0 * kPi * static_cast<double>(rateHz) / sampleRate_;
        const float sampleRateF = static_cast<float>(sampleRate_);

        const int numChannels = buffer.numChannels();
        const int numSamples = buffer.numSamples();

        for (int ch = 0; ch < numChannels && ch < static_cast<int>(perChannel_.size()); ++ch) {
            float* data = buffer.channel(ch);
            auto& state = perChannel_[static_cast<std::size_t>(ch)];

            for (int i = 0; i < numSamples; ++i) {
                const float dry = data[i];
                const float lfo = static_cast<float>(std::sin(state.lfoPhase));
                float wet = dry;

                switch (type) {
                    case 0: { // Chorus - bipolar sweep around a ~15ms base delay, no feedback
                        const float delayMs = 15.0f + depth * 8.0f * lfo;
                        wet = state.delay.process(dry, delayMs * sampleRateF / 1000.0f);
                        break;
                    }
                    case 1: { // Flanger - short unipolar-swept delay, with feedback for the comb resonance
                        const float unipolarLfo = 0.5f + 0.5f * lfo;
                        const float delayMs = 1.0f + depth * 6.0f * unipolarLfo;
                        const float toWrite = dry + state.feedbackState * feedbackGain;
                        wet = state.delay.process(toWrite, delayMs * sampleRateF / 1000.0f);
                        state.feedbackState = wet;
                        break;
                    }
                    case 2: { // Phaser - 4-stage allpass cascade, shared LFO-swept coefficient, summed with dry
                        const float unipolarLfo = 0.5f + 0.5f * lfo;
                        const double centerFreq = 200.0 + 1800.0 * static_cast<double>(unipolarLfo);
                        const float coeff = FirstOrderAllpass::coefficientForFrequency(centerFreq, sampleRate_);
                        float stageOut = dry + state.feedbackState * feedbackGain;
                        for (auto& stage : state.allpassStages) {
                            stage.setCoefficient(coeff);
                            stageOut = stage.process(stageOut);
                        }
                        state.feedbackState = stageOut;
                        wet = (dry + stageOut) * 0.5f;
                        break;
                    }
                    default: { // Tremolo - plain amplitude modulation
                        const float unipolarLfo = 0.5f + 0.5f * lfo;
                        const float gain = 1.0f - depth * (1.0f - unipolarLfo);
                        wet = dry * gain;
                        break;
                    }
                }

                data[i] = dry * (1.0f - mix) + wet * mix;

                state.lfoPhase += phaseIncrement;
                if (state.lfoPhase > 2.0 * kPi) state.lfoPhase = std::fmod(state.lfoPhase, 2.0 * kPi);
            }
        }
    }

    [[nodiscard]] std::string_view moduleId() const noexcept override { return "modulation.v1"; }

private:
    struct ChannelState {
        InterpolatingDelayLine delay;
        std::array<FirstOrderAllpass, 4> allpassStages;
        float feedbackState = 0.0f;
        double lfoPhase = 0.0;
    };

    double sampleRate_ = 44100.0;
    std::vector<ChannelState> perChannel_;
};

} // namespace tfcore
