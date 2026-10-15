#include <cmath>

#include "TestFramework.h"
#include "tfcore/dsp/modules/Modulation.h"

using namespace tfcore;

namespace {

float rmsOfRange(const AudioBuffer& buf, int channel, int start, int end) {
    double sumSquares = 0.0;
    int count = 0;
    for (int i = start; i < end && i < buf.numSamples(); ++i) {
        const float x = buf.channel(channel)[i];
        sumSquares += static_cast<double>(x) * x;
        ++count;
    }
    return count > 0 ? static_cast<float>(std::sqrt(sumSquares / count)) : 0.0f;
}

void fillSine(AudioBuffer& buf, int channel, double freqHz, double sampleRate, double amplitude) {
    double phase = 0.0;
    const double increment = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
    for (int i = 0; i < buf.numSamples(); ++i) {
        buf.channel(channel)[i] = static_cast<float>(amplitude * std::sin(phase));
        phase += increment;
    }
}

} // namespace

TEST_CASE("Modulation mix at 0 leaves the signal unchanged regardless of type") {
    for (float type = 0.0f; type <= 3.0f; type += 1.0f) {
        Modulation mod;
        mod.prepare(ProcessSpec{44100.0, 64, 1});
        mod.parameters().find("type")->set(type);
        mod.parameters().find("mix")->set(0.0f);

        AudioBuffer buf(1, 64);
        for (int i = 0; i < 64; ++i) buf.channel(0)[i] = 0.3f;
        mod.process(buf);

        for (int i = 0; i < 64; ++i) REQUIRE_APPROX(buf.channel(0)[i], 0.3f);
    }
}

TEST_CASE("Modulation Chorus with full mix measurably changes a sustained tone") {
    constexpr int numSamples = 4000;
    Modulation mod;
    mod.prepare(ProcessSpec{44100.0, numSamples, 1});
    mod.parameters().find("type")->set(0.0f); // Chorus
    mod.parameters().find("depth")->set(0.8f);
    mod.parameters().find("rate_hz")->set(1.0f);
    mod.parameters().find("mix")->set(1.0f);

    AudioBuffer buf(1, numSamples);
    fillSine(buf, 0, 220.0, 44100.0, 0.4);
    AudioBuffer dry = buf; // AudioBuffer is a value type (owns its own storage) - safe to copy for comparison
    mod.process(buf);

    double maxAbsDiff = 0.0;
    for (int i = 1000; i < 3000; ++i) {
        maxAbsDiff = std::max(maxAbsDiff, static_cast<double>(std::fabs(buf.channel(0)[i] - dry.channel(0)[i])));
    }
    REQUIRE(maxAbsDiff > 0.02);
}

TEST_CASE("Modulation Tremolo swings amplitude between near-silent and near-full over one LFO cycle") {
    constexpr int numSamples = 9000;
    Modulation mod;
    mod.prepare(ProcessSpec{44100.0, numSamples, 1});
    mod.parameters().find("type")->set(3.0f); // Tremolo
    mod.parameters().find("rate_hz")->set(5.0f); // 8820-sample period @ 44100 Hz
    mod.parameters().find("depth")->set(1.0f);
    mod.parameters().find("mix")->set(1.0f);

    AudioBuffer buf(1, numSamples);
    fillSine(buf, 0, 1000.0, 44100.0, 0.5); // fast carrier so short RMS windows are still meaningful
    mod.process(buf);

    // Channel 0's LFO phase starts at 0 (sin(0)=0), so gain is at its
    // midpoint at sample 0, rises to a peak (gain=1) near a quarter
    // period (~2205 samples) and falls to a trough (gain=0 at depth=1)
    // near three-quarters (~6615 samples) - see Modulation.h's Tremolo
    // case for the gain formula this follows from.
    const float peakRms = rmsOfRange(buf, 0, 2000, 2400);
    const float troughRms = rmsOfRange(buf, 0, 6400, 6800);

    REQUIRE(troughRms < 0.05f);
    REQUIRE(peakRms > troughRms * 3.0f);
}

