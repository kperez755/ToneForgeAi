#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace tfcore {

// Small, reusable RT-safe building blocks for Schroeder/Moorer-style
// algorithmic reverb (the classic comb-filter-bank + allpass-diffuser
// topology popularized by Freeverb). Shared by Reverb (reverb.v1) and
// available to any future module that wants the same primitives (a
// dedicated Delay module, for instance, could reuse SimpleDelayLine).
//
// All three classes follow the same allocate-in-prepare()-only rule as
// the rest of this codebase: prepare() sizes the internal circular buffer
// to the largest delay length the caller will ever ask for; process() and
// setDelaySamples() never allocate.

// One feedback comb filter with a one-pole lowpass in the feedback path
// (the "damping" control on real reverbs - simulates high frequencies
// losing energy faster than lows as they bounce around a space).
class CombFilter {
public:
    void prepare(int maxDelaySamples) {
        buffer_.assign(static_cast<std::size_t>(std::max(1, maxDelaySamples)), 0.0f);
        writeIndex_ = 0;
        filterStore_ = 0.0f;
    }

    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        writeIndex_ = 0;
        filterStore_ = 0.0f;
    }

    // delaySamples must be <= the maxDelaySamples passed to prepare() -
    // the caller (Reverb) is responsible for sizing prepare() generously
    // enough across every character preset it might switch to.
    void setDelaySamples(int delaySamples) noexcept { delaySamples_ = delaySamples; }
    void setFeedback(float feedback) noexcept { feedback_ = feedback; }
    void setDamping(float damping) noexcept { damping_ = damping; }

    [[nodiscard]] float process(float x) noexcept {
        if (buffer_.empty() || delaySamples_ <= 0) return x;
        const float output = buffer_[static_cast<std::size_t>(readIndex())];
        filterStore_ = output * (1.0f - damping_) + filterStore_ * damping_;
        buffer_[static_cast<std::size_t>(writeIndex_)] = x + filterStore_ * feedback_;
        advance();
        return output;
    }

private:
    [[nodiscard]] int readIndex() const noexcept {
        const int size = static_cast<int>(buffer_.size());
        int idx = writeIndex_ - delaySamples_;
        while (idx < 0) idx += size;
        return idx;
    }

    void advance() noexcept { writeIndex_ = (writeIndex_ + 1) % static_cast<int>(buffer_.size()); }

    std::vector<float> buffer_;
    int writeIndex_ = 0;
    int delaySamples_ = 0;
    float feedback_ = 0.5f;
    float damping_ = 0.5f;
    float filterStore_ = 0.0f;
};

// Schroeder allpass filter - diffuses discrete echoes from the comb bank
// into a smooth, dense tail without adding its own coloration (an ideal
// allpass has flat magnitude response; only phase changes).
class AllpassFilter {
public:
    void prepare(int maxDelaySamples) {
        buffer_.assign(static_cast<std::size_t>(std::max(1, maxDelaySamples)), 0.0f);
        writeIndex_ = 0;
    }

    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        writeIndex_ = 0;
    }

    void setDelaySamples(int delaySamples) noexcept { delaySamples_ = delaySamples; }
    void setFeedback(float feedback) noexcept { feedback_ = feedback; }

    [[nodiscard]] float process(float x) noexcept {
        if (buffer_.empty() || delaySamples_ <= 0) return x;
        const float delayed = buffer_[static_cast<std::size_t>(readIndex())];
        const float output = -feedback_ * x + delayed;
        buffer_[static_cast<std::size_t>(writeIndex_)] = x + feedback_ * output;
        advance();
        return output;
    }

private:
    [[nodiscard]] int readIndex() const noexcept {
        const int size = static_cast<int>(buffer_.size());
        int idx = writeIndex_ - delaySamples_;
        while (idx < 0) idx += size;
        return idx;
    }

    void advance() noexcept { writeIndex_ = (writeIndex_ + 1) % static_cast<int>(buffer_.size()); }

    std::vector<float> buffer_;
    int writeIndex_ = 0;
    int delaySamples_ = 0;
    float feedback_ = 0.5f;
};

// Plain feedback-free delay tap, used for the pre-delay stage (the gap
// between the dry signal and the onset of the reverb tail, which is what
// gives a sense of a room's physical size independent of decay length).
class SimpleDelayLine {
public:
    void prepare(int maxDelaySamples) {
        buffer_.assign(static_cast<std::size_t>(std::max(1, maxDelaySamples)), 0.0f);
        writeIndex_ = 0;
    }

    void reset() noexcept {
        std::fill(buffer_.begin(), buffer_.end(), 0.0f);
        writeIndex_ = 0;
    }

    void setDelaySamples(int delaySamples) noexcept {
        delaySamples_ = std::clamp(delaySamples, 0, static_cast<int>(buffer_.size()) - 1);
    }

    [[nodiscard]] float process(float x) noexcept {
        if (buffer_.empty()) return x;
        // delaySamples_ == 0 must be true zero-latency pass-through, not
        // "read the slot about to be written this sample" - reading
        // before writing at that same index would return whatever was
        // written a full buffer-length ago instead of "now", trapping
        // the signal for buffer_.size() samples. Caught by
        // core/tests/test_reverb.cpp's impulse-response test: with
        // predelay_ms == 0 the impulse vanished for ~4400 samples (the
        // predelay buffer's size) instead of passing straight through -
        // exactly this bug, not a tuning issue.
        if (delaySamples_ <= 0) return x;
        const int size = static_cast<int>(buffer_.size());
        int idx = writeIndex_ - delaySamples_;
        while (idx < 0) idx += size;
        const float output = buffer_[static_cast<std::size_t>(idx)];
        buffer_[static_cast<std::size_t>(writeIndex_)] = x;
        writeIndex_ = (writeIndex_ + 1) % size;
        return output;
    }

private:
    std::vector<float> buffer_;
    int writeIndex_ = 0;
    int delaySamples_ = 0;
};

} // namespace tfcore
