#include <cmath>

#include "TestFramework.h"
#include "tfcore/dsp/modules/Drive.h"

using namespace tfcore;

TEST_CASE("Drive at unity gain and full tone leaves a quiet signal essentially unchanged") {
    Drive drive;
    drive.prepare(ProcessSpec{44100.0, 64, 1});
    drive.parameters().find("gain_db")->set(0.0f);
    drive.parameters().find("tone")->set(1.0f);
    drive.parameters().find("level_db")->set(0.0f);

    AudioBuffer buf(1, 64);
    float* data = buf.channel(0);
    for (int i = 0; i < 64; ++i) data[i] = 0.1f; // well below the soft-clip knee
    drive.process(buf);

    for (int i = 0; i < 64; ++i) REQUIRE(std::fabs(data[i] - 0.1f) < 0.01f);
}

TEST_CASE("Drive output never exceeds the soft-clip ceiling even with extreme input") {
    Drive drive;
    drive.prepare(ProcessSpec{44100.0, 128, 1});
    drive.parameters().find("gain_db")->set(40.0f); // max gain
    drive.parameters().find("tone")->set(1.0f);
    drive.parameters().find("level_db")->set(0.0f);

    AudioBuffer buf(1, 128);
    float* data = buf.channel(0);
    for (int i = 0; i < 128; ++i) data[i] = (i % 2 == 0) ? 1.0f : -1.0f;
    drive.process(buf);

    for (int i = 0; i < 128; ++i) REQUIRE(std::fabs(data[i]) <= 1.05f);
}

TEST_CASE("Drive bypass leaves the signal untouched") {
    Drive drive;
    drive.prepare(ProcessSpec{44100.0, 32, 1});
    drive.setBypassed(true);
    drive.parameters().find("gain_db")->set(40.0f);

    AudioBuffer buf(1, 32);
    float* data = buf.channel(0);
    for (int i = 0; i < 32; ++i) data[i] = 0.5f;
    drive.process(buf);

    for (int i = 0; i < 32; ++i) REQUIRE_APPROX(data[i], 0.5f);
}

TEST_CASE("Drive level parameter scales output linearly") {
    Drive drive;
    drive.prepare(ProcessSpec{44100.0, 16, 1});
    drive.parameters().find("gain_db")->set(0.0f);
    drive.parameters().find("tone")->set(1.0f);
    drive.parameters().find("level_db")->set(-6.0f); // ~0.501 linear

    AudioBuffer buf(1, 16);
    float* data = buf.channel(0);
    for (int i = 0; i < 16; ++i) data[i] = 0.1f;
    drive.process(buf);

    const float expectedLevelGain = std::pow(10.0f, -6.0f / 20.0f);
    REQUIRE(std::fabs(data[15] - 0.1f * expectedLevelGain) < 0.02f);
}

TEST_CASE("Drive getState/setState round-trips parameters and bypass") {
    Drive drive;
    drive.prepare(ProcessSpec{44100.0, 16, 1});
    drive.parameters().find("gain_db")->set(20.0f);
    drive.parameters().find("tone")->set(0.25f);
    drive.setBypassed(true);

    auto state = drive.getState();
    REQUIRE(state.moduleId == "drive.v1");

    Drive other;
    other.prepare(ProcessSpec{44100.0, 16, 1});
    other.setState(state);
    REQUIRE(other.isBypassed() == true);
    REQUIRE_APPROX(other.parameters().find("gain_db")->get(), 20.0f);
    REQUIRE_APPROX(other.parameters().find("tone")->get(), 0.25f);
}
