#include <cmath>

#include "TestFramework.h"
#include "tfcore/dsp/modules/Reverb.h"

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

// A single-sample impulse followed by silence - the standard way to
// excite a reverb network and observe its decay tail in isolation.
AudioBuffer makeImpulse(int numSamples) {
    AudioBuffer buf(1, numSamples);
    buf.channel(0)[0] = 1.0f;
    return buf;
}

} // namespace

TEST_CASE("Reverb produces a decaying tail after an impulse, not silence") {
    Reverb reverb;
    reverb.prepare(ProcessSpec{44100.0, 4000, 1});
    reverb.parameters().find("mix")->set(1.0f);       // fully wet, isolate the reverb tail
    reverb.parameters().find("predelay_ms")->set(0.0f);
    reverb.parameters().find("size")->set(0.5f);

    auto buf = makeImpulse(4000);
    reverb.process(buf);

    // Well after the impulse and predelay, the comb/allpass network should
    // still be producing audible energy - that's the entire point of a
    // reverb tail.
    REQUIRE(rmsOfRange(buf, 0, 500, 3500) > 0.001f);
}

TEST_CASE("Reverb mix parameter at 0 leaves the signal unchanged") {
    Reverb reverb;
    reverb.prepare(ProcessSpec{44100.0, 64, 1});
    reverb.parameters().find("mix")->set(0.0f);

    AudioBuffer buf(1, 64);
    for (int i = 0; i < 64; ++i) buf.channel(0)[i] = 0.33f;
    reverb.process(buf);

    for (int i = 0; i < 64; ++i) REQUIRE_APPROX(buf.channel(0)[i], 0.33f);
}

TEST_CASE("Reverb bypass leaves the signal untouched") {
    Reverb reverb;
    reverb.prepare(ProcessSpec{44100.0, 64, 1});
    reverb.setBypassed(true);
    reverb.parameters().find("mix")->set(1.0f);

    AudioBuffer buf(1, 64);
    for (int i = 0; i < 64; ++i) buf.channel(0)[i] = 0.5f;
    reverb.process(buf);

    for (int i = 0; i < 64; ++i) REQUIRE_APPROX(buf.channel(0)[i], 0.5f);
}

TEST_CASE("Reverb Hall character sustains more late-tail energy than Room at the same size") {
    // Hall uses longer comb/allpass delays and a higher feedback ceiling
    // than Room (see Reverb.h's presetFor()) - at an identical "size"
    // setting, Hall's tail should still be audibly louder late in the
    // buffer, which is the whole point of the character presets actually
    // being differentiated rather than cosmetic labels on the same math.
    constexpr int numSamples = 8000;

    Reverb hall;
    hall.prepare(ProcessSpec{44100.0, numSamples, 1});
    hall.parameters().find("character")->set(0.0f); // Hall
    hall.parameters().find("size")->set(0.5f);
    hall.parameters().find("mix")->set(1.0f);
    hall.parameters().find("predelay_ms")->set(0.0f);
    auto hallBuf = makeImpulse(numSamples);
    hall.process(hallBuf);

    Reverb room;
    room.prepare(ProcessSpec{44100.0, numSamples, 1});
    room.parameters().find("character")->set(3.0f); // Room
    room.parameters().find("size")->set(0.5f);
    room.parameters().find("mix")->set(1.0f);
    room.parameters().find("predelay_ms")->set(0.0f);
    auto roomBuf = makeImpulse(numSamples);
    room.process(roomBuf);

    const float hallLateTail = rmsOfRange(hallBuf, 0, 6000, 8000);
    const float roomLateTail = rmsOfRange(roomBuf, 0, 6000, 8000);

    REQUIRE(hallLateTail > roomLateTail);
}

TEST_CASE("Reverb output stays bounded and finite even at max size and zero damping") {
    // size=1.0, damping=0.0 is the least-damped, highest-feedback
    // configuration (the closest this module gets to its stability
    // ceiling - feedback tops out at 0.98, still < 1, so energy must
    // decay, never diverge). Feeds a broadband-ish signal continuously
    // (not just one impulse) to stress the feedback loops harder than a
    // single-impulse test would.
    Reverb reverb;
    constexpr int numSamples = 20000;
    reverb.prepare(ProcessSpec{44100.0, numSamples, 1});
    reverb.parameters().find("size")->set(1.0f);
    reverb.parameters().find("damping")->set(0.0f);
    reverb.parameters().find("mix")->set(1.0f);

    AudioBuffer buf(1, numSamples);
    for (int i = 0; i < numSamples; ++i) {
        const double t = static_cast<double>(i);
        buf.channel(0)[i] = static_cast<float>(0.6 * std::sin(0.31 * t) + 0.3 * std::sin(0.017 * t));
    }
    reverb.process(buf);

    for (int i = 0; i < numSamples; ++i) {
        const float x = buf.channel(0)[i];
        REQUIRE(std::isfinite(x));
        REQUIRE(std::fabs(x) <= 5.0f);
    }
}

TEST_CASE("Reverb getState/setState round-trips all five parameters and bypass") {
    Reverb reverb;
    reverb.prepare(ProcessSpec{44100.0, 32, 1});
    reverb.parameters().find("character")->set(2.0f);
    reverb.parameters().find("size")->set(0.7f);
    reverb.parameters().find("damping")->set(0.3f);
    reverb.parameters().find("predelay_ms")->set(25.0f);
    reverb.parameters().find("mix")->set(0.4f);
    reverb.setBypassed(true);

    auto state = reverb.getState();
    REQUIRE(state.moduleId == "reverb.v1");

    Reverb other;
    other.prepare(ProcessSpec{44100.0, 32, 1});
    other.setState(state);
    REQUIRE(other.isBypassed() == true);
    REQUIRE_APPROX(other.parameters().find("character")->get(), 2.0f);
    REQUIRE_APPROX(other.parameters().find("size")->get(), 0.7f);
    REQUIRE_APPROX(other.parameters().find("damping")->get(), 0.3f);
    REQUIRE_APPROX(other.parameters().find("predelay_ms")->get(), 25.0f);
    REQUIRE_APPROX(other.parameters().find("mix")->get(), 0.4f);
}
