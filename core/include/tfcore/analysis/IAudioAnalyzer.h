#pragma once

#include <cmath>
#include <cstddef>

#include "tfcore/dsp/AudioBuffer.h"

namespace tfcore {

// Future work (docs/ROADMAP.md M8): real FFT-based spectral analysis,
// envelope-follower dynamics, onset-detection transients, and harmonic
// analysis for the "Analyze Recording" AI panel action. The interface is
// defined now, with a mock implementation, so the rest of the app (UI,
// preset-generation-from-analysis pipeline) can be built and tested against
// a stable contract today. Off-audio-thread only: analysis runs on a
// background worker thread against a captured/loaded buffer, never inline
// in process().

struct SpectralProfile {
    float brightnessScore = 0.5f;   // 0 = dark, 1 = bright, heuristic summary
    float lowMidBalance = 0.5f;
    float presenceScore = 0.5f;
};

struct DynamicsProfile {
    float estimatedCompressionRatio = 1.0f;
    float crestFactorDb = 12.0f;
    float rmsDb = -18.0f;
};

struct TransientProfile {
    float attackSharpness = 0.5f; // 0 = soft/rounded, 1 = sharp/percussive
    float estimatedOnsetCount = 0.0f;
};

struct HarmonicProfile {
    float evenHarmonicRatio = 0.5f;
    float oddHarmonicRatio = 0.5f;
    float estimatedDistortionAmount = 0.0f;
};

class IAudioAnalyzer {
public:
    virtual ~IAudioAnalyzer() = default;

    virtual SpectralProfile analyzeSpectrum(const AudioBuffer& buffer) = 0;
    virtual DynamicsProfile analyzeDynamics(const AudioBuffer& buffer) = 0;
    virtual TransientProfile analyzeTransients(const AudioBuffer& buffer) = 0;
    virtual HarmonicProfile analyzeHarmonics(const AudioBuffer& buffer) = 0;
};

// Deterministic, clearly-labeled placeholder: returns plausible fixed
// values derived from trivial signal statistics (peak/RMS) rather than
// real spectral/harmonic analysis. Exists so calling code and tests have
// something concrete to run against before M8 lands. Must never be
// presented to the user as a real analysis result without the mock nature
// being obvious from context (e.g. clearly gated behind a feature flag in
// the UI once real analysis exists).
class MockAudioAnalyzer final : public IAudioAnalyzer {
public:
    SpectralProfile analyzeSpectrum(const AudioBuffer& buffer) override {
        (void)buffer;
        return SpectralProfile{};
    }

    DynamicsProfile analyzeDynamics(const AudioBuffer& buffer) override {
        DynamicsProfile profile;
        profile.rmsDb = estimateRmsDb(buffer);
        return profile;
    }

    TransientProfile analyzeTransients(const AudioBuffer& buffer) override {
        (void)buffer;
        return TransientProfile{};
    }

    HarmonicProfile analyzeHarmonics(const AudioBuffer& buffer) override {
        (void)buffer;
        return HarmonicProfile{};
    }

private:
    static float estimateRmsDb(const AudioBuffer& buffer) {
        if (buffer.numChannels() == 0 || buffer.numSamples() == 0) return -100.0f;
        double sumSquares = 0.0;
        std::size_t count = 0;
        for (int ch = 0; ch < buffer.numChannels(); ++ch) {
            const float* data = buffer.channel(ch);
            for (int i = 0; i < buffer.numSamples(); ++i) {
                sumSquares += static_cast<double>(data[i]) * data[i];
                ++count;
            }
        }
        if (count == 0) return -100.0f;
        const double rms = std::sqrt(sumSquares / static_cast<double>(count));
        if (rms <= 0.0) return -100.0f;
        return static_cast<float>(20.0 * std::log10(rms));
    }
};

} // namespace tfcore