TEST_CASE("Modulation Flanger stays bounded and finite at maximum feedback") {
    Modulation mod;
    constexpr int numSamples = 20000;
    mod.prepare(ProcessSpec{44100.0, numSamples, 1});
    mod.parameters().find("type")->set(1.0f); // Flanger
    mod.parameters().find("feedback")->set(0.9f); // range maximum
    mod.parameters().find("depth")->set(1.0f);
    mod.parameters().find("mix")->set(1.0f);

    AudioBuffer buf(1, numSamples);
    for (int i = 0; i < numSamples; ++i) {
        const double t = static_cast<double>(i);
        buf.channel(0)[i] = static_cast<float>(0.5 * std::sin(0.15 * t));
    }
    mod.process(buf);

    for (int i = 0; i < numSamples; ++i) {
        REQUIRE(std::isfinite(buf.channel(0)[i]));
        REQUIRE(std::fabs(buf.channel(0)[i]) <= 20.0f);
    }
}

TEST_CASE("Modulation Phaser stays bounded and finite at maximum feedback") {
    Modulation mod;
    constexpr int numSamples = 20000;
    mod.prepare(ProcessSpec{44100.0, numSamples, 1});
    mod.parameters().find("type")->set(2.0f); // Phaser
    mod.parameters().find("feedback")->set(0.9f);
    mod.parameters().find("depth")->set(1.0f);
    mod.parameters().find("mix")->set(1.0f);

    AudioBuffer buf(1, numSamples);
    for (int i = 0; i < numSamples; ++i) {
        const double t = static_cast<double>(i);
        buf.channel(0)[i] = static_cast<float>(0.5 * std::sin(0.31 * t) + 0.2 * std::sin(0.02 * t));
    }
    mod.process(buf);

    for (int i = 0; i < numSamples; ++i) {
        REQUIRE(std::isfinite(buf.channel(0)[i]));
        REQUIRE(std::fabs(buf.channel(0)[i]) <= 20.0f);
    }
}

TEST_CASE("Modulation bypass leaves the signal untouched") {
    Modulation mod;
    mod.prepare(ProcessSpec{44100.0, 64, 1});
    mod.setBypassed(true);
    mod.parameters().find("mix")->set(1.0f);
    mod.parameters().find("depth")->set(1.0f);

    AudioBuffer buf(1, 64);
    for (int i = 0; i < 64; ++i) buf.channel(0)[i] = 0.4f;
    mod.process(buf);

    for (int i = 0; i < 64; ++i) REQUIRE_APPROX(buf.channel(0)[i], 0.4f);
}

TEST_CASE("Modulation getState/setState round-trips all five parameters and bypass") {
    Modulation mod;
    mod.prepare(ProcessSpec{44100.0, 32, 1});
    mod.parameters().find("type")->set(2.0f);
    mod.parameters().find("rate_hz")->set(2.5f);
    mod.parameters().find("depth")->set(0.6f);
    mod.parameters().find("feedback")->set(0.4f);
    mod.parameters().find("mix")->set(0.7f);
    mod.setBypassed(true);

    auto state = mod.getState();
    REQUIRE(state.moduleId == "modulation.v1");

    Modulation other;
    other.prepare(ProcessSpec{44100.0, 32, 1});
    other.setState(state);
    REQUIRE(other.isBypassed() == true);
    REQUIRE_APPROX(other.parameters().find("type")->get(), 2.0f);
    REQUIRE_APPROX(other.parameters().find("rate_hz")->get(), 2.5f);
    REQUIRE_APPROX(other.parameters().find("depth")->get(), 0.6f);
    REQUIRE_APPROX(other.parameters().find("feedback")->get(), 0.4f);
    REQUIRE_APPROX(other.parameters().find("mix")->get(), 0.7f);
}
