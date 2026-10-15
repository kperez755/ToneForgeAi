#include <cmath>

#include "TestFramework.h"
#include "tfcore/dsp/modules/Delay.h"

using namespace tfcore;

namespace {

float maxAbsInRange(const AudioBuffer& buf, int channel, int start, int end) {
    float peak = 0.0f;
    for (int i = start; i < end && i < buf.numSamples(); ++i) {
        peak = std::max(peak, std::fabs(buf.channel(channel)[i]));
    }
    return peak;
}

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

AudioBuffer makeImpulse(int numSamples) {
    AudioBuffer buf(1, numSamples);
    buf.channel(0)[0] = 1.0f;
    return buf;
}

} // namespace

TEST_CASE("Delay produces an echo at approximately the configured delay time") {
    constexpr int numSamples = 2000;
    Delay delay;
    delay.prepare(ProcessSpec{44100.0, numSamples, 1});
    delay.parameters().find("time_ms")->set(10.0f); // 441 samples @ 44100 Hz
    delay.parameters().find("feedback")->set(0.0f); // single, isolated echo
    delay.parameters().find("mix")->set(1.0f);

    auto buf = makeImpulse(numSamples);
    delay.process(buf);

    // Silence between the direct impulse and the echo.
    REQUIRE(maxAbsInRange(buf, 0, 5, 430) < 0.01f);
    // The echo itself, close to the expected 441-sample mark.
    REQUIRE(maxAbsInRange(buf, 0, 435, 448) > 0.9f);
    // Feedback is 0, so there should be no second repeat.
    REQUIRE(maxAbsInRange(buf, 0, 900, 1300) < 0.01f);
}

TEST_CASE("Delay mix at 0 leaves the signal unchanged") {
    Delay delay;
    delay.prepare(ProcessSpec{44100.0, 64, 1});
    delay.parameters().find("mix")->set(0.0f);

    AudioBuffer buf(1, 64);
    for (int i = 0; i < 64; ++i) buf.channel(0)[i] = 0.33f;
    delay.process(buf);

    for (int i = 0; i < 64; ++i) REQUIRE_APPROX(buf.channel(0)[i], 0.33f);
}

TEST_CASE("Delay feedback sustains tail energy longer than no feedback") {
    constexpr int numSamples = 6000;

    Delay noFeedback;
    noFeedback.prepare(ProcessSpec{44100.0, numSamples, 1});
    noFeedback.parameters().find("time_ms")->set(5.0f);
    noFeedback.parameters().find("feedback")->set(0.0f);
    noFeedback.parameters().find("mix")->set(1.0f);
    auto noFbBuf = makeImpulse(numSamples);
    noFeedback.process(noFbBuf);

    Delay withFeedback;
    withFeedback.prepare(ProcessSpec{44100.0, numSamples, 1});
    withFeedback.parameters().find("time_ms")->set(5.0f);
    withFeedback.parameters().find("feedback")->set(0.85f);
    withFeedback.parameters().find("mix")->set(1.0f);
    auto fbBuf = makeImpulse(numSamples);
    withFeedback.process(fbBuf);

    // Late in the buffer, the no-feedback case has long since gone
    // silent (one echo, done) while the feedback case is still
    // repeating.
    REQUIRE(rmsOfRange(fbBuf, 0, 4000, numSamples) > rmsOfRange(noFbBuf, 0, 4000, numSamples));
}

TEST_CASE("Delay output stays bounded and finite at maximum feedback") {
    Delay delay;
    constexpr int numSamples = 20000;
    delay.prepare(ProcessSpec{44100.0, numSamples, 1});
    delay.parameters().find("time_ms")->set(20.0f);
    delay.parameters().find("feedback")->set(0.95f); // range maximum - closest this gets to instability
    delay.parameters().find("mix")->set(1.0f);

    AudioBuffer buf(1, numSamples);
    for (int i = 0; i < numSamples; ++i) {
        const double t = static_cast<double>(i);
        buf.channel(0)[i] = static_cast<float>(0.5 * std::sin(0.2 * t));
    }
    delay.process(buf);

    for (int i = 0; i < numSamples; ++i) {
        const float x = buf.channel(0)[i];
        REQUIRE(std::isfinite(x));
        REQUIRE(std::fabs(x) <= 20.0f);
    }
}

TEST_CASE("Delay bypass leaves the signal untouched") {
    Delay delay;
    delay.prepare(ProcessSpec{44100.0, 64, 1});
    delay.setBypassed(true);
    delay.parameters().find("mix")->set(1.0f);

    AudioBuffer buf(1, 64);
    for (int i = 0; i < 64; ++i) buf.channel(0)[i] = 0.5f;
    delay.process(buf);

    for (int i = 0; i < 64; ++i) REQUIRE_APPROX(buf.channel(0)[i], 0.5f);
}

TEST_CASE("Delay getState/setState round-trips all four parameters and bypass") {
    Delay delay;
    delay.prepare(ProcessSpec{44100.0, 32, 1});
    delay.parameters().find("time_ms")->set(500.0f);
    delay.parameters().find("feedback")->set(0.5f);
    delay.parameters().find("tone")->set(0.2f);
    delay.parameters().find("mix")->set(0.6f);
    delay.setBypassed(true);

    auto state = delay.getState();
    REQUIRE(state.moduleId == "delay.v1");

    Delay other;
    other.prepare(ProcessSpec{44100.0, 32, 1});
    other.setState(state);
    REQUIRE(other.isBypassed() == true);
    REQUIRE_APPROX(other.parameters().find("time_ms")->get(), 500.0f);
    REQUIRE_APPROX(other.parameters().find("feedback")->get(), 0.5f);
    REQUIRE_APPROX(other.parameters().find("tone")->get(), 0.2f);
    REQUIRE_APPROX(other.parameters().find("mix")->get(), 0.6f);
}
