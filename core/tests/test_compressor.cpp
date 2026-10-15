#include <cmath>

#include "TestFramework.h"
#include "tfcore/dsp/modules/Compressor.h"

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

AudioBuffer makeSine(double amplitude, double freqHz, double sampleRate, int numSamples) {
    AudioBuffer buf(1, numSamples);
    double phase = 0.0;
    const double increment = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
    for (int i = 0; i < numSamples; ++i) {
        buf.channel(0)[i] = static_cast<float>(amplitude * std::sin(phase));
        phase += increment;
    }
    return buf;
}

} // namespace

TEST_CASE("Compressor reduces the level of a signal well above threshold") {
    constexpr int numSamples = 4000;

    Compressor comp;
    comp.prepare(ProcessSpec{44100.0, numSamples, 1});
    comp.parameters().find("threshold_db")->set(-18.0f);
    comp.parameters().find("ratio")->set(8.0f);
    comp.parameters().find("attack_ms")->set(1.0f);
    comp.parameters().find("release_ms")->set(50.0f);
    comp.parameters().find("makeup_db")->set(0.0f);

    auto buf = makeSine(0.9, 200.0, 44100.0, numSamples); // loud - well above -18 dB
    const float inputRms = rmsOfRange(buf, 0, 2000, numSamples); // settled region, pre-compression reference on a copy below
    comp.process(buf);
    const float outputRms = rmsOfRange(buf, 0, 2000, numSamples);

    REQUIRE(outputRms < inputRms);
}

TEST_CASE("Compressor leaves a signal below threshold essentially unchanged with no makeup gain") {
    constexpr int numSamples = 2000;

    Compressor comp;
    comp.prepare(ProcessSpec{44100.0, numSamples, 1});
    comp.parameters().find("threshold_db")->set(-6.0f); // quiet signal below this stays untouched
    comp.parameters().find("ratio")->set(10.0f);
    comp.parameters().find("makeup_db")->set(0.0f);

    auto buf = makeSine(0.05, 200.0, 44100.0, numSamples); // ~-26 dBFS, well below threshold
    const float before = rmsOfRange(buf, 0, 1000, numSamples);
    comp.process(buf);
    const float after = rmsOfRange(buf, 0, 1000, numSamples);

    REQUIRE(std::fabs(after - before) < 0.002f);
}

TEST_CASE("Compressor makeup gain boosts an under-threshold signal by roughly the expected amount") {
    constexpr int numSamples = 2000;

    Compressor comp;
    comp.prepare(ProcessSpec{44100.0, numSamples, 1});
    comp.parameters().find("threshold_db")->set(0.0f); // never engages at this amplitude
    comp.parameters().find("makeup_db")->set(6.0f);     // ~2x linear

    auto buf = makeSine(0.1, 200.0, 44100.0, numSamples);
    const float before = rmsOfRange(buf, 0, 1000, numSamples);
    comp.process(buf);
    const float after = rmsOfRange(buf, 0, 1000, numSamples);

    const float expectedRatio = std::pow(10.0f, 6.0f / 20.0f);
    REQUIRE(std::fabs(after / before - expectedRatio) < 0.05f);
}

TEST_CASE("Compressor higher ratio produces more gain reduction than a lower ratio for the same signal") {
    constexpr int numSamples = 4000;

    Compressor low;
    low.prepare(ProcessSpec{44100.0, numSamples, 1});
    low.parameters().find("threshold_db")->set(-18.0f);
    low.parameters().find("ratio")->set(2.0f);
    low.parameters().find("attack_ms")->set(1.0f);
    auto lowBuf = makeSine(0.9, 200.0, 44100.0, numSamples);
    low.process(lowBuf);

    Compressor high;
    high.prepare(ProcessSpec{44100.0, numSamples, 1});
    high.parameters().find("threshold_db")->set(-18.0f);
    high.parameters().find("ratio")->set(15.0f);
    high.parameters().find("attack_ms")->set(1.0f);
    auto highBuf = makeSine(0.9, 200.0, 44100.0, numSamples);
    high.process(highBuf);

    REQUIRE(rmsOfRange(highBuf, 0, 2000, numSamples) < rmsOfRange(lowBuf, 0, 2000, numSamples));
}

TEST_CASE("Compressor bypass leaves the signal untouched") {
    Compressor comp;
    comp.prepare(ProcessSpec{44100.0, 64, 1});
    comp.setBypassed(true);
    comp.parameters().find("ratio")->set(20.0f);

    AudioBuffer buf(1, 64);
    for (int i = 0; i < 64; ++i) buf.channel(0)[i] = 0.5f;
    comp.process(buf);

    for (int i = 0; i < 64; ++i) REQUIRE_APPROX(buf.channel(0)[i], 0.5f);
}

TEST_CASE("Compressor getState/setState round-trips all five parameters and bypass") {
    Compressor comp;
    comp.prepare(ProcessSpec{44100.0, 32, 1});
    comp.parameters().find("threshold_db")->set(-24.0f);
    comp.parameters().find("ratio")->set(6.0f);
    comp.parameters().find("attack_ms")->set(3.0f);
    comp.parameters().find("release_ms")->set(150.0f);
    comp.parameters().find("makeup_db")->set(4.0f);
    comp.setBypassed(true);

    auto state = comp.getState();
    REQUIRE(state.moduleId == "compressor.v1");

    Compressor other;
    other.prepare(ProcessSpec{44100.0, 32, 1});
    other.setState(state);
    REQUIRE(other.isBypassed() == true);
    REQUIRE_APPROX(other.parameters().find("threshold_db")->get(), -24.0f);
    REQUIRE_APPROX(other.parameters().find("ratio")->get(), 6.0f);
    REQUIRE_APPROX(other.parameters().find("attack_ms")->get(), 3.0f);
    REQUIRE_APPROX(other.parameters().find("release_ms")->get(), 150.0f);
    REQUIRE_APPROX(other.parameters().find("makeup_db")->get(), 4.0f);
}
