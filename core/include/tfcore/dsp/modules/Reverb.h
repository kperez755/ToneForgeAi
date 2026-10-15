#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "tfcore/dsp/AudioModuleBase.h"
#include "tfcore/dsp/Biquad.h" // for kPi
#include "tfcore/dsp/ReverbNetwork.h"

namespace tfcore {

// Algorithmic reverb: the classic Schroeder/Moorer topology (8 parallel
// feedback comb filters feeding 4 series allpass diffusers, per channel) -
// the same family of algorithm "Freeverb" popularized, not a physical
// model and not convolution against a measured IR. Honest scope note in
// the same spirit as Cab (docs/DSP.md): this will sound like a
// competent, hand-tuned digital reverb pedal, not a specific real hall,
// plate, or spring tank.
//
// The "character" parameter picks between four hand-tuned presets that
// vary comb/allpass delay-length tables, feedback, and damping - the
// standard way algorithmic reverbs differentiate room types without a
// different algorithm per type:
//   0 = Hall   - long comb/allpass delays, high feedback ceiling, full
//                damping response -> large, smooth, long-decaying space.
//   1 = Plate  - shorter delays than Hall, denser diffusion (higher
//                allpass feedback), less damping -> bright, dense,
//                metallic-ish plate character.
//   2 = Spring - short delays, sparse diffusion, PLUS a slow sine LFO
//                modulating one comb's delay length by a few samples -
//                approximates a spring tank's characteristic pitch
//                "flutter"/boing. Still not a physical spring model.
//   3 = Room   - shortest delays, sparse diffusion, extra damping ->
//                small, fast-decaying space.
//
// "character" is exposed as an ordinary continuous 0..3 knob (rounded to
// the nearest integer each block, see updateCoefficients()) rather than a
// discrete stepped switch, because the pedalboard slot system's generic
// knob pool (docs/ARCHITECTURE.md 4.1) doesn't have a segmented-switch
// widget type yet - turning the knob steps through the four characters in
// quarters. A nicer detented/labeled control is a UI follow-up, not a DSP
// gap.
//
// Coefficients (delay lengths, feedback, damping) are recomputed once per
// block, not per sample - same convention as Amp's tone stack and Eq's
// bands, and cheap enough here too (a handful of array writes).
class Reverb final : public AudioModuleBase {
public:
    Reverb() {
        parameters_.add("character", "Character", ParameterRange{0.0f, 3.0f, 1.0f, 1.0f}, 0.0f, /*units=*/"",
                         std::vector<std::string>{"Hall", "Plate", "Spring", "Room"});
        parameters_.add("size", "Size", ParameterRange{0.0f, 1.0f, 0.0f, 1.0f}, 0.5f);
        parameters_.add("damping", "Damping", ParameterRange{0.0f, 1.0f, 0.0f, 1.0f}, 0.5f);
        parameters_.add("predelay_ms", "Pre-Delay", ParameterRange{0.0f, 100.0f, 0.0f, 1.0f}, 10.0f, "ms");
        parameters_.add("mix", "Mix", ParameterRange{0.0f, 1.0f, 0.0f, 1.0f}, 0.3f);
        parameters_.seal();
    }

