#include <cmath>

#include "TestFramework.h"
#include "tfcore/dsp/modules/NoiseGate.h"

using namespace tfcore;

namespace {

AudioBuffer makeSilence(int channels, int samples) {
    AudioBuffer buf(channels, samples);
    buf.clear();
    return buf;
}

AudioBuffer makeLoudTone(int channels, int samples, float amplitude = 0.8f) {
    AudioBuffer buf(channels, samples);
    for (int ch = 0; ch < channels; ++ch) {
        float* data = buf.channel(ch);
        for (int i = 0; i < samples; ++i) data[i] = amplitude;
    }
    return buf;
}

} // namespace

TEST_CASE("NoiseGate silences a buffer well below threshold after settling") {
    NoiseGate gate;
    ProcessSpec spec{44100.0, 512, 1};
    gate.prepare(spec);
    gate.parameters().find("threshold_db")->set(-40.0f);
    gate.parameters().find("attack_ms")->set(1.0f);
    gate.parameters().find("hold_ms")->set(0.0f);
    gate.parameters().find("release_ms")->set(5.0f);

    // Feed a very quiet signal (-80 dBFS-ish) for long enough that the
    // envelope follower and gain smoothing settle to fully closed.
    for (int block = 0; block < 50; ++block) {
        AudioBuffer buf(1, 512);
        float* data = buf.channel(0);
        for (int i = 0; i < 512; ++i) data[i] = 0.0001f;
        gate.process(buf);
        if (block == 49) {
            // By the last block the gate should have closed almost fully.
            REQUIRE(std::fabs(data[511]) < 0.0001f * 0.5f);
        }
    }
}

TEST_CASE("NoiseGate passes a loud signal through essentially unattenuated once open") {
    NoiseGate gate;
    ProcessSpec spec{44100.0, 512, 1};
    gate.prepare(spec);
    gate.parameters().find("threshold_db")->set(-40.0f);
    gate.parameters().find("attack_ms")->set(0.1f);
    gate.parameters().find("hold_ms")->set(50.0f);
    gate.parameters().find("release_ms")->set(50.0f);

    // First block opens the gate; by a later block within one sample's
    // amplitude it should be fully open.
    for (int block = 0; block < 10; ++block) {
        auto buf = makeLoudTone(1, 512);
        gate.process(buf);
        if (block == 9) {
            REQUIRE(buf.channel(0)[511] > 0.79f);
        }
    }
}

TEST_CASE("NoiseGate bypass leaves the signal untouched") {
    NoiseGate gate;
    ProcessSpec spec{44100.0, 256, 1};
    gate.prepare(spec);
    gate.setBypassed(true);

    AudioBuffer buf(1, 256);
    float* data = buf.channel(0);
    for (int i = 0; i < 256; ++i) data[i] = 0.0001f; // would normally be gated closed
    gate.process(buf);

    for (int i = 0; i < 256; ++i) REQUIRE_APPROX(data[i], 0.0001f);
}

TEST_CASE("NoiseGate getState/setState round-trips parameters and bypass") {
    NoiseGate gate;
    gate.prepare(ProcessSpec{44100.0, 256, 1});
    gate.parameters().find("threshold_db")->set(-30.0f);
    gate.setBypassed(true);

    auto state = gate.getState();
    REQUIRE(state.moduleId == "noise_gate.v1");
    REQUIRE(state.bypassed == true);
    REQUIRE_APPROX(state.parameters.at("threshold_db"), -30.0f);

    NoiseGate other;
    other.prepare(ProcessSpec{44100.0, 256, 1});
    other.setState(state);
    REQUIRE(other.isBypassed() == true);
    REQUIRE_APPROX(other.parameters().find("threshold_db")->get(), -30.0f);
}
