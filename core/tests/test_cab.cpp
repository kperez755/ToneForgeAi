#include <cmath>

#include "TestFramework.h"
#include "tfcore/dsp/modules/Cab.h"

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
        buf.channel(0)[i] = static_cast<float>(0.5 * std::sin(phase));
        phase += increment;
    }
    return buf;
}

} // namespace

TEST_CASE("Cab attenuates very low frequencies relative to midrange") {
    Cab cab;
    cab.prepare(ProcessSpec{44100.0, 4000, 1});

    auto lowBuf = makeSine(40.0, 44100.0, 4000);
    cab.process(lowBuf);
    const float lowRms = rmsOfTail(lowBuf, 0, 2000);

    Cab cab2;
    cab2.prepare(ProcessSpec{44100.0, 4000, 1});
    auto midBuf = makeSine(800.0, 44100.0, 4000);
    cab2.process(midBuf);
    const float midRms = rmsOfTail(midBuf, 0, 2000);

    REQUIRE(midRms > lowRms);
}

TEST_CASE("Cab attenuates very high frequencies relative to midrange") {
    Cab cab;
    cab.prepare(ProcessSpec{44100.0, 4000, 1});

    auto highBuf = makeSine(12000.0, 44100.0, 4000);
    cab.process(highBuf);
    const float highRms = rmsOfTail(highBuf, 0, 2000);

    Cab cab2;
    cab2.prepare(ProcessSpec{44100.0, 4000, 1});
    auto midBuf = makeSine(800.0, 44100.0, 4000);
    cab2.process(midBuf);
    const float midRms = rmsOfTail(midBuf, 0, 2000);

    REQUIRE(midRms > highRms);
}

TEST_CASE("Cab mix parameter at 0 leaves the signal unchanged") {
    Cab cab;
    cab.prepare(ProcessSpec{44100.0, 64, 1});
    cab.parameters().find("mix")->set(0.0f);

    AudioBuffer buf(1, 64);
    for (int i = 0; i < 64; ++i) buf.channel(0)[i] = 0.33f;
    cab.process(buf);

    for (int i = 0; i < 64; ++i) REQUIRE_APPROX(buf.channel(0)[i], 0.33f);
}

TEST_CASE("Cab bypass leaves the signal untouched") {
    Cab cab;
    cab.prepare(ProcessSpec{44100.0, 64, 1});
    cab.setBypassed(true);

    AudioBuffer buf(1, 64);
    for (int i = 0; i < 64; ++i) buf.channel(0)[i] = 0.5f;
    cab.process(buf);

    for (int i = 0; i < 64; ++i) REQUIRE_APPROX(buf.channel(0)[i], 0.5f);
}

TEST_CASE("Cab getState/setState round-trips mix and bypass") {
    Cab cab;
    cab.prepare(ProcessSpec{44100.0, 32, 1});
    cab.parameters().find("mix")->set(0.4f);
    cab.setBypassed(true);

    auto state = cab.getState();
    REQUIRE(state.moduleId == "cab.v1");

    Cab other;
    other.prepare(ProcessSpec{44100.0, 32, 1});
    other.setState(state);
    REQUIRE(other.isBypassed() == true);
    REQUIRE_APPROX(other.parameters().find("mix")->get(), 0.4f);
}