    void prepare(const ProcessSpec& spec) override {
        sampleRate_ = spec.sampleRate;
        const double srScale = sampleRate_ / 44100.0;

        // Every comb/allpass buffer is sized to the largest reference
        // delay length across ALL FOUR character presets (Hall's, the
        // longest) plus headroom for stereo spread and spring LFO
        // modulation - so switching character at runtime never needs a
        // reallocation, just a different (smaller-or-equal) delay length
        // written into an already-sized buffer. This wastes a little
        // memory when a short preset (Room, Spring) is active, in
        // exchange for character switches being real-time safe.
        constexpr int kMaxCombRef = 1617;   // Hall's longest comb, samples @ 44100 Hz
        constexpr int kMaxAllpassRef = 556; // Hall's longest allpass, samples @ 44100 Hz
        constexpr int kHeadroomSamples = 128; // stereo spread + LFO depth safety margin

        const int combBufferSize = static_cast<int>(std::ceil(kMaxCombRef * srScale)) + kHeadroomSamples;
        const int allpassBufferSize = static_cast<int>(std::ceil(kMaxAllpassRef * srScale)) + kHeadroomSamples;
        const int predelayBufferSize = static_cast<int>(std::ceil(100.0 * sampleRate_ / 1000.0)) + 16; // 100ms max + margin

        const auto numChannels = static_cast<std::size_t>(spec.numChannels);
        combs_.assign(numChannels, {});
        allpasses_.assign(numChannels, {});
        predelay_.assign(numChannels, SimpleDelayLine{});
        lfoPhase_.assign(numChannels, 0.0);

        for (std::size_t ch = 0; ch < numChannels; ++ch) {
            for (auto& c : combs_[ch]) c.prepare(combBufferSize);
            for (auto& a : allpasses_[ch]) a.prepare(allpassBufferSize);
            predelay_[ch].prepare(predelayBufferSize);
        }

        reset();
        updateCoefficients(spec.maximumBlockSize);
    }

    void reset() noexcept override {
        for (auto& channelCombs : combs_)
            for (auto& c : channelCombs) c.reset();
        for (auto& channelAllpasses : allpasses_)
            for (auto& a : channelAllpasses) a.reset();
        for (auto& p : predelay_) p.reset();
    }

    void process(AudioBuffer& buffer) noexcept override {
        if (isBypassed()) return;
        if (combs_.empty()) return;

        updateCoefficients(buffer.numSamples());

        const float mix = parameters_.find("mix")->get();
        const int numChannels = buffer.numChannels();
        const int numSamples = buffer.numSamples();

        for (int ch = 0; ch < numChannels && ch < static_cast<int>(combs_.size()); ++ch) {
            float* data = buffer.channel(ch);
            auto& channelCombs = combs_[static_cast<std::size_t>(ch)];
            auto& channelAllpasses = allpasses_[static_cast<std::size_t>(ch)];
            auto& channelPredelay = predelay_[static_cast<std::size_t>(ch)];

            for (int i = 0; i < numSamples; ++i) {
                const float dry = data[i];
                const float predelayed = channelPredelay.process(dry);

                float combSum = 0.0f;
                for (auto& comb : channelCombs) combSum += comb.process(predelayed);
                combSum *= (1.0f / static_cast<float>(channelCombs.size()));

                float diffused = combSum;
                for (auto& allpass : channelAllpasses) diffused = allpass.process(diffused);

                data[i] = dry * (1.0f - mix) + diffused * mix;
            }
        }
    }

    [[nodiscard]] std::string_view moduleId() const noexcept override { return "reverb.v1"; }

private:
    struct CharacterPreset {
        const std::array<int, 8>* combRef;
        const std::array<int, 4>* allpassRef;
        float feedbackBase;
        float feedbackRange;   // feedback = feedbackBase + size * feedbackRange
        float dampingScale;    // effective damping = clamp(dampingKnob * dampingScale, 0, 0.98)
        float allpassFeedback;
        double lfoRateHz;      // 0 = no modulation (only Spring modulates)
        double lfoDepthSamples; // ignored when lfoRateHz == 0
    };

