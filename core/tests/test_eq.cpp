#include <cmath>

#include "TestFramework.h"
#include "tfcore/dsp/modules/Eq.h"

using namespace tfcore;

namespace {

float rmsOfTail(const AudioBuffer& buf, int channel, int tailStart) {
    double sumSquares = 0.0;
    int count = 0;
    for (int i = tailStart; i < buf.numSamples(); ++i) {
        const float x = buf.channel(channel)[i];
        sumSquares += static_cast<double>(x) * x;
        ++count;
    }
    return static_cast<float>(std::sqrt(sumSquares / count));
}

AudioBuffer makeSine(double freqHz, double sampleRate, int numSamples) {
    AudioBuffer buf(1, numSamples);
    double phase = 0.0;
    const double increment = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
    for (int i = 0; i < numSamples; ++i) {
        buf.channel(0)[i] = static_cast<float>(0.4 * std::sin(phase));
        phase += increment;
    }
    return buf;
}

} // namespace

TEST_CASE("Eq high_gain_db boost increases high-frequency energy relative to flat") {
    Eq flat;
    flat.prepare(ProcessSpec{44100.0, 4000, 1});
    auto flatBuf = makeSine(8000.0, 44100.0, 4000);
    flat.process(flatBuf);

    Eq boosted;
    boosted.prepare(ProcessSpec{44100.0, 4000, 1});
    boosted.parameters().find("high_gain_db")->set(12.0f);
    auto boostedBuf = makeSine(8000.0, 44100.0, 4000);
    boosted.process(boostedBuf);

    REQUIRE(rmsOfTail(boostedBuf, 0, 2000) > rmsOfTail(flatBuf, 0, 2000));
}

TEST_CASE("Eq low_gain_db cut decreases low-frequency energy relative to flat") {
    Eq flat;
    flat.prepare(ProcessSpec{44100.0, 4000, 1});
    auto flatBuf = makeSine(60.0, 44100.0, 4000);
    flat.process(flatBuf);

    Eq cut;
    cut.prepare(ProcessSpec{44100.0, 4000, 1});
    cut.parameters().find("low_gain_db")->set(-12.0f);
    auto cutBuf = makeSine(60.0, 44100.0, 4000);
    cut.process(cutBuf);

    REQUIRE(rmsOfTail(cutBuf, 0, 2000) < rmsOfTail(flatBuf, 0, 2000));
}

TEST_CASE("Eq with all bands at 0 dB leaves signal amplitude roughly unchanged") {
    Eq eq;
    eq.prepare(ProcessSpec{44100.0, 4000, 1});
    auto buf = makeSine(1000.0, 44100.0, 4000);
    const float inputRms = rmsOfTail(buf, 0, 2000);
    eq.process(buf);
    const float outputRms = rmsOfTail(buf, 0, 2000);

    REQUIRE(std::fabs(inputRms - outputRms) < 0.02f);
}

TEST_CASE("Eq bypass leaves the signal untouched") {
    Eq eq;
    eq.prepare(ProcessSpec{44100.0, 64, 1});
    eq.setBypassed(true);
    eq.parameters().find("low_gain_db")->set(12.0f);

    AudioBuffer buf(1, 64);
    for (int i = 0; i < 64; ++i) buf.channel(0)[i] = 0.2f;
    eq.process(buf);

    for (int i = 0; i < 64; ++i) REQUIRE_APPROX(buf.channel(0)[i], 0.2f);
}

TEST_CASE("Eq getState/setState round-trips all three bands and bypass") {
    Eq eq;
    eq.prepare(ProcessSpec{44100.0, 32, 1});
    eq.parameters().find("low_gain_db")->set(5.0f);
    eq.parameters().find("mid_gain_db")->set(-3.0f);
    eq.parameters().find("high_gain_db")->set(7.0f);
    eq.setBypassed(true);

    auto state = eq.getState();
    REQUIRE(state.moduleId == "eq.v1");

    Eq other;
    other.prepare(ProcessSpec{44100.0, 32, 1});
    other.setState(state);
    REQUIRE(other.isBypassed() == true);
    REQUIRE_APPROX(other.parameters().find("low_gain_db")->get(), 5.0f);
    REQUIRE_APPROX(other.parameters().find("mid_gain_db")->get(), -3.0f);
    REQUIRE_APPROX(other.parameters().find("high_gain_db")->get(), 7.0f);
}
