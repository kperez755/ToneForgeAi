#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "tfcore/dsp/Biquad.h" // for kPi

namespace tfcore {

// Reusable building blocks for LFO-modulated effects (Chorus, Flanger,
// Phaser, Tremolo - see modules/Modulation.h). Unlike the rest of this
// codebase's convention of recomputing coefficients once per block
// (Amp's tone stack, Eq's bands, Reverb's comb/allpass lengths), true
// modulation effects need their delay time / filter coefficient to move
// CONTINUOUSLY, sample by sample - updating only once per block would
// mean the modulation only ever changes in stair-steps at block
// boundaries, which is audible as zipper noise rather than a smooth
// sweep. That's why Modulation.h computes its LFO and pushes a new
// coefficient into these classes on every sample, a deliberate,
// documented departure from the rest of the codebase's per-block norm -
// not an oversight.

// A delay line supporting fractional (linearly-interpolated) delay
// lengths, needed for chorus/flanger where the delay time itself is the
// thing being modulated smoothly by an LFO. `tfcore::SimpleDelayLine`
// (ReverbNetwork.h) only supports integer delay lengths, which is fine
// for a fixed predelay but would produce audible "stepping" here.
class InterpolatingDelayLine {
public:
    void prepare(int maxDelaySamples) {
        buffer_.assign(static_cast<std::size_t>(std::max(4, maxDelaySamples)), 0.0f);
        writeIndex_ = 0;
    }

    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        writeIndex_ = 0;
    }

    // delaySamples should stay comfortably >= 1 sample - true sub-1-sample
    // delays aren't meaningful with this write-then-read-with-offset
    // structure (there's nothing to interpolate between yet on the same
    // sample it's written). Callers (Modulation.h) keep their modulation
    // ranges well clear of 0 for exactly this reason.
    [[nodiscard]] float process(float x, float delaySamples) noexcept {
        if (buffer_.empty()) return x;
        const int size = static_cast<int>(buffer_.size());
        const float clampedDelay = std::clamp(delaySamples, 1.0f, static_cast<float>(size - 2));

        const float readPos = static_cast<float>(writeIndex_) - clampedDelay;
        int idx0 = static_cast<int>(std::floor(readPos));
        const float frac = readPos - static_cast<float>(idx0);
        idx0 %= size;
        if (idx0 < 0) idx0 += size;
        int idx1 = idx0 + 1;
        if (idx1 >= size) idx1 -= size;

        const float output =
            buffer_[static_cast<std::size_t>(idx0)] * (1.0f - frac) + buffer_[static_cast<std::size_t>(idx1)] * frac;

        buffer_[static_cast<std::size_t>(writeIndex_)] = x;
        writeIndex_ = (writeIndex_ + 1) % size;
        return output;
    }

private:
    std::vector<float> buffer_;
    int writeIndex_ = 0;
};

// First-order allpass filter, the standard building block of a digital
// phaser: flat magnitude response, frequency-dependent phase shift. A
// phaser's characteristic moving notches come from SUMMING the dry
// signal with a cascade of these (see Modulation.h) while sweeping their
// shared coefficient with an LFO - the allpass alone doesn't notch
// anything; the interference between dry and phase-shifted does.
class FirstOrderAllpass {
public:
    void reset() noexcept { x1_ = y1_ = 0.0f; }

    void setCoefficient(float a) noexcept { a_ = a; }

    // Standard formula mapping a target "notch" frequency to this
    // stage's coefficient (see e.g. DAFX / Julius O. Smith's allpass
    // filter design notes): a = (tan(pi*fc/fs) - 1) / (tan(pi*fc/fs) + 1).
    [[nodiscard]] static float coefficientForFrequency(double freqHz, double sampleRate) noexcept {
        const double clampedFreq = std::clamp(freqHz, 20.0, sampleRate * 0.45);
        const double t = std::tan(kPi * clampedFreq / sampleRate);
        return static_cast<float>((t - 1.0) / (t + 1.0));
    }

    [[nodiscard]] float process(float x) noexcept {
        const float y = a_ * x + x1_ - a_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }

private:
    float a_ = 0.0f;
    float x1_ = 0.0f;
    float y1_ = 0.0f;
};

} // namespace tfcore