    static const CharacterPreset& presetFor(int character) noexcept {
        // Reference delay lengths @ 44100 Hz, scaled by sampleRate/44100 in
        // updateCoefficients(). Hall uses the classic Freeverb tuning
        // values; Plate/Room/Spring are Hall's table scaled down (0.75x /
        // 0.45x / 0.28x respectively) - shorter delays raise each comb's
        // resonant frequency and shrink the perceived space, which is the
        // standard way to differentiate room "size" character in this
        // family of algorithm without a different topology per preset.
        static constexpr std::array<int, 8> kHallComb{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static constexpr std::array<int, 4> kHallAllpass{556, 441, 341, 225};
        static constexpr std::array<int, 8> kPlateComb{837, 891, 958, 1017, 1067, 1118, 1168, 1213};
        static constexpr std::array<int, 4> kPlateAllpass{417, 331, 256, 169};
        static constexpr std::array<int, 8> kSpringComb{312, 333, 358, 380, 398, 417, 436, 453};
        static constexpr std::array<int, 4> kSpringAllpass{156, 123, 95, 63};
        static constexpr std::array<int, 8> kRoomComb{502, 535, 575, 610, 640, 671, 701, 728};
        static constexpr std::array<int, 4> kRoomAllpass{250, 198, 153, 101};

        static const CharacterPreset kHall{&kHallComb, &kHallAllpass, 0.70f, 0.28f, 1.0f, 0.50f, 0.0, 0.0};
        static const CharacterPreset kPlate{&kPlateComb, &kPlateAllpass, 0.65f, 0.30f, 0.7f, 0.60f, 0.0, 0.0};
        static const CharacterPreset kSpring{&kSpringComb, &kSpringAllpass, 0.55f, 0.35f, 1.1f, 0.35f, 3.7, 3.0};
        static const CharacterPreset kRoom{&kRoomComb, &kRoomAllpass, 0.50f, 0.35f, 1.3f, 0.35f, 0.0, 0.0};

        switch (character) {
            case 1: return kPlate;
            case 2: return kSpring;
            case 3: return kRoom;
            default: return kHall; // 0, and any out-of-range value
        }
    }

    void updateCoefficients(int blockSize) noexcept {
        const int character = std::clamp(static_cast<int>(std::lround(parameters_.find("character")->get())), 0, 3);
        const float size = parameters_.find("size")->get();
        const float dampingKnob = parameters_.find("damping")->get();
        const float predelayMs = parameters_.find("predelay_ms")->get();

        const CharacterPreset& preset = presetFor(character);
        const double srScale = sampleRate_ / 44100.0;

        const float feedback = preset.feedbackBase + size * preset.feedbackRange;
        const float damping = std::clamp(dampingKnob * preset.dampingScale, 0.0f, 0.98f);
        const int predelaySamples = static_cast<int>(std::lround(predelayMs * sampleRate_ / 1000.0));
        const double lfoPhaseIncrementPerSample = 2.0 * kPi * preset.lfoRateHz / sampleRate_;

        constexpr int kStereoSpreadRef = 23; // classic Freeverb per-channel spread, samples @ 44100 Hz

        for (std::size_t ch = 0; ch < combs_.size(); ++ch) {
            const int spread = static_cast<int>(std::lround(kStereoSpreadRef * static_cast<double>(ch) * srScale));

            for (std::size_t i = 0; i < combs_[ch].size(); ++i) {
                int delaySamples = static_cast<int>(std::lround((*preset.combRef)[i] * srScale)) + spread;

                if (preset.lfoRateHz > 0.0 && i == 0) {
                    const double modSamples = preset.lfoDepthSamples * srScale * std::sin(lfoPhase_[ch]);
                    delaySamples += static_cast<int>(std::lround(modSamples));
                }

                combs_[ch][i].setDelaySamples(std::max(1, delaySamples));
                combs_[ch][i].setFeedback(feedback);
                combs_[ch][i].setDamping(damping);
            }

            for (std::size_t i = 0; i < allpasses_[ch].size(); ++i) {
                const int delaySamples = static_cast<int>(std::lround((*preset.allpassRef)[i] * srScale)) + spread;
                allpasses_[ch][i].setDelaySamples(std::max(1, delaySamples));
                allpasses_[ch][i].setFeedback(preset.allpassFeedback);
            }

            predelay_[ch].setDelaySamples(predelaySamples);

            lfoPhase_[ch] += lfoPhaseIncrementPerSample * blockSize;
            if (lfoPhase_[ch] > 2.0 * kPi) lfoPhase_[ch] = std::fmod(lfoPhase_[ch], 2.0 * kPi);
        }
    }

    double sampleRate_ = 44100.0;
    std::vector<std::array<CombFilter, 8>> combs_;
    std::vector<std::array<AllpassFilter, 4>> allpasses_;
    std::vector<SimpleDelayLine> predelay_;
    std::vector<double> lfoPhase_;
};

} // namespace tfcore
