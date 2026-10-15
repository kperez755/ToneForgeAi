#include <cmath>

#include "TestFramework.h"
#include "tfcore/dsp/modules/Amp.h"

using namespace tfcore;

namespace {

float rmsOf(const AudioBuffer& buf, int channel) {
    double sumSquares = 0.0;
    for (int i = 0; i < buf.numSamples(); ++i) {
        const float x = buf.channel(channel)[i];
        sumSquares += static_cast<double>(x) * x;
    }
    return static_cast<float>(std::sqrt(sumSquares / buf.numSamples()));
}

void fillSine(AudioBuffer& buf, int channel, double freqHz, double sampleRate) {
    double phase = 0.0;
    const double increment = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
    for (int i = 0; i < buf.numSamples(); ++i) {
        buf.channel(channel)[i] = static_cast<float>(0.3 * std::sin(phase));
        phase += increment;
    }
}

} // namespace

TEST_CASE("Amp output stays bounded even with max gain and extreme input") {
    Amp amp;
    amp.prepare(ProcessSpec{44100.0, 256, 1});
    amp.parameters().find("gain")->set(1.0f);

    AudioBuffer buf(1, 256);
    for (int i = 0; i < 256; ++i) buf.channel(0)[i] = (i % 2 == 0) ? 1.0f : -1.0f;
    amp.process(buf);

    for (int i = 0; i < 256; ++i) REQUIRE(std::fabs(buf.channel(0)[i]) <= 2.0f);
}

TEST_CASE("Amp bass knob increases low-frequency energy relative to a flat setting") {
    ProcessSpec spec{44100.0, 4000, 1};

    Amp flatAmp;
    flatAmp.prepare(spec);
    flatAmp.parameters().find("gain")->set(0.1f); // low gain so we're measuring EQ, not clipping
    AudioBuffer flatBuf(1, 4000);
    fillSine(flatBuf, 0, 80.0, 44100.0);
    flatAmp.process(flatBuf);

    Amp bassyAmp;
    bassyAmp.prepare(spec);
    bassyAmp.parameters().find("gain")->set(0.1f);
    bassyAmp.parameters().find("bass")->set(1.0f); // max bass boost
    AudioBuffer bassyBuf(1, 4000);
    fillSine(bassyBuf, 0, 80.0, 44100.0);
    bassyAmp.process(bassyBuf);

    // Compare energy in the back half of the buffer (filter settled).
    AudioBuffer flatTail(1, 2000), bassyTail(1, 2000);
    for (int i = 0; i < 2000; ++i) {
        flatTail.channel(0)[i] = flatBuf.channel(0)[2000 + i];
        bassyTail.channel(0)[i] = bassyBuf.channel(0)[2000 + i];
    }

    REQUIRE(rmsOf(bassyTail, 0) > rmsOf(flatTail, 0));
}

TEST_CASE("Amp bypass leaves the signal untouched") {
    Amp amp;
    amp.prepare(ProcessSpec{44100.0, 64, 1});
    amp.setBypassed(true);
    amp.parameters().find("gain")->set(1.0f);

    AudioBuffer buf(1, 64);
    for (int i = 0; i < 64; ++i) buf.channel(0)[i] = 0.4f;
    amp.process(buf);

    for (int i = 0; i < 64; ++i) REQUIRE_APPROX(buf.channel(0)[i], 0.4f);
}

TEST_CASE("Amp getState/setState round-trips all five parameters and bypass") {
    Amp amp;
    amp.prepare(ProcessSpec{44100.0, 32, 1});
    amp.parameters().find("gain")->set(0.8f);
    amp.parameters().find("bass")->set(0.2f);
    amp.parameters().find("mid")->set(0.9f);
    amp.parameters().find("treble")->set(0.1f);
    amp.parameters().find("presence")->set(0.6f);
    amp.setBypassed(true);

    auto state = amp.getState();
    REQUIRE(state.moduleId == "amp.v1");

    Amp other;
    other.prepare(ProcessSpec{44100.0, 32, 1});
    other.setState(state);
    REQUIRE(other.isBypassed() == true);
    REQUIRE_APPROX(other.parameters().find("gain")->get(), 0.8f);
    REQUIRE_APPROX(other.parameters().find("bass")->get(), 0.2f);
    REQUIRE_APPROX(other.parameters().find("mid")->get(), 0.9f);
    REQUIRE_APPROX(other.parameters().find("treble")->get(), 0.1f);
    REQUIRE_APPROX(other.parameters().find("presence")->get(), 0.6f);
}
