#pragma once

#include <cstddef>
#include <vector>

namespace tfcore {

// Minimal, dependency-free multichannel audio buffer used throughout the core
// library. The JUCE plugin layer adapts juce::AudioBuffer<float> to/from this
// type at the boundary so `core` never needs to know JUCE exists.
//
// NOTE: allocation happens only in prepare()/resize(), never in process().
// Modules must call resize() ahead of time (in prepare()) for any scratch
// buffers they need; process() must be allocation-free.
class AudioBuffer {
public:
    AudioBuffer() = default;

    AudioBuffer(int numChannels, int numSamples) { resize(numChannels, numSamples); }

    void resize(int numChannels, int numSamples) {
        channels_ = numChannels;
        samples_ = numSamples;
        storage_.assign(static_cast<std::size_t>(numChannels) * static_cast<std::size_t>(numSamples), 0.0f);
    }

    [[nodiscard]] int numChannels() const noexcept { return channels_; }
    [[nodiscard]] int numSamples() const noexcept { return samples_; }

    float* channel(int ch) noexcept { return storage_.data() + static_cast<std::size_t>(ch) * samples_; }
    const float* channel(int ch) const noexcept { return storage_.data() + static_cast<std::size_t>(ch) * samples_; }

    void clear() noexcept {
        for (auto& s : storage_) s = 0.0f;
    }

private:
    int channels_ = 0;
    int samples_ = 0;
    std::vector<float> storage_; // allocated only outside the audio thread
};

// Fixed configuration handed to prepare(). No allocation happens from this
// struct itself; it just describes the context the module must prepare for.
struct ProcessSpec {
    double sampleRate = 44100.0;
    int maximumBlockSize = 512;
    int numChannels = 2;
};

} // namespace tfcore
