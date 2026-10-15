#include <cmath>

#include "TestFramework.h"
#include "tfcore/dsp/modules/Distortion.h"
#include "tfcore/dsp/modules/Drive.h"

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

void fillSine(AudioBuffer& buf, int channel, double freqHz, double sampleRate, double amplitude) {
    double phase = 0.0;
    const double increment = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
    for (int i = 0; i < buf.numSamples(); ++i) {
        buf.channel(channel)[i] = static_cast<float>(amplitude * std::sin(phase));
        phase += increment;
    }
}

} // namespace

TEST_CASE("Distortion clips a signal that Drive's threshold doesn't reach, given identical input and gain") {
    // Both at 0 dB gain / tone=1 (bypasses the tilt coloration so this is
    // purely about the clip curve) / unity level. Drive's clip thresholds
    // (0.6/0.8) aren't reached by a 0.5-amplitude input, so it should
    // pass straight through; Distortion's much lower threshold (0.35 -
    // see Distortion.h) should already visibly compress the same input.
    // This is the main documented perceptual difference between the two
    // modules, not just a bigger gain range.
    Distortion dist;
    dist.prepare(ProcessSpec{44100.0, 256, 1});
    dist.parameters().find("gain_db")->set(0.0f);
    dist.parameters().find("tone")->set(1.0f);
    dist.parameters().find("presence")->set(0.0f);
    dist.parameters().find("level_db")->set(0.0f);

    Drive drive;
    drive.prepare(ProcessSpec{44100.0, 256, 1});
    drive.parameters().find("gain_db")->set(0.0f);
    drive.parameters().find("tone")->set(1.0f);
    drive.parameters().find("level_db")->set(0.0f);

    AudioBuffer distBuf(1, 256), driveBuf(1, 256);
    for (int i = 0; i < 256; ++i) {
        distBuf.channel(0)[i] = 0.5f;
        driveBuf.channel(0)[i] = 0.5f;
    }

    dist.process(distBuf);
    drive.process(driveBuf);

    REQUIRE_APPROX(driveBuf.channel(0)[0], 0.5f);
    REQUIRE(std::fabs(distBuf.channel(0)[0] - 0.5f) > 0.02f);
}

TEST_CASE("Distortion output stays bounded even with max gain and extreme input") {
    Distortion dist;
    dist.prepare(ProcessSpec{44100.0, 256, 1});
    dist.parameters().find("gain_db")->set(60.0f); // range max

    AudioBuffer buf(1, 256);
    for (int i = 0; i < 256; ++i) buf.channel(0)[i] = (i % 2 == 0) ? 1.0f : -1.0f;
    dist.process(buf);

    for (int i = 0; i < 256; ++i) REQUIRE(std::fabs(buf.channel(0)[i]) <= 4.0f);
}

TEST_CASE("Distortion presence knob increases high-frequency energy relative to flat") {
    ProcessSpec spec{44100.0, 4000, 1};

    Distortion flat;
    flat.prepare(spec);
    flat.parameters().find("gain_db")->set(0.0f); // avoid clipping dominating the measurement
    flat.parameters().find("presence")->set(0.0f);
    AudioBuffer flatBuf(1, 4000);
    fillSine(flatBuf, 0, 6000.0, 44100.0, 0.2);
    flat.process(flatBuf);

    Distortion bright;
    bright.prepare(spec);
    bright.parameters().find("gain_db")->set(0.0f);
    bright.parameters().find("presence")->set(1.0f);
    AudioBuffer brightBuf(1, 4000);
    fillSine(brightBuf, 0, 6000.0, 44100.0, 0.2);
    bright.process(brightBuf);

    AudioBuffer flatTail(1, 2000), brightTail(1, 2000);
    for (int i = 0; i < 2000; ++i) {
        flatTail.channel(0)[i] = flatBuf.channel(0)[2000 + i];
        brightTail.channel(0)[i] = brightBuf.channel(0)[2000 + i];
    }

    REQUIRE(rmsOf(brightTail, 0) > rmsOf(flatTail, 0));
}

TEST_CASE("Distortion bypass leaves the signal untouched") {
    Distortion dist;
    dist.prepare(ProcessSpec{44100.0, 64, 1});
    dist.setBypassed(true);
    dist.parameters().find("gain_db")->set(60.0f);

    AudioBuffer buf(1, 64);
    for (int i = 0; i < 64; ++i) buf.channel(0)[i] = 0.4f;
    dist.process(buf);

    for (int i = 0; i < 64; ++i) REQUIRE_APPROX(buf.channel(0)[i], 0.4f);
}

TEST_CASE("Distortion getState/setState round-trips all four parameters and bypass") {
    Distortion dist;
    dist.prepare(ProcessSpec{44100.0, 32, 1});
    dist.parameters().find("gain_db")->set(35.0f);
    dist.parameters().find("tone")->set(0.3f);
    dist.parameters().find("presence")->set(0.7f);
    dist.parameters().find("level_db")->set(-3.0f);
    dist.setBypassed(true);

    auto state = dist.getState();
    REQUIRE(state.moduleId == "distortion.v1");

    Distortion other;
    other.prepare(ProcessSpec{44100.0, 32, 1});
    other.setState(state);
    REQUIRE(other.isBypassed() == true);
    REQUIRE_APPROX(other.parameters().find("gain_db")->get(), 35.0f);
    REQUIRE_APPROX(other.parameters().find("tone")->get(), 0.3f);
    REQUIRE_APPROX(other.parameters().find("presence")->get(), 0.7f);
    REQUIRE_APPROX(other.parameters().find("level_db")->get(), -3.0f);
}
